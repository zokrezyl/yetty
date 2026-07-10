#!/usr/bin/env bash
# tree-sitter — incremental parsing library runtime. We build only the C
# runtime (lib/), not the Rust CLI. Pure C, no external deps.

set -euo pipefail

NAME=tree-sitter
VERSION=0.22.6
URL="https://github.com/tree-sitter/tree-sitter/archive/refs/tags/v${VERSION}.tar.gz"
SHA256="e2b687f74358ab6404730b7fb1a1ced7ddb3780202d37595ecd7b20a8f41861f"

: "${ROOT:?}"; : "${PREFIX:?}"; : "${WASM_CC:?}"; : "${WASM_SYSROOT:?}"
: "${WORK:=$ROOT/build-linux/wasm-pkgs/${NAME}-${VERSION}}"

mkdir -p "$WORK" "$PREFIX/lib" "$PREFIX/include/tree_sitter"

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

# tree-sitter ships a unity build: lib/src/lib.c #includes every .c file in
# the runtime. Compile that one TU and we get the whole library.
CFLAGS_W="--target=wasm32 -nostdlib -O3 $WASM_CFLAGS \
    -I lib/src -I lib/include \
    -fno-strict-aliasing -fvisibility=hidden"

"$WASM_CC" $CFLAGS_W -c lib/src/lib.c -o lib/src/lib.o
llvm-ar rcs libtree-sitter.a lib/src/lib.o
llvm-ranlib libtree-sitter.a

cp libtree-sitter.a "$PREFIX/lib/"
cp lib/include/tree_sitter/*.h "$PREFIX/include/tree_sitter/"

cat > "$PREFIX/manifest.txt" <<EOF
name=tree-sitter
version=${VERSION}
prefix=${PREFIX}
libs=-ltree-sitter
cflags=-I${PREFIX}/include
deps=
EOF

echo "[$NAME] installed → $PREFIX"
