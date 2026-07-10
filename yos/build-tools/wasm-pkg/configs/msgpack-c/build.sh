#!/usr/bin/env bash
# msgpack-c — C MessagePack library, static lib for wasm32 against
# the yos libc surface (FreeBSD-shape headers, env imports resolved
# at runtime by yos).
#
# Inputs (env, set by tools/wasm-pkg.sh):
#   ROOT, PREFIX, WORK, WASM_CC, WASM_SYSROOT, WASM_CFLAGS, WASM_LDFLAGS
#
# Output: $PREFIX/lib/libmsgpack-c.a + headers under $PREFIX/include/.
set -euo pipefail

NAME=msgpack-c
VERSION=6.0.0
URL="https://github.com/msgpack/msgpack-c/releases/download/c-${VERSION}/msgpack-c-${VERSION}.tar.gz"
SHA256="3654f5e2c652dc52e0a993e270bb57d5702b262703f03771c152bba51602aeba"

: "${ROOT:?}"
: "${PREFIX:?}"
: "${WASM_CC:?}"
: "${WASM_SYSROOT:?}"
: "${WORK:=$ROOT/build-linux/wasm-pkgs/${NAME}-${VERSION}}"

mkdir -p "$WORK" "$PREFIX/lib" "$PREFIX/include"

# 1) Fetch (cached, sha256-pinned).
TARBALL="$WORK/${NAME}-${VERSION}.tar.gz"
if [[ ! -f "$TARBALL" ]]; then
    echo "[$NAME] fetching $URL"
    curl -fsSL "$URL" -o "$TARBALL"
fi
echo "${SHA256}  $TARBALL" | sha256sum -c - > /dev/null

# 2) Extract.
SRC="$WORK/src"
if [[ ! -f "$SRC/.extracted" ]]; then
    rm -rf "$SRC"
    mkdir -p "$SRC"
    tar -xzf "$TARBALL" -C "$SRC" --strip-components=1
    touch "$SRC/.extracted"
fi

# 3) Configure + build via upstream CMake.
BLD="$WORK/cmake-build"
rm -rf "$BLD"
mkdir -p "$BLD"
cd "$BLD"

cmake "$SRC" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_SYSTEM_NAME=Generic \
    -DCMAKE_SYSTEM_PROCESSOR=wasm32 \
    -DCMAKE_C_COMPILER="$WASM_CC" \
    -DCMAKE_C_COMPILER_TARGET=wasm32-unknown-unknown \
    -DCMAKE_AR="$(command -v llvm-ar)" \
    -DCMAKE_RANLIB="$(command -v llvm-ranlib)" \
    -DCMAKE_C_FLAGS="$WASM_CFLAGS" \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DMSGPACK_BUILD_TESTS=OFF \
    -DMSGPACK_BUILD_EXAMPLES=OFF \
    -DMSGPACK_ENABLE_CXX=OFF \
    -DMSGPACK_GEN_COVERAGE=OFF

cmake --build . --parallel
cmake --install .

# 4) Manifest.
cat > "$PREFIX/manifest.txt" <<EOF
name=msgpack-c
version=${VERSION}
prefix=${PREFIX}
libs=-lmsgpack-c
cflags=-I${PREFIX}/include
deps=
EOF

echo "[$NAME] installed -> $PREFIX"
