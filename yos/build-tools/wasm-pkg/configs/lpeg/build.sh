#!/usr/bin/env bash
# lpeg — pattern-matching library for Lua. Pure C extension. Static lib so we
# can link it directly into nvim. Needs lua's headers (consumes DEP_PREFIXES).

set -euo pipefail

NAME=lpeg
VERSION=1.1.0
URL="http://www.inf.puc-rio.br/~roberto/lpeg/lpeg-${VERSION}.tar.gz"
SHA256="4b155d67d2246c1ffa7ad7bc466c1ea899bbc40fef0257cc9c03cecbaed4352a"
DEPS=lua

: "${ROOT:?}"; : "${PREFIX:?}"; : "${WASM_CC:?}"; : "${WASM_SYSROOT:?}"
: "${DEP_PREFIXES:?}"   # must contain lua's prefix
: "${WORK:=$ROOT/build-linux/wasm-pkgs/${NAME}-${VERSION}}"

mkdir -p "$WORK" "$PREFIX/lib" "$PREFIX/include"

TARBALL="$WORK/${NAME}-${VERSION}.tar.gz"
[[ -f "$TARBALL" ]] || curl -fsSL "$URL" -o "$TARBALL"
echo "${SHA256}  $TARBALL" | sha256sum -c - > /dev/null

SRC="$WORK/src"
if [[ ! -f "$SRC/.extracted" ]]; then
    rm -rf "$SRC"; mkdir -p "$SRC"
    tar -xzf "$TARBALL" -C "$SRC" --strip-components=1
    touch "$SRC/.extracted"
fi

cd "$SRC"

# DEP_PREFIXES is space-separated; first/only entry here is lua. Pull its
# include path for lua.h, lauxlib.h.
LUA_INC=""
for p in $DEP_PREFIXES; do LUA_INC="-I$p/include $LUA_INC"; done

CFLAGS_W="$WASM_CFLAGS $LUA_INC -fPIC"

SRCS=(lpcap.c lpcode.c lpprint.c lptree.c lpvm.c)
OBJS=()
for s in "${SRCS[@]}"; do
    o="${s%.c}.o"
    "$WASM_CC" $CFLAGS_W -c "$s" -o "$o"
    OBJS+=("$o")
done
llvm-ar rcs liblpeg.a "${OBJS[@]}"
llvm-ranlib liblpeg.a

cp liblpeg.a "$PREFIX/lib/"
cp lpeg.html "$PREFIX/" 2>/dev/null || true   # docs, optional
# lpeg has no public C header — it registers Lua C functions. lua-side users
# `require('lpeg')`. For nvim we just need the symbol `luaopen_lpeg`.

cat > "$PREFIX/manifest.txt" <<EOF
name=lpeg
version=${VERSION}
prefix=${PREFIX}
libs=-llpeg
cflags=
deps=lua
EOF

echo "[$NAME] installed → $PREFIX"
