#!/bin/bash
#
# Recover an Anyka-based pet feeder stuck in a reboot loop.
# This script uses UART, logs in, and restores a safe /mnt/config/tmp_test.sh.
#
# Usage:
#   ./recover_uart_bootloop.sh [serial_device] [baud]
# Example:
#   ./recover_uart_bootloop.sh /dev/cu.usbserial-BG0192MA 115200
#

set -euo pipefail

SERIAL="${1:-/dev/cu.usbserial-BG0192MA}"
BAUD="${2:-115200}"
PASSWORD="${PET_FEEDER_PASS:-AK2040jk}"
LOG="${PET_FEEDER_LOG:-/tmp/pet_feeder_recover_uart.log}"
WAIT_TIMEOUT="${PET_FEEDER_TIMEOUT:-180}"
AUTO_REBOOT="${PET_FEEDER_AUTO_REBOOT:-1}"

cleanup() {
    if [ -n "${READER_PID:-}" ]; then
        kill "$READER_PID" 2>/dev/null || true
    fi
    exec 3>&- 2>/dev/null || true
}
trap cleanup EXIT

wait_for_pattern() {
    local pattern="$1"
    local timeout="$2"
    local elapsed=0
    while [ "$elapsed" -lt "$timeout" ]; do
        if grep -Eq "$pattern" "$LOG" 2>/dev/null; then
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    return 1
}

echo "=== UART Recovery ==="
echo "Serial: $SERIAL"
echo "Baud:   $BAUD"
echo "Log:    $LOG"
echo ""
echo "Power-cycle the feeder now if it is not already looping."

# Kill anything currently holding the UART device.
lsof "$SERIAL" 2>/dev/null | awk 'NR>1{print $2}' | sort -u | xargs kill -9 2>/dev/null || true
sleep 1

# Configure UART.
stty -f "$SERIAL" "$BAUD" cs8 -cstopb -parenb raw -echo

# Open fd3 read/write and start background log capture.
> "$LOG"
exec 3<>"$SERIAL"
cat <&3 >> "$LOG" &
READER_PID=$!

echo "Waiting for login prompt..."

FOUND_LOGIN=0
for _ in $(seq 1 "$WAIT_TIMEOUT"); do
    printf '\r' >&3
    if grep -Eq "ipc login:|login:" "$LOG" 2>/dev/null; then
        FOUND_LOGIN=1
        break
    fi
    sleep 1
done

if [ "$FOUND_LOGIN" -ne 1 ]; then
    echo "ERROR: did not detect login prompt within ${WAIT_TIMEOUT}s."
    echo "Last UART output:"
    tail -n 40 "$LOG" || true
    exit 1
fi

echo "Login prompt detected. Attempting root login..."
printf 'root\r' >&3

if wait_for_pattern "Password:" 5; then
    printf '%s\r' "$PASSWORD" >&3
fi

if ! wait_for_pattern "\\[root@ipc:|# " 10; then
    echo "ERROR: shell prompt not detected after login."
    echo "Last UART output:"
    tail -n 60 "$LOG" || true
    exit 1
fi

echo "Shell detected. Restoring safe startup script..."

printf 'cp -f /mnt/config/tmp_test.sh /mnt/config/tmp_test.sh.bak 2>/dev/null || true\r' >&3
sleep 1
printf "printf '#!/bin/sh\\n# recovery mode: keep stock boot services\\nsleep 2\\nexit 0\\n' > /mnt/config/tmp_test.sh\r" >&3
sleep 1
printf 'chmod 755 /mnt/config/tmp_test.sh\r' >&3
sleep 1
printf 'cat /mnt/config/tmp_test.sh\r' >&3
sleep 1
printf 'sync\r' >&3
sleep 1
printf 'echo RECOVERY_OK\r' >&3

if ! wait_for_pattern "RECOVERY_OK" 8; then
    echo "WARNING: did not see RECOVERY_OK marker. Check $LOG manually."
else
    echo "Recovery script written successfully."
fi

if [ "$AUTO_REBOOT" = "1" ]; then
    echo "Rebooting device..."
    printf 'reboot\r' >&3
    sleep 1
else
    echo "Auto reboot disabled (PET_FEEDER_AUTO_REBOOT=$AUTO_REBOOT)."
fi

echo "Done. UART log saved to $LOG"
