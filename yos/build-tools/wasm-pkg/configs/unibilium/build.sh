#!/usr/bin/env bash
# unibilium — terminfo parser (used by neovim for terminal capability lookup).
# Pure C, plain Makefile, builds straightforwardly for wasm32.

set -euo pipefail

NAME=unibilium
VERSION=2.0.0
URL="https://github.com/mauke/unibilium/archive/refs/tags/v${VERSION}.tar.gz"
SHA256="78997d38d4c8177c60d3d0c1aa8c53fd0806eb21825b7b335b1768d7116bc1c1"

: "${ROOT:?}"
: "${PREFIX:?}"
: "${WASM_CC:?}"
: "${WASM_SYSROOT:?}"
: "${WORK:=$ROOT/build-linux/wasm-pkgs/${NAME}-${VERSION}}"

mkdir -p "$WORK" "$PREFIX/lib" "$PREFIX/include"

TARBALL="$WORK/${NAME}-${VERSION}.tar.gz"
if [[ ! -f "$TARBALL" ]]; then
    echo "[$NAME] fetching $URL"
    curl -fsSL "$URL" -o "$TARBALL"
fi
echo "${SHA256}  $TARBALL" | sha256sum -c - > /dev/null

SRC="$WORK/src"
if [[ ! -f "$SRC/.extracted" ]]; then
    rm -rf "$SRC"; mkdir -p "$SRC"
    tar -xzf "$TARBALL" -C "$SRC" --strip-components=1
    touch "$SRC/.extracted"
fi

# Build static library only. Upstream Makefile builds shared lib + utility
# binaries; we override to compile the .c sources into a .a archive and skip
# the shared-lib / executable steps (no wasm-ld needed at this stage).
cd "$SRC"

CFLAGS_W="$WASM_CFLAGS -fPIC"
# TERMINFO_DIRS is normally set by the upstream Makefile. Pass the C string
# literal as a separate -D so shell quoting doesn't collapse the inner double
# quotes into a (broken) char constant.
TERMINFO_FLAG=-DTERMINFO_DIRS=\"/etc/terminfo:/lib/terminfo:/usr/share/terminfo\"

# unibilium has only a handful of C sources. Compile them all into a static
# archive directly — bypasses the upstream Makefile's libtool/shared-lib
# machinery.
SRCS=(unibilium.c uniutil.c uninames.c)
OBJS=()
for s in "${SRCS[@]}"; do
    o="${s%.c}.o"
    "$WASM_CC" $CFLAGS_W "$TERMINFO_FLAG" -c "$s" -o "$o"
    OBJS+=("$o")
done
llvm-ar rcs libunibilium.a "${OBJS[@]}"
llvm-ranlib libunibilium.a

# Headers + lib install
cp libunibilium.a "$PREFIX/lib/"
cp unibilium.h "$PREFIX/include/"

cat > "$PREFIX/manifest.txt" <<EOF
name=unibilium
version=${VERSION}
prefix=${PREFIX}
libs=-lunibilium
cflags=-I${PREFIX}/include
deps=
EOF

echo "[$NAME] installed → $PREFIX"
