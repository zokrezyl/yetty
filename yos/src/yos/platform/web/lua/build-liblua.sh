#!/usr/bin/env bash
# Build liblua.wasm — the Lua 5.1 library as a loadable wasm module, so the
# browser can provide nvim (and any lua_*-using guest) the SAME Lua the desktop
# gets from host liblua (epic #33: full library surface in the browser).
#
# Two decisions make this work:
#   1. Compiled as C++ with -fwasm-exceptions. Lua 5.1 uses C++ try/catch for
#      error handling when __cplusplus is defined (luaconf.h), so pcall/error
#      work via NATIVE wasm exceptions — no setjmp/longjmp bridge needed.
#   2. Built against the yos sysroot (the same one nvim/tools use), so struct
#      layouts / libc ABI match the guest; libc calls become env imports the JS
#      host serves (shared linear memory).
#
# luaconf.h's LUA_API is forced to `extern "C"` so the public lua_*/luaL_*
# symbols export unmangled (nvim imports them by their C names).
#
# Output: <this dir>/liblua.wasm. Overrides: YOS_WASM_CLANG (clang++),
# YOS_SYSROOT, LUA_DATA_BASE (where liblua parks its static data in the shared
# memory — must sit above the guest heap; the host reserves it).
set -euo pipefail
cd "$(dirname "$0")"

REPO_ROOT="$(cd ../../../../.. && pwd)"
SYSROOT="${YOS_SYSROOT:-$REPO_ROOT/build-linux/sysroot}"
CXX="${YOS_WASM_CLANGXX:-clang++}"
VERSION=5.1.5
URL="https://www.lua.org/ftp/lua-${VERSION}.tar.gz"
SHA256="2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333"
# liblua's static data lives here in the shared memory (256 MiB in); the host
# reserves [LUA_DATA_BASE, +data) so the guest allocator never overwrites it.
LUA_DATA_BASE="${LUA_DATA_BASE:-268435456}"
# Same parking trick for the SHARED function table: liblua's element segment
# initializes its ~170 indirect functions at this table offset. Without it
# (default base 1) instantiating liblua OVERWRITES the guest's own table
# entries 1..N — nvim then traps "null function or function signature
# mismatch" on its next call_indirect (first hit: the RPC dispatch of the
# embedded server). The host grows the guest table past this base before
# instantiating (lua_bridge.mjs LUA_TABLE_BASE must match).
LUA_TABLE_BASE="${LUA_TABLE_BASE:-65536}"

if [ ! -d "$SYSROOT/usr/include" ]; then
	echo "error: yos sysroot not found at $SYSROOT (run 'make sysroot' / 'make all')" >&2
	exit 1
fi

WORK=".build"
mkdir -p "$WORK"
TARBALL="$WORK/lua-${VERSION}.tar.gz"
if [ ! -f "$TARBALL" ]; then
	echo "fetching lua-${VERSION} …"
	curl -fsSL "$URL" -o "$TARBALL"
fi
echo "${SHA256}  $TARBALL" | sha256sum -c - >/dev/null

SRC="$WORK/src"
if [ ! -f "$SRC/.extracted" ]; then
	rm -rf "$SRC"; mkdir -p "$SRC"
	tar -xzf "$TARBALL" -C "$SRC" --strip-components=1
	touch "$SRC/.extracted"
fi

# Force the public API to extern "C" so exports are unmangled.
sed -i 's/#define LUA_API\t\textern$/#define LUA_API\t\textern "C"/' "$SRC/src/luaconf.h"

CFLAGS=(-target wasm32-unknown-unknown -x c++ -fwasm-exceptions -O2 -nostdlib
        -fno-rtti --sysroot="$SYSROOT" -isystem "$SYSROOT/usr/include"
        -D__i386__=1 -D__yos__=1 -DLUA_USE_C89 -Wno-everything)

cd "$SRC/src"
objs=()
for c in *.c; do
	case "$c" in lua.c | luac.c | print.c) continue ;; esac
	"$CXX" "${CFLAGS[@]}" -c "$c" -o "${c%.c}.o"
	objs+=("${c%.c}.o")
done
# minimal C++ exception ABI (__cxa_*) so Lua's -fwasm-exceptions error handling
# links without libc++abi.
"$CXX" "${CFLAGS[@]}" -c "$OLDPWD/cxa_min.c" -o cxa_min.o
objs+=("cxa_min.o")
# real stdin/stdout/stderr for Lua's io library (fdopen'd via the env bridge
# at ctor time — otherwise the undefined __std*p data symbols resolve to
# address 0 and io.write hits "attempt to use a closed file").
"$CXX" "${CFLAGS[@]}" -c "$OLDPWD/stdio_shim.c" -o stdio_shim.o
objs+=("stdio_shim.o")

# NO --stack-first: the stack must sit AFTER liblua's data (i.e. high, at/above
# LUA_DATA_BASE), NOT at low addresses where it would collide with the guest
# (nvim) stack/data in the shared memory. 16 MiB stack for Lua's parser
# recursion. Everything liblua touches then lives in [LUA_DATA_BASE, +~20 MiB],
# which the host grows into and the guest heap stays below.
wasm-ld --no-entry --export-all --import-memory --import-table --allow-undefined \
	-z stack-size=16777216 "--global-base=$LUA_DATA_BASE" "--table-base=$LUA_TABLE_BASE" \
	-o "$OLDPWD/liblua.wasm" "${objs[@]}"
echo "built liblua.wasm ($(wc -c <"$OLDPWD/liblua.wasm") bytes), data base $LUA_DATA_BASE, table base $LUA_TABLE_BASE"
