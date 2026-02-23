#!/usr/bin/expect -f
#
# Deploy mqtt_feeder via base64 over telnet
# Works across subnets where nc/wget can't reach
#

set timeout 30

spawn telnet 10.0.10.129
expect "login:"
send "root\r"
expect "Password:"
send "AK2040jk\r"
expect "#"

# Start receiving base64
send "cat > /tmp/mf_b64 << 'ENDOFFILE'\r"

# Read the base64 file and send it line by line
set fp [open "/tmp/mf_b64.txt" r]
set linenum 0
while {[gets $fp line] >= 0} {
    send "$line\r"
    incr linenum
    if {$linenum % 100 == 0} {
        # Brief pause every 100 lines to avoid buffer overflow
        after 200
        puts "Sent $linenum lines..."
    }
}
close $fp

send "ENDOFFILE\r"
expect "#"
puts "Base64 transfer complete ($linenum lines)"

# Decode and install
send "base64 -d /tmp/mf_b64 > /tmp/mf_new && echo DECODED\r"
expect "DECODED"
expect "#"

send "chmod +x /tmp/mf_new && md5sum /tmp/mf_new && wc -c /tmp/mf_new\r"
expect "#"

send "cp /tmp/mf_new /mnt/config/mqtt_feeder && sync && echo INSTALLED\r"
expect "INSTALLED"
expect "#"

send "reboot\r"
expect eof

puts "Deploy complete!"
