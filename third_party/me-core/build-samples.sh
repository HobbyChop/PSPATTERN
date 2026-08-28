#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAMPLES_DIR="$SCRIPT_DIR/samples"

USBHOSTFS=""
for f in "$SCRIPT_DIR"/usbhostfs_pc*; do
  if [ -f "$f" ]; then
    USBHOSTFS="$f"
    break
  fi
done

for SAMPLE_DIR in "$SAMPLES_DIR"/*/; do
  SAMPLE=$(basename "$SAMPLE_DIR")
  BUILD_DIR="$SAMPLE_DIR/build"

  echo ">>> $SAMPLE"

  if [ -d "$BUILD_DIR" ]; then
    echo "  Removing $BUILD_DIR"
    rm -rf "$BUILD_DIR"
  fi

  mkdir -p "$BUILD_DIR"

  if [ -n "$USBHOSTFS" ]; then
    echo "  Copying $(basename "$USBHOSTFS") to $BUILD_DIR"
    cp "$USBHOSTFS" "$BUILD_DIR/"
  fi

  cd "$BUILD_DIR" || continue
  cmake .. && make
  cd "$SCRIPT_DIR"

  echo ""
done

