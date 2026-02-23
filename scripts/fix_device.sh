#!/bin/bash
#
# Fix bricked pet feeder via UART serial console
#
# Opens serial port ONCE, reads continuously in background,
# watches for login prompt, then sends fix commands.
#

SERIAL=/dev/cu.usbserial-BG0192MA
LOG=/tmp/serial_recovery.log
PASSWORD="${PET_FEEDER_PASS:-AK2040jk}"

# Kill anything holding the port
lsof "$SERIAL" 2>/dev/null | awk 'NR>1{print $2}' | sort -u | xargs kill -9 2>/dev/null || true
sleep 1

# Configure serial port
stty -f "$SERIAL" 115200 cs8 -cstopb -parenb raw -echo

# Clean log
> "$LOG"

echo ""
echo "=== Pet Feeder Recovery Script ==="
echo "=== Power on the feeder NOW if you haven't already ==="
echo ""

# Open serial port as file descriptor 3 for read/write
exec 3<>"$SERIAL"

# Background reader: continuously read from fd 3 and append to log
cat <&3 >> "$LOG" &
READER_PID=$!

# Cleanup on exit
cleanup() {
    kill $READER_PID 2>/dev/null
    exec 3>&- 2>/dev/null
    echo "Cleaned up."
}
trap cleanup EXIT

echo ">>> Waiting for login prompt (up to 2 minutes)..."
echo ">>> (You can watch raw output: tail -f $LOG)"
echo ""

TIMEOUT=120
ELAPSED=0
GOT_LOGIN=0

while [ $ELAPSED -lt $TIMEOUT ]; do
    if grep -q "login:" "$LOG" 2>/dev/null; then
        GOT_LOGIN=1
        echo ">>> Got login prompt!"
        break
    fi
    sleep 1
    ELAPSED=$((ELAPSED + 1))
    # Show a dot every 10 seconds so user knows it's alive
    if [ $((ELAPSED % 10)) -eq 0 ]; then
        echo ">>>   ...still waiting (${ELAPSED}s)"
    fi
done

if [ $GOT_LOGIN -eq 0 ]; then
    echo ">>> ERROR: No login prompt after ${TIMEOUT}s."
    echo ">>> Check wires and power. Raw output saved to $LOG"
    echo ">>> Last 20 lines:"
    tail -20 "$LOG"
    exit 1
fi

sleep 1

echo ">>> Logging in as root..."
printf 'root\r' >&3
sleep 1

if grep -q "Password:" "$LOG" 2>/dev/null; then
    printf '%s\r' "$PASSWORD" >&3
    sleep 2
fi

echo ">>> Checking for shell..."
printf 'echo SHELL_OK\r' >&3
sleep 2

if grep -q "SHELL_OK" "$LOG" 2>/dev/null; then
    echo ">>> Shell confirmed!"
else
    echo ">>> Warning: Could not confirm shell, attempting fix anyway..."
fi

echo ""
echo ">>> Current tmp_test.sh:"
printf 'cat /mnt/config/tmp_test.sh\r' >&3
sleep 2
# Show recent log lines
tail -5 "$LOG"
echo ""

echo ">>> Writing fixed tmp_test.sh..."
printf "echo '#!/bin/sh' > /mnt/config/tmp_test.sh\r" >&3
sleep 1
printf "echo '# recovery mode: keep stock boot services' >> /mnt/config/tmp_test.sh\r" >&3
sleep 1
printf "echo 'sleep 2' >> /mnt/config/tmp_test.sh\r" >&3
sleep 1
printf "echo 'exit 0' >> /mnt/config/tmp_test.sh\r" >&3
sleep 1

echo ">>> Verifying fix..."
printf 'cat /mnt/config/tmp_test.sh\r' >&3
sleep 2
echo "--- Verification output: ---"
tail -5 "$LOG"
echo "---"

echo ""
echo ">>> Syncing filesystem..."
printf 'sync\r' >&3
sleep 2

echo ">>> Rebooting device..."
printf 'reboot\r' >&3
sleep 3

echo ""
echo "=== DONE ==="
echo "=== Device is rebooting. WiFi should come back in ~30 seconds ==="
echo "=== Try: ping 10.0.10.129 ==="
echo ""
