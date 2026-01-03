#!/bin/bash
# Download pre-built Toybox for Android ARM64
# Toybox binaries from https://landley.net/toybox/bin/
set -e

# Go to project root (script is in build-tools/android/)
cd "$(dirname "$0")/../.."

TOYBOX_URL="https://landley.net/toybox/bin"
OUTPUT_DIR="build-android/assets"

# Map Android ABI to toybox binary name
case "${ANDROID_ABI:-arm64-v8a}" in
    arm64-v8a)
        TOYBOX_BINARY="toybox-aarch64"
        ;;
    armeabi-v7a)
        TOYBOX_BINARY="toybox-armv7l"
        ;;
    x86_64)
        TOYBOX_BINARY="toybox-x86_64"
        ;;
    x86)
        TOYBOX_BINARY="toybox-i686"
        ;;
    *)
        echo "ERROR: Unsupported ANDROID_ABI: ${ANDROID_ABI}"
        echo "Supported: arm64-v8a, armeabi-v7a, x86_64, x86"
        exit 1
        ;;
esac

mkdir -p "$OUTPUT_DIR"

# Download if not already present (or if forced)
TOYBOX_PATH="$OUTPUT_DIR/toybox"
if [ ! -f "$TOYBOX_PATH" ] || [ "${FORCE_DOWNLOAD:-}" = "1" ]; then
    echo "Downloading Toybox ($TOYBOX_BINARY) for Android..."
    curl -fsSL "${TOYBOX_URL}/${TOYBOX_BINARY}" -o "$TOYBOX_PATH"
    chmod +x "$TOYBOX_PATH"
    echo "Toybox downloaded: $TOYBOX_PATH"
else
    echo "Toybox already exists: $TOYBOX_PATH"
fi

# Show version info
echo "Toybox binary info:"
file "$TOYBOX_PATH" 2>/dev/null || true
ls -la "$TOYBOX_PATH"
