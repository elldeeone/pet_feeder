#!/bin/sh
# Keep the existing RTSP executable running and bound its RAM-backed logs.
# Never touches MQTT, UART, food commands, or the immutable boot-time binary swap.
set -eu

state_dir=${CAMERA_WATCH_STATE_DIR:-/tmp/feeder-camera-watch}
rtsp_log=${CAMERA_WATCH_LOG:-/tmp/rtsp.log}
rtsp_bin=${CAMERA_WATCH_BINARY:-/tmp/bin/ak_tuya_ipc}
max_log_bytes=${CAMERA_WATCH_MAX_LOG_BYTES:-262144}
keep_log_bytes=${CAMERA_WATCH_KEEP_LOG_BYTES:-65536}

trim_log() {
  [ -f "$rtsp_log" ] || return 0
  size=$(wc -c < "$rtsp_log")
  [ "$size" -gt "$max_log_bytes" ] || return 0
  tail -c "$keep_log_bytes" "$rtsp_log" > "$state_dir/log-tail"
  # Preserve the inode for the immutable boot script's append-mode writer.
  cat "$state_dir/log-tail" > "$rtsp_log"
  rm -f "$state_dir/log-tail"
}

# A non-actuating mode for testing log retention on an ordinary host.
if [ "${1:-}" = '--trim-only' ]; then
  mkdir -p "$state_dir"
  trim_log
  exit 0
fi

mkdir "$state_dir" 2>/dev/null || exit 0
trap 'rm -rf "$state_dir"' EXIT
trap 'exit 0' INT TERM
trap '' HUP

# Let start_tuya_ipc.sh perform its normal swap/start first.
sleep 90
while :; do
  trim_log
  if ! pidof ak_tuya_ipc >/dev/null 2>&1 && ! pidof Tuya_Main >/dev/null 2>&1; then
    if [ -x "$rtsp_bin" ]; then
      "$rtsp_bin" >/dev/null 2>&1 </dev/null &
      printf '%s\n' "$!" > "$state_dir/last-started-pid"
      # Bound retries after a persistent camera failure.
      sleep 120
    fi
  fi
  sleep 30
done
