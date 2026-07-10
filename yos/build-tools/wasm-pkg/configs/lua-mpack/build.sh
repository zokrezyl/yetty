#!/usr/bin/env bash
# lua-mpack — Lua bindings for libmpack (msgpack RPC). Single-TU build:
# lmpack.c #includes "mpack-src/src/mpack.c" (the libmpack amalgamation), so
# the recipe stages libmpack source inside lua-mpack's tree under that path
# and compiles a single .c file into .a. No system mpack lib needed.
#
# Depends on lua (headers).

set -euo pipefail

NAME=lua-mpack
VERSION=1.0.13
URL="https://github.com/libmpack/libmpack-lua/archive/refs/tags/${VERSION}.tar.gz"
SHA256="436a6a3973207403d3f20082002c32e74c25d9149ff2516dc06b0b41514044bf"
DEPS=lua

# Bundled mpack: lua-mpack expects libmpack source at "mpack-src/" inside
# its own tree. Pin the same version libmpack-lua's CI uses.
MPACK_VERSION=1.0.5
MPACK_URL="https://github.com/libmpack/libmpack/archive/refs/tags/${MPACK_VERSION}.tar.gz"
MPACK_SHA256="4ce91395d81ccea97d3ad4cb962f8540d166e59d3e2ddce8a22979b49f108956"

: "${ROOT:?}"; : "${PREFIX:?}"; : "${WASM_CC:?}"; : "${WASM_SYSROOT:?}"
: "${DEP_PREFIXES:?}"
: "${WORK:=$ROOT/build-linux/wasm-pkgs/${NAME}-${VERSION}}"

mkdir -p "$WORK" "$PREFIX/lib" "$PREFIX/include"

# 1) Fetch + verify both tarballs.
TARBALL="$WORK/${NAME}-${VERSION}.tar.gz"
[[ -f "$TARBALL" ]] || curl -fsSL "$URL" -o "$TARBALL"
echo "${SHA256}  $TARBALL" | sha256sum -c - > /dev/null

MPACK_TAR="$WORK/libmpack-${MPACK_VERSION}.tar.gz"
[[ -f "$MPACK_TAR" ]] || curl -fsSL "$MPACK_URL" -o "$MPACK_TAR"
echo "${MPACK_SHA256}  $MPACK_TAR" | sha256sum -c - > /dev/null

# 2) Extract; stage libmpack into mpack-src/ inside lua-mpack source tree.
SRC="$WORK/src"
if [[ ! -f "$SRC/.extracted" ]]; then
    rm -rf "$SRC"; mkdir -p "$SRC"
    tar -xzf "$TARBALL"  -C "$SRC" --strip-components=1
    mkdir -p "$SRC/mpack-src"
    tar -xzf "$MPACK_TAR" -C "$SRC/mpack-src" --strip-components=1
    touch "$SRC/.extracted"
fi

# 3) Compile.
LUA_INC=""
for p in $DEP_PREFIXES; do LUA_INC="-I$p/include $LUA_INC"; done

CFLAGS_W="$WASM_CFLAGS $LUA_INC \
    -DLUA_C89_NUMBERS -fPIC"

cd "$SRC"
"$WASM_CC" $CFLAGS_W -c lmpack.c -o lmpack.o
llvm-ar rcs liblua-mpack.a lmpack.o
llvm-ranlib liblua-mpack.a

cp liblua-mpack.a "$PREFIX/lib/"

cat > "$PREFIX/manifest.txt" <<EOF
name=lua-mpack
version=${VERSION}
prefix=${PREFIX}
libs=-llua-mpack
cflags=
deps=lua
EOF

echo "[$NAME] installed → $PREFIX"
