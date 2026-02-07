#!/bin/bash
# Copy firmware from build directory to web-installer for local testing
# Run this after building with 'idf.py build'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

echo "Copying firmware from $BUILD_DIR to web-installer..."

mkdir -p "$SCRIPT_DIR/firmware"

if [ -f "$BUILD_DIR/bootloader/bootloader.bin" ]; then
    cp "$BUILD_DIR/bootloader/bootloader.bin" "$SCRIPT_DIR/firmware/"
    echo "  - bootloader.bin"
else
    echo "  ! bootloader.bin not found"
fi

if [ -f "$BUILD_DIR/partition_table/partition-table.bin" ]; then
    cp "$BUILD_DIR/partition_table/partition-table.bin" "$SCRIPT_DIR/firmware/"
    echo "  - partition-table.bin"
else
    echo "  ! partition-table.bin not found"
fi

if [ -f "$BUILD_DIR/ota_data_initial.bin" ]; then
    cp "$BUILD_DIR/ota_data_initial.bin" "$SCRIPT_DIR/firmware/"
    echo "  - ota_data_initial.bin"
else
    echo "  ! ota_data_initial.bin not found"
fi

if [ -f "$BUILD_DIR/tled.bin" ]; then
    cp "$BUILD_DIR/tled.bin" "$SCRIPT_DIR/firmware/"
    echo "  - tled.bin"
else
    echo "  ! tled.bin not found"
fi

echo ""
echo "Done! Firmware files are in $SCRIPT_DIR/firmware/"
echo "Run ./serve-local.sh to test the web installer UI"
