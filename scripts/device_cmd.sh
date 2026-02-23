#!/usr/bin/expect -f
#
# Run a command on the pet feeder via telnet
# Usage: ./device_cmd.sh "command here"
#

set timeout 10
set cmd [lindex $argv 0]

spawn telnet 10.0.10.129
expect "login:"
send "root\r"
expect "Password:"
send "AK2040jk\r"
expect "#"
send "$cmd\r"
expect "#"
puts $expect_out(buffer)
send "exit\r"
expect eof
