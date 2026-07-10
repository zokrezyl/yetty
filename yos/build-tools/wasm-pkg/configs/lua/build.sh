#!/usr/bin/env bash
# lua-5.1.5 — static library for wasm32 against the yos musl sysroot.
#
# Inputs (env, set by tools/wasm-pkg.sh):
#   ROOT          repo root
#   PREFIX        install dir (lib/ + include/ land here)
#   WORK          working dir (default: $ROOT/build-linux/wasm-pkgs/lua-5.1.5)
#   WASM_CC       wasm32 compiler driver (clang)
#   WASM_SYSROOT  musl-wasm32 sysroot (build/musl-sysroot/usr/local/musl)
#
# Output: $PREFIX/lib/liblua.a + lua.h luaconf.h lualib.h lauxlib.h in include/.
# We deliberately don't build the `lua` / `luac` executables — downstream
# consumers (neovim) link against the static lib only.

set -euo pipefail

NAME=lua
VERSION=5.1.5
URL="https://www.lua.org/ftp/lua-${VERSION}.tar.gz"
SHA256="2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333"

: "${ROOT:?ROOT must be set}"
: "${PREFIX:?PREFIX must be set}"
: "${WASM_CC:?WASM_CC must be set}"
: "${WASM_SYSROOT:?WASM_SYSROOT must be set}"
: "${WORK:=$ROOT/build-linux/wasm-pkgs/${NAME}-${VERSION}}"

mkdir -p "$WORK" "$PREFIX/lib" "$PREFIX/include"

# 1) Fetch (cached, sha256-verified)
TARBALL="$WORK/${NAME}-${VERSION}.tar.gz"
if [[ ! -f "$TARBALL" ]]; then
    echo "[$NAME] fetching $URL"
    curl -fsSL "$URL" -o "$TARBALL"
fi
echo "${SHA256}  $TARBALL" | sha256sum -c - > /dev/null

# 2) Extract (idempotent)
SRC="$WORK/src"
if [[ ! -f "$SRC/.extracted" ]]; then
    rm -rf "$SRC"
    mkdir -p "$SRC"
    tar -xzf "$TARBALL" -C "$SRC" --strip-components=1
    touch "$SRC/.extracted"
fi

# 3) Build
# Lua 5.1.5's src/Makefile has a `liblua.a` target that builds the static
# library from CORE_O + LIB_O without touching the `lua` / `luac` binaries
# (those need readline / link-time work we don't want for wasm32).
CFLAGS_W="$WASM_CFLAGS \
     \
    -DLUA_USE_POSIX -DLUA_ANSI \
    -fno-stack-protector"

cd "$SRC/src"
make -j liblua.a \
    CC="$WASM_CC" \
    CFLAGS="$CFLAGS_W" \
    AR="llvm-ar rcu" \
    RANLIB="llvm-ranlib" \
    MYCFLAGS="" MYLDFLAGS="" MYLIBS=""

# 4) Install — just the static lib + public headers
cp liblua.a "$PREFIX/lib/"
cp lua.h luaconf.h lualib.h lauxlib.h "$PREFIX/include/"

# 5) Also build a HOST lua binary so cross-builds (e.g. neovim) can run their
# build-time Lua codegen without needing a system lua install. Same source,
# native compiler.
mkdir -p "$PREFIX/bin"
HOST_BUILD="$WORK/host-build"
rm -rf "$HOST_BUILD"; cp -r "$SRC" "$HOST_BUILD"
cd "$HOST_BUILD/src"
make clean   # drop stale wasm32 .o files inherited from the cross build
# Host build: lua's Makefile defaults to CC=gcc, which doesn't exist
# on darwin under nix; and a bare CC=$CC resolves to `clang`, which is
# shadowed on PATH by the wasm toolchain's *unwrapped* clang symlink
# (no resource-dir → no stdarg.h). The toolchain doesn't ship a `cc`
# binary, so naming `cc` here reliably picks up nix-stdenv's wrapped
# host compiler on both Linux and darwin.
make -j posix CC=cc MYCFLAGS="-DLUA_USE_POSIX -DLUA_ANSI" MYLIBS=""
cp lua "$PREFIX/bin/lua"
cp luac "$PREFIX/bin/luac" 2>/dev/null || true
"$PREFIX/bin/lua" -e 'print("[lua] host interpreter OK: " .. _VERSION)'

# 5) Tiny manifest, easy for downstream `pkg-config`-less consumers to read
cat > "$PREFIX/manifest.txt" <<EOF
name=lua
version=${VERSION}
prefix=${PREFIX}
libs=-llua
cflags=-I${PREFIX}/include
deps=
EOF

echo "[$NAME] installed → $PREFIX"
