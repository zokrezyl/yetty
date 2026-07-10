#!/usr/bin/env bash
# libvterm — terminal emulator state machine (used by neovim's :term and the
# UI layer). Plain C, hand-written Makefile that we override.

set -euo pipefail

NAME=libvterm
VERSION=0.3.3
URL="http://www.leonerd.org.uk/code/libvterm/libvterm-${VERSION}.tar.gz"
SHA256="09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0"

: "${ROOT:?}"; : "${PREFIX:?}"; : "${WASM_CC:?}"; : "${WASM_SYSROOT:?}"
: "${WORK:=$ROOT/build-linux/wasm-pkgs/${NAME}-${VERSION}}"

mkdir -p "$WORK" "$PREFIX/lib" "$PREFIX/include/vterm"

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

CFLAGS_W="$WASM_CFLAGS -I include -I src"

# Find all the C sources libvterm builds into the lib (skip the bin/ tools).
SRCS=()
while IFS= read -r f; do SRCS+=("$f"); done < <(find src -maxdepth 1 -name '*.c' | sort)

OBJS=()
for s in "${SRCS[@]}"; do
    o="${s%.c}.o"
    "$WASM_CC" $CFLAGS_W -c "$s" -o "$o"
    OBJS+=("$o")
done
llvm-ar rcs libvterm.a "${OBJS[@]}"
llvm-ranlib libvterm.a

cp libvterm.a "$PREFIX/lib/"
cp include/vterm.h        "$PREFIX/include/"
cp include/vterm_keycodes.h "$PREFIX/include/"

cat > "$PREFIX/manifest.txt" <<EOF
name=libvterm
version=${VERSION}
prefix=${PREFIX}
libs=-lvterm
cflags=-I${PREFIX}/include
deps=
EOF

echo "[$NAME] installed → $PREFIX"
