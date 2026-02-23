#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SDK_DIR="/tmp/anyka_sdk"
IMAGE_NAME="rtsp_builder"

if [ ! -d "$SDK_DIR" ]; then
    echo "ERROR: Anyka SDK not found at $SDK_DIR"
    exit 1
fi

echo "=== Building cross-compile Docker image ==="
docker build -t "$IMAGE_NAME" "$SCRIPT_DIR"

echo "=== Cross-compiling RTSP server ==="
docker run --rm \
    -v "$SCRIPT_DIR":/build \
    -v "$SDK_DIR":/sdk:ro \
    "$IMAGE_NAME" \
    make clean all

echo ""
echo "=== Build complete ==="
file "$SCRIPT_DIR/rtsp_server"
ls -lh "$SCRIPT_DIR/rtsp_server"
