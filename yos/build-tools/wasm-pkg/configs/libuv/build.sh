#!/usr/bin/env bash
# libuv — async I/O library. Heavy POSIX assumptions (epoll, signals, pty,
# threads). This recipe tries the upstream CMake build against the yos musl
# sysroot — first run is expected to surface a stack of missing platform
# bits we'll have to stub or implement on the yos side.
#
# Status: probe / WIP. Errors expected; we iterate.

set -euo pipefail

NAME=libuv
VERSION=1.48.0
URL="https://github.com/libuv/libuv/archive/refs/tags/v${VERSION}.tar.gz"
SHA256="8c253adb0f800926a6cbd1c6576abae0bc8eb86a4f891049b72f9e5b7dc58f33"
DEPS=""

: "${ROOT:?}"; : "${PREFIX:?}"; : "${WASM_CC:?}"; : "${WASM_SYSROOT:?}"
: "${WORK:=$ROOT/build-linux/wasm-pkgs/${NAME}-${VERSION}}"

mkdir -p "$WORK" "$PREFIX/lib" "$PREFIX/include"

TARBALL="$WORK/${NAME}-${VERSION}.tar.gz"
[[ -f "$TARBALL" ]] || curl -fsSL "$URL" -o "$TARBALL"
echo "${SHA256}  $TARBALL" | sha256sum -c - > /dev/null

SRC="$WORK/src"
if [[ ! -f "$SRC/.extracted" ]]; then
    rm -rf "$SRC"; mkdir -p "$SRC"
    tar -xzf "$TARBALL" -C "$SRC" --strip-components=1
    # Patches:
    # async.c — `__asm__("rep; nop")` is x86 PAUSE; clang's wasm
    # assembler rejects it. Replace with a no-op: spin loops still
    # work, just without the CPU yield hint. Safe.
    sed -i 's|__asm__ __volatile__ ("rep; nop" ::: "memory");|(void)0;|' \
        "$SRC/src/unix/async.c"
    touch "$SRC/.extracted"
fi

BLD="$WORK/cmake-build"
rm -rf "$BLD"; mkdir -p "$BLD"
cd "$BLD"

# Force the unix backend; libuv's CMake otherwise tries to detect Win32/macOS.
# Force "Linux" detection so it picks the linux/ backend (closest to what
# musl-wasm32 + yos expose). Errors below this point are the actual port work.
cmake "$SRC" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_SYSTEM_NAME=FreeBSD \
    -DCMAKE_SYSTEM_PROCESSOR=wasm32 \
    -DCMAKE_C_COMPILER="$WASM_CC" \
    -DCMAKE_C_COMPILER_TARGET=wasm32-unknown-unknown \
    -DCMAKE_AR="$(command -v llvm-ar)" \
    -DCMAKE_RANLIB="$(command -v llvm-ranlib)" \
    -DCMAKE_C_FLAGS="$WASM_CFLAGS -D_GNU_SOURCE -D__FreeBSD__=14" \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DLIBUV_BUILD_TESTS=OFF \
    -DLIBUV_BUILD_BENCH=OFF \
    -DLIBUV_BUILD_SHARED=OFF

cmake --build . --parallel

mkdir -p "$PREFIX/lib" "$PREFIX/include"
# Static lib: name varies with cmake options; just take whichever .a is in
# the build dir.
cp libuv.a "$PREFIX/lib/libuv.a"
cp -a "$SRC/include/." "$PREFIX/include/"

cat > "$PREFIX/manifest.txt" <<EOF
name=libuv
version=${VERSION}
prefix=${PREFIX}
libs=-luv
cflags=-I${PREFIX}/include
deps=
EOF

echo "[$NAME] installed → $PREFIX"
