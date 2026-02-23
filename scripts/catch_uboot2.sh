#!/bin/bash
#
# Catch U-Boot by trying multiple baud rates during early boot
# The bootloader may init at a different rate than Linux
#

SERIAL=/dev/cu.usbserial-BG0192MA

echo ""
echo "=== U-Boot Catcher v2 ==="
echo "=== Power cycle the device IMMEDIATELY after running this ==="
echo ""

# Try each baud rate: spam spaces for a few seconds, then check for prompt
for baud in 115200 57600 38400 9600 19200 230400; do
    echo "--- Trying ${baud} baud ---"
    stty -f "$SERIAL" "$baud" cs8 -cstopb -parenb raw -echo 2>/dev/null || continue

    exec 3<>"$SERIAL"

    # Spam spaces rapidly
    for i in $(seq 1 50); do
        printf ' ' >&3
        sleep 0.02
    done

    # Read whatever came back
    timeout 2 cat <&3 > /tmp/uboot_test.bin 2>/dev/null &
    RPID=$!
    sleep 0.5

    # Send newline and see if we get a prompt
    printf '\r' >&3
    sleep 1.5
    kill $RPID 2>/dev/null
    wait $RPID 2>/dev/null

    exec 3>&-

    BYTES=$(wc -c < /tmp/uboot_test.bin | tr -d ' ')
    PRINTABLE=$(tr -cd '[:print:]\n\r\t' < /tmp/uboot_test.bin 2>/dev/null | wc -c | tr -d ' ')
    if [ "$BYTES" -gt 0 ]; then
        PCT=$((PRINTABLE * 100 / BYTES))
    else
        PCT=0
    fi

    echo "  ${BYTES} bytes, ${PCT}% printable"

    if [ "$PCT" -gt 50 ] && [ "$BYTES" -gt 5 ]; then
        echo ""
        echo ">>> Readable output at ${baud}!"
        echo "--- Content: ---"
        cat /tmp/uboot_test.bin
        echo ""
        echo "--- End ---"

        if grep -q "anyka\|U-Boot\|=>\ " /tmp/uboot_test.bin 2>/dev/null; then
            echo ">>> U-Boot prompt detected at ${baud}!"
        fi

        echo ""
        echo "To connect interactively:"
        echo "  screen $SERIAL $baud"
        exit 0
    fi
done

echo ""
echo "No readable baud rate found during this boot cycle."
echo "Try again - timing is tricky. Power cycle RIGHT as you start the script."
