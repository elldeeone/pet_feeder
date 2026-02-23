#!/bin/bash
set -e

DEVICE_IP="${1:-10.0.10.129}"
BINARY="$(dirname "$0")/mqtt_feeder"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: mqtt_feeder binary not found. Run build.sh first."
    exit 1
fi

BINARY_SIZE=$(wc -c < "$BINARY" | tr -d ' ')
echo "=== Deploying mqtt_feeder to $DEVICE_IP ==="
echo "Binary: $BINARY_SIZE bytes"

# Compress for flash storage (768KB jffs2 partition)
COMPRESSED="/tmp/mqtt_feeder_deploy.gz"
gzip -c "$BINARY" > "$COMPRESSED"
COMPRESSED_SIZE=$(wc -c < "$COMPRESSED" | tr -d ' ')
echo "Compressed: $COMPRESSED_SIZE bytes"

# --- Step 1: Stop existing instance, clean up ---
echo "Stopping existing instance..."
echo "killall mqtt_feeder 2>/dev/null; true" | nc -w 3 "$DEVICE_IP" 9990 || true
sleep 1

# --- Step 2: Transfer compressed binary via nc ---
echo "Starting nc listener on device..."
expect -c "
set timeout 120
spawn telnet $DEVICE_IP
expect \"login:\"
send \"root\r\"
expect \"Password:\"
send \"AK2040jk\r\"
expect \"#\"
send \"rm -f /mnt/config/mqtt_feeder.gz.new\r\"
expect \"#\"
send \"nc -l -p 9991 > /mnt/config/mqtt_feeder.gz.new; echo XFER_COMPLETE\r\"
expect \"XFER_COMPLETE\" timeout {
    puts \"TIMEOUT waiting for transfer\"
    exit 1
}
expect \"#\"
send \"ls -l /mnt/config/mqtt_feeder.gz.new\r\"
expect \"#\"
send \"exit\r\"
expect eof
" &
EXPECT_PID=$!

# Wait for nc listener to be ready
sleep 5

echo "Pushing compressed binary..."
cat "$COMPRESSED" | nc -w 10 "$DEVICE_IP" 9991
echo "Push sent, waiting for device to finish..."

# Wait for expect (telnet session) to complete
wait $EXPECT_PID
XFER_RC=$?
if [ $XFER_RC -ne 0 ]; then
    echo "ERROR: Transfer failed"
    rm -f "$COMPRESSED"
    exit 1
fi
echo "Transfer successful"

# --- Step 3: Install and set up ---
echo "Installing..."
expect -c "
set timeout 15
spawn telnet $DEVICE_IP
expect \"login:\"
send \"root\r\"
expect \"Password:\"
send \"AK2040jk\r\"
expect \"#\"
# Replace old compressed binary
send \"mv /mnt/config/mqtt_feeder.gz.new /mnt/config/mqtt_feeder.gz\r\"
expect \"#\"
# Decompress to /tmp
send \"gunzip -c /mnt/config/mqtt_feeder.gz > /tmp/mqtt_feeder && chmod +x /tmp/mqtt_feeder\r\"
expect \"#\"
send \"ls -l /tmp/mqtt_feeder /mnt/config/mqtt_feeder.gz\r\"
expect \"#\"
# Config file
send \"test -f /mnt/config/mqtt_feeder.conf || printf 'broker_host=localhost\nbroker_port=1883\nserial_path=/dev/ttySAK1\n' > /mnt/config/mqtt_feeder.conf\r\"
expect \"#\"
# Auto-start script
send \"printf '#!/bin/sh\nsleep 15\ngunzip -c /mnt/config/mqtt_feeder.gz > /tmp/mqtt_feeder\nchmod +x /tmp/mqtt_feeder\nkillall mqtt_feeder 2>/dev/null\n/tmp/mqtt_feeder >> /tmp/mqtt_feeder.log 2>&1 &\n' > /mnt/config/tmp_test.sh && chmod +x /mnt/config/tmp_test.sh\r\"
expect \"#\"
send \"cat /mnt/config/tmp_test.sh\r\"
expect \"#\"
send \"df -h /mnt/config\r\"
expect \"#\"
send \"exit\r\"
expect eof
"

rm -f "$COMPRESSED"

echo ""
echo "=== Deploy complete ==="
echo ""
echo "Start now:    echo '/tmp/mqtt_feeder &' | nc -w 3 $DEVICE_IP 9990"
echo "View logs:    echo 'cat /tmp/mqtt_feeder.log' | nc -w 3 $DEVICE_IP 9990"
echo "Auto-start:   runs on reboot via /mnt/config/tmp_test.sh"
