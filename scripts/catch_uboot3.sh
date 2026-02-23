#!/bin/bash
#
# Catch U-Boot - starts spamming IMMEDIATELY, no delays
#

SERIAL=/dev/cu.usbserial-BG0192MA
stty -f "$SERIAL" 115200 cs8 -cstopb -parenb raw -echo

exec 3<>"$SERIAL"

# Start reader in background
cat <&3 | tee /tmp/uboot_capture.log &
READER_PID=$!

cleanup() {
    kill $READER_PID 2>/dev/null
    exec 3>&- 2>/dev/null
}
trap cleanup EXIT

# START SPAMMING IMMEDIATELY - no echo, no delay, just spam
# This is the critical part - spaces must be flowing before U-Boot starts
while true; do
    printf '   ' >&3
    # Check if we caught it
    if grep -q "anyka\$" /tmp/uboot_capture.log 2>/dev/null; then
        break
    fi
    if grep -q "Hit any key" /tmp/uboot_capture.log 2>/dev/null; then
        # Extra burst once we see the countdown
        for i in $(seq 1 20); do printf ' ' >&3; done
        sleep 2
        break
    fi
done

echo ""
echo ">>> Should be at U-Boot. Interactive mode:"
echo ">>> Type commands. 'quit' to exit."
echo ""

# Clear log for fresh prompt detection
printf '\r' >&3
sleep 1

while IFS= read -r -p "uboot> " cmd; do
    [ "$cmd" = "quit" ] && break
    printf '%s\r' "$cmd" >&3
    sleep 1
done
