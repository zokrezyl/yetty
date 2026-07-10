#!/usr/bin/env bash
# setup.sh — configure the yos host build for x86_64 iPhone Simulator.
#
#   ./build-tools/ios/setup.sh
#
# Produces build-ios-sim-x86_64/, an out-of-tree meson build that
# cross-compiles the yos host binary against the iPhoneSimulator SDK.
# Wasm guest payloads (.#all/libexec/*.wasm) are platform-independent;
# they're produced by the existing nix umbrella and reused as-is.
#
# Optional deps that aren't in the iPhoneSimulator SDK are disabled
# at meson time:
#   - msgpack-c     → -Dwith_yctl=disabled        (no --yctl-socket)
#   - libpython3.12 → -Dwith_libpython=disabled   (env.Py_* unresolved)
#
# Idempotent: re-running re-emits the patched cross-file (in case SDK
# path moved) and asks meson to reconfigure.

set -euo pipefail

cd "$(dirname "$0")/../.."
REPO="$PWD"

# ── locate the iPhoneSimulator SDK + clang ────────────────────────────
SDK="$(xcrun --sdk iphonesimulator --show-sdk-path)"
CLANG="$(xcrun --sdk iphonesimulator --find clang)"
TOOLCHAIN_BIN="$(dirname "$CLANG")"
TOOLCHAIN_AR="$TOOLCHAIN_BIN/ar"
TOOLCHAIN_STRIP="$TOOLCHAIN_BIN/strip"
TOOLCHAIN_RANLIB="$TOOLCHAIN_BIN/ranlib"

# ── render the cross-file with substitutions ──────────────────────────
BUILD_DIR="$REPO/build-ios-sim-x86_64"
CROSS_TMPL="$REPO/build-tools/ios/cross-x86_64-ios-simulator.txt"
CROSS_OUT="$REPO/build-tools/ios/cross-x86_64-ios-simulator.generated.txt"

sed \
    -e "s|@SDK@|$SDK|g" \
    -e "s|@CLANG@|$CLANG|g" \
    -e "s|@TOOLCHAIN_AR@|$TOOLCHAIN_AR|g" \
    -e "s|@TOOLCHAIN_STRIP@|$TOOLCHAIN_STRIP|g" \
    -e "s|@TOOLCHAIN_RANLIB@|$TOOLCHAIN_RANLIB|g" \
    "$CROSS_TMPL" > "$CROSS_OUT"

echo "[ios] SDK   : $SDK"
echo "[ios] clang : $CLANG"
echo "[ios] cross : $CROSS_OUT"
echo "[ios] build : $BUILD_DIR"

# ── meson setup / reconfigure ─────────────────────────────────────────
MESON_FLAGS=(
    --cross-file "$CROSS_OUT"
    -Dwith_yctl=disabled
    -Dwith_libpython=disabled
    -Dios_unblock=true
)

if [ -d "$BUILD_DIR" ]; then
    meson setup --reconfigure "${MESON_FLAGS[@]}" "$BUILD_DIR"
else
    meson setup "${MESON_FLAGS[@]}" "$BUILD_DIR"
fi

echo
echo "[ios] setup ok. Compile with:"
echo "    meson compile -C $BUILD_DIR src/yos/yos"
echo
echo "[ios] then deploy with:"
echo "    ./build-tools/ios/deploy.sh"
