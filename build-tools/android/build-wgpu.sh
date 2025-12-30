#!/bin/bash
#
# Download pre-built wgpu-native for Android
# No build dependencies required - just downloads from GitHub releases
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
OUTPUT_DIR="$PROJECT_ROOT/build-android/wgpu-libs"
INCLUDE_DIR="$PROJECT_ROOT/build-android/wgpu-include"

# wgpu-native version with Android pre-builts
WGPU_VERSION="v27.0.4.0"

mkdir -p "$OUTPUT_DIR/arm64-v8a"
mkdir -p "$INCLUDE_DIR"

if [ -f "$OUTPUT_DIR/arm64-v8a/libwgpu_native.so" ] && [ -f "$INCLUDE_DIR/webgpu/webgpu.h" ]; then
    echo "wgpu-native already exists: $OUTPUT_DIR/arm64-v8a/libwgpu_native.so"
    exit 0
fi

echo "Downloading pre-built wgpu-native ${WGPU_VERSION} for Android..."

WGPU_URL="https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_VERSION}/wgpu-android-aarch64-release.zip"
curl -L -o "/tmp/wgpu-android.zip" "$WGPU_URL"

echo "Extracting..."
unzip -o "/tmp/wgpu-android.zip" -d "/tmp/wgpu-android"
cp "/tmp/wgpu-android/lib/libwgpu_native.so" "$OUTPUT_DIR/arm64-v8a/"

# Copy headers from the release (must match library version)
# v27+ has wgpu.h inside webgpu/ directory
cp -r "/tmp/wgpu-android/include/webgpu" "$INCLUDE_DIR/"

rm -rf "/tmp/wgpu-android" "/tmp/wgpu-android.zip"

echo ""
echo "wgpu-native downloaded:"
echo "  Library: $OUTPUT_DIR/arm64-v8a/libwgpu_native.so"
echo "  Headers: $INCLUDE_DIR/"
