#!/usr/bin/expect -f
#
# Transfer ArnCo RTSP binary to device via base64 over telnet
# File is ~1.6MB -> ~29K lines base64
#

set timeout 60

spawn telnet 10.0.10.129
expect "login:"
send "root\r"
expect "Password:"
send "AK2040jk\r"
expect "#"

# Clean up and prep
send "rm -f /tmp/bin/ak_tuya_ipc /tmp/arnco_b64\r"
expect "#"
send "mkdir -p /tmp/bin\r"
expect "#"

# Start receiving base64
send "cat > /tmp/arnco_b64 << 'ENDOFFILE'\r"

# Read the base64 file and send it line by line
set fp [open "/tmp/arnco_b64.txt" r]
set linenum 0
while {[gets $fp line] >= 0} {
    send "$line\r"
    incr linenum
    if {$linenum % 200 == 0} {
        after 100
        puts "Sent $linenum / 29022 lines..."
    }
}
close $fp

send "ENDOFFILE\r"
expect -timeout 120 "#"
puts "Base64 transfer complete ($linenum lines)"

# Decode
send "base64 -d /tmp/arnco_b64 > /tmp/bin/ak_tuya_ipc && echo DECODED\r"
expect -timeout 30 "DECODED"
expect "#"

send "chmod +x /tmp/bin/ak_tuya_ipc && wc -c /tmp/bin/ak_tuya_ipc && md5sum /tmp/bin/ak_tuya_ipc\r"
expect "#"

# Kill stock process
send "killall tuya_daemon.sh 2>/dev/null; killall ak_tuya_ipc 2>/dev/null; sleep 1\r"
expect "#"

# Run
send "/tmp/bin/ak_tuya_ipc >> /tmp/rtsp.log 2>&1 &\r"
expect "#"

# Wait for RTSP to init
send "sleep 8 && netstat -tlnp 2>/dev/null | grep -E '554|8554' && echo RTSP_OK || echo NO_RTSP\r"
expect -timeout 20 "#"

send "cat /tmp/rtsp.log 2>/dev/null | tail -20\r"
expect "#"

send "exit\r"
expect eof

puts "Done!"
