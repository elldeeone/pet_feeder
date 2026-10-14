#!/bin/sh
# Correct this SC223A feeder's IR-cut GPIO map in the existing RTSP binary.
# Original firmware uses GPIO80/81; ArnCo uses 23/24 and drives 81 as a speaker.
# Runs before the normal boot-time binary swap. Never opens MQTT or the UART.
set -eu

source_file=${CAMERA_HW_SOURCE:-/usr/sbin/ak_tuya_rtsp}
patched_file=${CAMERA_HW_PATCHED:-/tmp/bin/ak_tuya_rtsp_gpio_fixed}
running_file=${CAMERA_HW_RUNNING:-/tmp/bin/ak_tuya_ipc}
source_sha=5c07bcd7b0bdd0d76583ea15ca0a241f3c467afcc617204eddfe7fa8546fdf23
patched_sha=1f37ee7a488e1f5d8b0a0d1c7ac0dba44dcb50ce3aaaff23cc46f82072baedda

checksum() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  else
    /bin/busybox sha256sum "$1" | awk '{print $1}'
  fi
}

[ "$(checksum "$source_file")" = "$source_sha" ] || {
  echo 'Camera hardware fix: unknown source; left unchanged' >&2
  exit 1
}
[ "$source_file" != "$patched_file" ] || exit 1
cp "$source_file" "$patched_file"
# 0x143704: IR-cut GPIO; 1800a0e3 -> 5100a0e3
printf '\121\000\240\343' | dd of="$patched_file" bs=1 seek=1259268 conv=notrunc 2>/dev/null
# 0x14370c: IR-cut GPIO; 1700a0e3 -> 5000a0e3
printf '\120\000\240\343' | dd of="$patched_file" bs=1 seek=1259276 conv=notrunc 2>/dev/null
# 0x143718: IR-cut GPIO; 1800a0e3 -> 5100a0e3
printf '\121\000\240\343' | dd of="$patched_file" bs=1 seek=1259288 conv=notrunc 2>/dev/null
# 0x143724: IR-cut GPIO; 1700a0e3 -> 5000a0e3
printf '\120\000\240\343' | dd of="$patched_file" bs=1 seek=1259300 conv=notrunc 2>/dev/null
# 0x143730: IR-cut GPIO; 1800a0e3 -> 5100a0e3
printf '\121\000\240\343' | dd of="$patched_file" bs=1 seek=1259312 conv=notrunc 2>/dev/null
# 0x143738: IR-cut GPIO; 1700a0e3 -> 5000a0e3
printf '\120\000\240\343' | dd of="$patched_file" bs=1 seek=1259320 conv=notrunc 2>/dev/null
# 0x1437e8: IR-cut GPIO; 1800a0e3 -> 5100a0e3
printf '\121\000\240\343' | dd of="$patched_file" bs=1 seek=1259496 conv=notrunc 2>/dev/null
# 0x1437f8: IR-cut GPIO; 1800a0e3 -> 5100a0e3
printf '\121\000\240\343' | dd of="$patched_file" bs=1 seek=1259512 conv=notrunc 2>/dev/null
# 0x14380c: IR-cut GPIO; 1700a0e3 -> 5000a0e3
printf '\120\000\240\343' | dd of="$patched_file" bs=1 seek=1259532 conv=notrunc 2>/dev/null
# 0x14381c: IR-cut GPIO; 1700a0e3 -> 5000a0e3
printf '\120\000\240\343' | dd of="$patched_file" bs=1 seek=1259548 conv=notrunc 2>/dev/null
# 0x143794: Skip incompatible speaker GPIO81 initialisation; 5100a0e3e0feffeb -> 0000a0e31080bde8
printf '\000\000\240\343\020\200\275\350' | dd of="$patched_file" bs=1 seek=1259412 conv=notrunc 2>/dev/null
# 0x14383c: Disable incompatible speaker GPIO81 writes; 0010a0e15100a0e3 -> 0000a0e31eff2fe1
printf '\000\000\240\343\036\377\057\341' | dd of="$patched_file" bs=1 seek=1259580 conv=notrunc 2>/dev/null

[ "$(checksum "$patched_file")" = "$patched_sha" ] || {
  rm -f "$patched_file"
  echo 'Camera hardware fix: output verification failed' >&2
  exit 1
}
chmod 755 "$patched_file"
if [ "${1:-}" = '--prepare-only' ]; then
  echo "Camera hardware fix: verified $patched_sha"
  exit 0
fi

# Temporarily present the corrected executable to the unchanged boot script.
# Its usual cp creates the only long-lived RAM copy in /tmp/bin.
mount -o bind "$patched_file" "$source_file" || {
  rm -f "$patched_file"
  exit 1
}
(
  trap '' HUP
  i=0
  while [ "$i" -lt 60 ]; do
    if [ -f "$running_file" ] &&
       { pidof ak_tuya_ipc >/dev/null 2>&1 || pidof Tuya_Main >/dev/null 2>&1; } &&
       [ "$(checksum "$running_file")" = "$patched_sha" ]; then
      if umount "$source_file"; then
        rm -f "$patched_file"
        echo 'Camera hardware fix: running copy verified; temporary copy removed'
        exit 0
      fi
    fi
    i=$((i+1))
    sleep 2
  done
  # Restore the immutable source even if normal camera startup failed.
  if umount "$source_file"; then rm -f "$patched_file"; fi
  echo 'Camera hardware fix: camera startup verification timed out' >&2
) </dev/null >>/tmp/bin/camera-hardware-fix.log 2>&1 &
echo 'Camera hardware fix: prepared for normal startup'
