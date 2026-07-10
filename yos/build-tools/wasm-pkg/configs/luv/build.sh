#!/usr/bin/env bash
# luv — Lua bindings to libuv. CMake-based. Depends on lua + libuv (we built
# both already).

set -euo pipefail

NAME=luv
VERSION=1.48.0-2
URL="https://github.com/luvit/luv/archive/refs/tags/${VERSION}.tar.gz"
SHA256="e64cd8a0197449288b37df6ca058120e8d2308fc305f543162b5bf3e92273a05"
DEPS="lua libuv"

# luv's src/luv.c uses lua-compat-5.3 (Lua 5.3 API on top of Lua 5.1). The
# upstream source has the compat headers in deps/lua-compat-5.3/c-api/ but
# only via a git submodule which we don't initialise. Fetch + stage manually.
COMPAT_VERSION=0.13
COMPAT_URL="https://github.com/keplerproject/lua-compat-5.3/archive/refs/tags/v${COMPAT_VERSION}.tar.gz"
COMPAT_SHA256="f5dc30e7b1fda856ee4d392be457642c1f0c259264a9b9bfbcb680302ce88fc2"

: "${ROOT:?}"; : "${PREFIX:?}"; : "${WASM_CC:?}"; : "${WASM_SYSROOT:?}"
: "${DEP_PREFIXES:?}"
: "${WORK:=$ROOT/build-linux/wasm-pkgs/${NAME}-${VERSION}}"

mkdir -p "$WORK" "$PREFIX/lib" "$PREFIX/include"

TARBALL="$WORK/${NAME}-${VERSION}.tar.gz"
[[ -f "$TARBALL" ]] || curl -fsSL "$URL" -o "$TARBALL"
echo "${SHA256}  $TARBALL" | sha256sum -c - > /dev/null

COMPAT_TAR="$WORK/lua-compat-5.3-${COMPAT_VERSION}.tar.gz"
[[ -f "$COMPAT_TAR" ]] || curl -fsSL "$COMPAT_URL" -o "$COMPAT_TAR"
echo "${COMPAT_SHA256}  $COMPAT_TAR" | sha256sum -c - > /dev/null

SRC="$WORK/src"
if [[ ! -f "$SRC/.extracted" ]]; then
    rm -rf "$SRC"; mkdir -p "$SRC"
    tar -xzf "$TARBALL" -C "$SRC" --strip-components=1
    # Stage lua-compat-5.3 where luv's source expects to find it
    # (deps/lua-compat-5.3/c-api/ relative to luv's source root).
    mkdir -p "$SRC/deps/lua-compat-5.3"
    tar -xzf "$COMPAT_TAR" -C "$SRC/deps/lua-compat-5.3" --strip-components=1
    touch "$SRC/.extracted"
fi

# Pull dep prefixes apart: order in DEPS is "lua libuv" so DEP_PREFIXES is
# "<lua-prefix> <libuv-prefix>". Use both for include paths and the libuv
# prefix for the libuv lib search. Keep it simple — manually parse.
LUA_PREFIX=""; LIBUV_PREFIX=""
read LUA_PREFIX LIBUV_PREFIX <<<"$DEP_PREFIXES"

BLD="$WORK/cmake-build"
rm -rf "$BLD"; mkdir -p "$BLD"
cd "$BLD"

cmake "$SRC" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=wasm32 \
    -DCMAKE_C_COMPILER="$WASM_CC" \
    -DCMAKE_C_COMPILER_TARGET=wasm32-unknown-unknown \
    -DCMAKE_AR="$(command -v llvm-ar)" \
    -DCMAKE_RANLIB="$(command -v llvm-ranlib)" \
    -DCMAKE_C_FLAGS="-nostdlib -O2 $WASM_CFLAGS -D_GNU_SOURCE -D__linux__=1 -I$LUA_PREFIX/include -I$LIBUV_PREFIX/include" \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_MODULE=OFF \
    -DBUILD_STATIC_LIBS=ON \
    -DLUA_BUILD_TYPE=System \
    -DWITH_LUA_ENGINE=Lua \
    -DLUA_INCLUDE_DIR="$LUA_PREFIX/include" \
    -DLUA_LIBRARY="$LUA_PREFIX/lib/liblua.a" \
    -DWITH_SHARED_LIBUV=ON \
    -DLIBUV_INCLUDE_DIR="$LIBUV_PREFIX/include" \
    -DLIBUV_LIBRARIES="$LIBUV_PREFIX/lib/libuv.a"

cmake --build . --parallel

# luv produces libluv.a (or similar). Copy whatever .a it built.
mkdir -p "$PREFIX/lib"
cp $(find . -maxdepth 2 -name 'libluv*.a' | head -1) "$PREFIX/lib/libluv.a"
# Headers (luv API): luv.h + luv_*.h
mkdir -p "$PREFIX/include/luv"
cp -r "$SRC/src/"*.h "$PREFIX/include/luv/" 2>/dev/null || true

cat > "$PREFIX/manifest.txt" <<EOF
name=luv
version=${VERSION}
prefix=${PREFIX}
libs=-lluv
cflags=-I${PREFIX}/include
deps=lua libuv
EOF

echo "[$NAME] installed → $PREFIX"
