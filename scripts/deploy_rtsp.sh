#!/usr/bin/expect -f
#
# Deploy rtsp_server via base64 over telnet
#

set timeout 30

spawn telnet 10.0.10.129
expect "login:"
send "root\r"
expect "Password:"
send "AK2040jk\r"
expect "#"

# Start receiving base64
send "cat > /tmp/rtsp_b64 << 'ENDOFFILE'\r"

# Read the base64 file and send it line by line
set fp [open "/tmp/rtsp_b64.txt" r]
set linenum 0
while {[gets $fp line] >= 0} {
    send "$line\r"
    incr linenum
    if {$linenum % 100 == 0} {
        after 200
        puts "Sent $linenum lines..."
    }
}
close $fp

send "ENDOFFILE\r"
expect "#"
puts "Base64 transfer complete ($linenum lines)"

# Decode and install
send "base64 -d /tmp/rtsp_b64 > /tmp/rtsp_new && echo DECODED\r"
expect "DECODED"
expect "#"

send "chmod +x /tmp/rtsp_new && md5sum /tmp/rtsp_new && wc -c /tmp/rtsp_new\r"
expect "#"

send "cp /tmp/rtsp_new /mnt/config/rtsp_server && sync && echo INSTALLED\r"
expect "INSTALLED"
expect "#"

# Update startup script - safer boot behavior:
# - do not kill stock watchdog/Tuya daemons at boot
# - start mqtt_feeder from .gz or plain binary if available
# - start rtsp_server only when present
send "cat > /mnt/config/tmp_test.sh << 'STARTUPEOF'\r"
send "#!/bin/sh\r"
send "(\r"
send "  # Wait for WiFi\r"
send "  i=0\r"
send "  while \[ \$i -lt 60 \]; do\r"
send "    ip addr show wlan0 2>/dev/null | grep -q 'inet ' && break\r"
send "    i=\$((i+1))\r"
send "    sleep 1\r"
send "  done\r"
send "  # Restart only custom services (leave stock daemons alone)\r"
send "  killall mqtt_feeder 2>/dev/null\r"
send "  killall rtsp_server 2>/dev/null\r"
send "  sleep 1\r"
send "  # uClibc compat: device libc has pthread/m/dl/rt built in\r"
send "  # but binary expects separate .so.0 files\r"
send "  ln -sf /lib/libuClibc-1.0.28.so /tmp/libpthread.so.0\r"
send "  ln -sf /lib/libuClibc-1.0.28.so /tmp/libm.so.0\r"
send "  ln -sf /lib/libuClibc-1.0.28.so /tmp/libdl.so.0\r"
send "  ln -sf /lib/libuClibc-1.0.28.so /tmp/librt.so.0\r"
send "  export LD_LIBRARY_PATH=/tmp:/usr/lib\r"
send "  if \[ -f /mnt/config/mqtt_feeder.gz \]; then\r"
send "    gunzip -c /mnt/config/mqtt_feeder.gz > /tmp/mqtt_feeder\r"
send "    chmod +x /tmp/mqtt_feeder\r"
send "    /tmp/mqtt_feeder >> /tmp/mqtt_feeder.log 2>&1 &\r"
send "  elif \[ -x /mnt/config/mqtt_feeder \]; then\r"
send "    /mnt/config/mqtt_feeder >> /tmp/mqtt_feeder.log 2>&1 &\r"
send "  fi\r"
send "  if \[ -x /mnt/config/rtsp_server \]; then\r"
send "    /mnt/config/rtsp_server >> /tmp/rtsp_server.log 2>&1 &\r"
send "  fi\r"
send ") &\r"
send "STARTUPEOF\r"
expect "#"

send "chmod +x /mnt/config/tmp_test.sh && sync && echo STARTUP_OK\r"
expect "STARTUP_OK"
expect "#"

send "df -h /mnt/config/\r"
expect "#"

puts "RTSP server deployed! Rebooting..."
send "reboot\r"
expect eof

puts "Deploy complete!"
