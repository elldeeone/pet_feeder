#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE_NAME="mqtt_feeder_builder"

echo "=== Building cross-compile Docker image ==="
docker build -t "$IMAGE_NAME" "$SCRIPT_DIR"

echo "=== Cross-compiling mqtt_feeder ==="
docker run --rm -v "$SCRIPT_DIR":/build "$IMAGE_NAME" make clean all

echo ""
echo "=== Build complete ==="
file "$SCRIPT_DIR/mqtt_feeder"
ls -lh "$SCRIPT_DIR/mqtt_feeder"
