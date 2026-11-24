#!/bin/bash
# Copy firmware from build directory to web-installer for local testing
# Run this after building with 'idf.py build'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# Detect which chip the build in $BUILD_DIR was made for. The ESP32-C6 build goes
# to firmware/ (what manifest.json serves); other targets go to firmware-<target>/
# so they never overwrite the C6 files. (firmware-*/ is listed in .gitignore.)
TARGET=$(grep -o '"target": *"[a-z0-9]*"' "$BUILD_DIR/project_description.json" 2>/dev/null | head -1 | sed 's/.*"\([a-z0-9]*\)"$/\1/')
TARGET="${TARGET:-esp32c6}"
if [ "$TARGET" = "esp32c6" ]; then
    FIRMWARE_DIR="$SCRIPT_DIR/firmware"
else
    FIRMWARE_DIR="$SCRIPT_DIR/firmware-$TARGET"
fi

echo "Copying $TARGET firmware from $BUILD_DIR to $FIRMWARE_DIR..."

mkdir -p "$FIRMWARE_DIR"

if [ -f "$BUILD_DIR/bootloader/bootloader.bin" ]; then
    cp "$BUILD_DIR/bootloader/bootloader.bin" "$FIRMWARE_DIR/"
    echo "  - bootloader.bin"
else
    echo "  ! bootloader.bin not found"
fi

if [ -f "$BUILD_DIR/partition_table/partition-table.bin" ]; then
    cp "$BUILD_DIR/partition_table/partition-table.bin" "$FIRMWARE_DIR/"
    echo "  - partition-table.bin"
else
    echo "  ! partition-table.bin not found"
fi

if [ -f "$BUILD_DIR/ota_data_initial.bin" ]; then
    cp "$BUILD_DIR/ota_data_initial.bin" "$FIRMWARE_DIR/"
    echo "  - ota_data_initial.bin"
else
    echo "  ! ota_data_initial.bin not found"
fi

if [ -f "$BUILD_DIR/tled.bin" ]; then
    cp "$BUILD_DIR/tled.bin" "$FIRMWARE_DIR/"
    echo "  - tled.bin"
else
    echo "  ! tled.bin not found"
fi

echo ""
echo "Done! Firmware files are in $FIRMWARE_DIR/"
echo "Run ./serve-local.sh to test the web installer UI"
