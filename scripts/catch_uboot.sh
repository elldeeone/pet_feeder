#!/bin/bash
#
# Catch U-Boot prompt by spamming keys during autoboot countdown
# Usage: ./catch_uboot.sh then power cycle the device
#

SERIAL=/dev/cu.usbserial-BG0192MA

stty -f "$SERIAL" 115200 cs8 -cstopb -parenb raw -echo

# Open serial as fd 3 for read/write
exec 3<>"$SERIAL"

# Background reader: show everything on screen and save to log
cat <&3 | tee /tmp/uboot_capture.log &
READER_PID=$!

cleanup() {
    kill $READER_PID 2>/dev/null
    exec 3>&- 2>/dev/null
    echo ""
    echo "Cleaned up."
}
trap cleanup EXIT

echo ""
echo "=== U-Boot Catcher ==="
echo "=== Power cycle the device NOW ==="
echo "=== Spamming spacebar to catch autoboot... ==="
echo ""

# Spam spaces for 30 seconds to catch the autoboot window
for i in $(seq 1 300); do
    printf ' ' >&3
    sleep 0.1
done

echo ""
echo "=== Spam done. Checking if we caught U-Boot... ==="
sleep 1

# Send a newline to see if we get a prompt
printf '\r' >&3
sleep 1

if grep -q "anyka\$\|U-Boot>\|=>" /tmp/uboot_capture.log 2>/dev/null; then
    echo ""
    echo ">>> SUCCESS! Got U-Boot prompt!"
    echo ">>> Dropping you into interactive mode. Type commands below."
    echo ">>> Type 'quit' to exit."
    echo ""

    while IFS= read -r -p "uboot> " cmd; do
        if [ "$cmd" = "quit" ]; then
            break
        fi
        printf '%s\r' "$cmd" >&3
        sleep 1
    done
else
    echo ""
    echo ">>> Didn't detect U-Boot prompt in log."
    echo ">>> Dropping you into interactive mode anyway - check the output above."
    echo ">>> Type commands, 'quit' to exit."
    echo ""

    while IFS= read -r -p "uboot> " cmd; do
        if [ "$cmd" = "quit" ]; then
            break
        fi
        printf '%s\r' "$cmd" >&3
        sleep 1
    done
fi
