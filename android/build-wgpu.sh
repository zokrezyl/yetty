#!/bin/bash
#
# Download pre-built wgpu-native for Android
# No build dependencies required - just downloads from GitHub releases
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="$SCRIPT_DIR/app/libs"

# wgpu-native version with Android pre-builts
# Note: v0.19.x does not have Android pre-builts
WGPU_VERSION="v22.1.0.5"

mkdir -p "$OUTPUT_DIR/arm64-v8a"

if [ -f "$OUTPUT_DIR/arm64-v8a/libwgpu_native.so" ]; then
    echo "wgpu-native already exists: $OUTPUT_DIR/arm64-v8a/libwgpu_native.so"
    exit 0
fi

echo "Downloading pre-built wgpu-native ${WGPU_VERSION} for Android..."

WGPU_URL="https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_VERSION}/wgpu-android-aarch64-release.zip"
curl -L -o "/tmp/wgpu-android.zip" "$WGPU_URL"

echo "Extracting..."
unzip -o "/tmp/wgpu-android.zip" -d "/tmp/wgpu-android"
cp "/tmp/wgpu-android/lib/libwgpu_native.so" "$OUTPUT_DIR/arm64-v8a/"
rm -rf "/tmp/wgpu-android" "/tmp/wgpu-android.zip"

echo ""
echo "wgpu-native downloaded: $OUTPUT_DIR/arm64-v8a/libwgpu_native.so"
