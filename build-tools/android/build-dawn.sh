#!/bin/bash
#
# Download pre-built dependencies for Android (Dawn backend):
# - Dawn WebGPU (Google's C++ implementation)
# - toybox (BSD-licensed shell utilities)
#
# Supports: arm64-v8a (default) and x86_64 (for emulator)
# Set ANDROID_ABI=x86_64 for emulator builds
#
# Dawn release URL: https://github.com/google/dawn/releases
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"

# Use ANDROID_BUILD_DIR env var or default
BUILD_DIR="${ANDROID_BUILD_DIR:-$PROJECT_ROOT/build-android-dawn}"
OUTPUT_DIR="$BUILD_DIR/dawn-libs"
INCLUDE_DIR="$BUILD_DIR/dawn-include"
ASSETS_DIR="$BUILD_DIR/assets"

# Architecture (default: arm64-v8a, or x86_64 for emulator)
ANDROID_ABI="${ANDROID_ABI:-arm64-v8a}"

# Versions - must match Dawn.cmake
DAWN_VERSION="20260214.164635"
DAWN_COMMIT="1a3afc99a7ef7dacaab73b71d44575c4f1bf2dd7"

mkdir -p "$OUTPUT_DIR/$ANDROID_ABI"
mkdir -p "$INCLUDE_DIR"
mkdir -p "$ASSETS_DIR"

#-----------------------------------------------------------------------------
# Dawn WebGPU
#-----------------------------------------------------------------------------
if [ -f "$OUTPUT_DIR/$ANDROID_ABI/libwebgpu_dawn.a" ] && [ -f "$INCLUDE_DIR/webgpu/webgpu.h" ]; then
    echo "Dawn already exists: $OUTPUT_DIR/$ANDROID_ABI/libwebgpu_dawn.a"
else
    echo "Downloading pre-built Dawn ${DAWN_VERSION} for Android $ANDROID_ABI..."

    # Android build uses different naming: dawn-android-{commit}.tar.gz
    DAWN_URL="https://github.com/google/dawn/releases/download/v${DAWN_VERSION}/dawn-android-${DAWN_COMMIT}.tar.gz"
    HEADERS_URL="https://github.com/google/dawn/releases/download/v${DAWN_VERSION}/dawn-headers-${DAWN_COMMIT}.tar.gz"

    echo "Downloading: $DAWN_URL"
    curl -fsSL -o "/tmp/dawn-android.tar.gz" "$DAWN_URL"

    echo "Downloading headers: $HEADERS_URL"
    curl -fsSL -o "/tmp/dawn-headers.tar.gz" "$HEADERS_URL"

    echo "Extracting library..."
    mkdir -p /tmp/dawn-android
    tar -xzf /tmp/dawn-android.tar.gz -C /tmp/dawn-android

    echo "Extracting headers..."
    mkdir -p /tmp/dawn-headers
    tar -xzf /tmp/dawn-headers.tar.gz -C /tmp/dawn-headers

    # Find and copy the library (Dawn Android provides static .a library)
    DAWN_EXTRACTED="/tmp/dawn-android/dawn-android-${DAWN_COMMIT}"
    FOUND_LIB=""

    # Try various paths where Dawn library might be
    for TRY_PATH in \
        "$DAWN_EXTRACTED/$ANDROID_ABI/libwebgpu_dawn.a" \
        "/tmp/dawn-android/$ANDROID_ABI/libwebgpu_dawn.a" \
        "$DAWN_EXTRACTED/libwebgpu_dawn.a" \
        "/tmp/dawn-android/libwebgpu_dawn.a"; do
        if [ -f "$TRY_PATH" ]; then
            cp "$TRY_PATH" "$OUTPUT_DIR/$ANDROID_ABI/"
            FOUND_LIB="$TRY_PATH"
            echo "Copied static library from: $TRY_PATH"
            break
        fi
    done

    if [ -z "$FOUND_LIB" ]; then
        echo "Looking for Dawn library (target: $ANDROID_ABI)..."
        find /tmp/dawn-android -name "*.so" -o -name "*.a" 2>/dev/null || true
        echo ""
        echo "ERROR: Could not find Dawn library for $ANDROID_ABI in extracted archive"
        echo "Dawn may not provide pre-built binaries for $ANDROID_ABI architecture."
        echo ""
        echo "For x86_64 emulator testing, consider using wgpu backend instead:"
        echo "  make build-android_x86_64-wgpu-release"
        exit 1
    fi

    # Copy headers (structure: dawn-headers/dawn-headers/include/{webgpu,dawn}/)
    # Need both webgpu/ and dawn/ directories since webgpu.h includes dawn/webgpu.h
    if [ -d "/tmp/dawn-headers/dawn-headers/include" ]; then
        cp -r "/tmp/dawn-headers/dawn-headers/include/"* "$INCLUDE_DIR/"
    elif [ -d "/tmp/dawn-headers/dawn-headers-${DAWN_COMMIT}/include" ]; then
        cp -r "/tmp/dawn-headers/dawn-headers-${DAWN_COMMIT}/include/"* "$INCLUDE_DIR/"
    elif [ -d "/tmp/dawn-headers/include" ]; then
        cp -r "/tmp/dawn-headers/include/"* "$INCLUDE_DIR/"
    else
        echo "Looking for headers..."
        find /tmp/dawn-headers -name "webgpu.h" 2>/dev/null
        echo "ERROR: Could not find Dawn headers"
        exit 1
    fi

    rm -rf "/tmp/dawn-android" "/tmp/dawn-android.tar.gz" "/tmp/dawn-headers" "/tmp/dawn-headers.tar.gz"

    echo "Dawn downloaded to: $OUTPUT_DIR/$ANDROID_ABI/"
fi

#-----------------------------------------------------------------------------
# Copy assets
#-----------------------------------------------------------------------------
if [ -d "$PROJECT_ROOT/assets" ]; then
    cp -f "$PROJECT_ROOT/assets"/*.ttf "$PROJECT_ROOT/assets"/*.png "$PROJECT_ROOT/assets"/*.json "$ASSETS_DIR/" 2>/dev/null || true
    echo "Assets copied to: $ASSETS_DIR/"
fi

#-----------------------------------------------------------------------------
# Copy shaders (must be done BEFORE gradle, not as CMake post-build)
#-----------------------------------------------------------------------------
SHADERS_SRC="$PROJECT_ROOT/src/yetty/shaders"
SHADERS_DST="$ASSETS_DIR/shaders"
if [ -d "$SHADERS_SRC" ]; then
    echo "Copying shaders..."
    mkdir -p "$SHADERS_DST"
    cp -r "$SHADERS_SRC"/* "$SHADERS_DST/"
    echo "Shaders copied to: $SHADERS_DST/"
fi

echo ""
echo "Android Dawn dependencies ready ($ANDROID_ABI):"
echo "  Dawn:    $OUTPUT_DIR/$ANDROID_ABI/"
echo "  headers: $INCLUDE_DIR/"
echo "  assets:  $ASSETS_DIR/"
