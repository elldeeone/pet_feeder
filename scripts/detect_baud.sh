#!/bin/bash
#
# Detect correct baud rate for UART serial device
# Usage: ./detect_baud.sh
# Then power on/reboot the device while this runs
#

SERIAL=/dev/cu.usbserial-BG0192MA
BAUDS=(115200 9600 921600 1500000 57600 38400 74880 19200 230400 460800 4800 2400)
SAMPLE_TIME=3

echo ""
echo "=== UART Baud Rate Detector ==="
echo "=== Power cycle the device NOW or while this runs ==="
echo ""

for baud in "${BAUDS[@]}"; do
    echo -n "Testing ${baud}... "

    stty -f "$SERIAL" "$baud" cs8 -cstopb -parenb raw -echo 2>/dev/null
    if [ $? -ne 0 ]; then
        echo "SKIP (unsupported)"
        continue
    fi

    # Capture for SAMPLE_TIME seconds
    timeout "$SAMPLE_TIME" cat "$SERIAL" > /tmp/baud_test.bin 2>/dev/null

    BYTES=$(wc -c < /tmp/baud_test.bin | tr -d ' ')

    if [ "$BYTES" -eq 0 ]; then
        echo "no data"
        continue
    fi

    # Count printable ASCII characters vs total
    PRINTABLE=$(tr -cd '[:print:]\n\r\t' < /tmp/baud_test.bin | wc -c | tr -d ' ')

    if [ "$BYTES" -gt 0 ]; then
        PCT=$((PRINTABLE * 100 / BYTES))
    else
        PCT=0
    fi

    echo "${BYTES} bytes, ${PCT}% printable"

    if [ "$PCT" -gt 70 ]; then
        echo ""
        echo ">>> MATCH! Baud rate is likely: ${baud}"
        echo ""
        echo "--- Sample output ---"
        head -c 500 /tmp/baud_test.bin | strings
        echo ""
        echo "--- End sample ---"
        echo ""
        echo "To connect:"
        echo "  stty -f $SERIAL $baud cs8 -cstopb -parenb raw -echo"
        echo "  cat $SERIAL | tee /tmp/serial_capture.log"
        echo ""
        exit 0
    fi
done

echo ""
echo "No clear match found. Try power-cycling the device and run again."
echo "If the device boot-loops very fast, data may not arrive during the sample window."
