#!/bin/bash
#
# Build wgpu-native for Android
# This script cross-compiles wgpu-native from source for Android targets
#

set -e

# If in nix shell, use nix-provided tools. Otherwise use system PATH.
if [ -z "$IN_NIX_SHELL" ]; then
    export PATH="/usr/local/bin:/usr/bin:/bin:$HOME/.cargo/bin:$PATH"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Configuration
WGPU_NATIVE_VERSION="v0.19.4.1"
WGPU_NATIVE_DIR="$PROJECT_ROOT/build-android/wgpu-native"
OUTPUT_DIR="$SCRIPT_DIR/app/libs"

# Android NDK configuration
ANDROID_API_LEVEL="28"

# Detect NDK location
if [ -n "$NDK_HOME" ]; then
    ANDROID_NDK="$NDK_HOME"
elif [ -n "$ANDROID_NDK_HOME" ]; then
    ANDROID_NDK="$ANDROID_NDK_HOME"
elif [ -n "$ANDROID_HOME" ] && [ -d "$ANDROID_HOME/ndk" ]; then
    ANDROID_NDK=$(ls -d "$ANDROID_HOME/ndk"/*/ 2>/dev/null | sort -V | tail -1)
    ANDROID_NDK="${ANDROID_NDK%/}"
elif [ -d "$HOME/android-sdk/ndk" ]; then
    ANDROID_NDK=$(ls -d "$HOME/android-sdk/ndk"/*/ 2>/dev/null | sort -V | tail -1)
    ANDROID_NDK="${ANDROID_NDK%/}"
fi

if [ -z "$ANDROID_NDK" ] || [ ! -d "$ANDROID_NDK" ]; then
    echo "Error: Android NDK not found!"
    echo "Please set NDK_HOME, ANDROID_NDK_HOME, or install NDK via Android SDK Manager"
    exit 1
fi

echo "Using Android NDK: $ANDROID_NDK"

# Detect host OS
case "$(uname -s)" in
    Linux*)  HOST_OS=linux ;;
    Darwin*) HOST_OS=darwin ;;
    *)       echo "Unsupported host OS"; exit 1 ;;
esac

NDK_TOOLCHAIN="$ANDROID_NDK/toolchains/llvm/prebuilt/${HOST_OS}-x86_64"

# Check Rust
if ! command -v cargo &> /dev/null; then
    echo "Error: cargo not found. Please install Rust."
    exit 1
fi

echo "Using Rust: $(rustc --version)"
echo "Using Cargo: $(cargo --version)"

# Add Android target if rustup is available (skip if in nix shell)
if [ -z "$IN_NIX_SHELL" ] && command -v rustup &> /dev/null; then
    echo "Adding Rust Android target..."
    rustup target add aarch64-linux-android || true
fi

# Clone wgpu-native if not present
if [ ! -d "$WGPU_NATIVE_DIR" ]; then
    echo "Cloning wgpu-native $WGPU_NATIVE_VERSION..."
    git clone --depth 1 --branch "$WGPU_NATIVE_VERSION" --recurse-submodules \
        https://github.com/gfx-rs/wgpu-native.git "$WGPU_NATIVE_DIR"
elif [ ! -f "$WGPU_NATIVE_DIR/ffi/webgpu-headers/webgpu.h" ]; then
    echo "Fetching submodules..."
    cd "$WGPU_NATIVE_DIR" && git submodule update --init --recursive
fi

# Create output directory
mkdir -p "$OUTPUT_DIR/arm64-v8a"

# Set up cross-compilation environment
export CC="${NDK_TOOLCHAIN}/bin/aarch64-linux-android${ANDROID_API_LEVEL}-clang"
export CXX="${NDK_TOOLCHAIN}/bin/aarch64-linux-android${ANDROID_API_LEVEL}-clang++"
export AR="${NDK_TOOLCHAIN}/bin/llvm-ar"
export CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER="${CC}"

# Set up sysroot for bindgen (cross-compilation headers)
ANDROID_SYSROOT="${NDK_TOOLCHAIN}/sysroot"
export BINDGEN_EXTRA_CLANG_ARGS="--sysroot=${ANDROID_SYSROOT} -I${ANDROID_SYSROOT}/usr/include -I${ANDROID_SYSROOT}/usr/include/aarch64-linux-android"

echo "Building wgpu-native for aarch64-linux-android..."
cd "$WGPU_NATIVE_DIR"
cargo build --release --target aarch64-linux-android

# Copy output
cp "target/aarch64-linux-android/release/libwgpu_native.so" "$OUTPUT_DIR/arm64-v8a/"

echo ""
echo "wgpu-native build complete: $OUTPUT_DIR/arm64-v8a/libwgpu_native.so"
