#!/usr/bin/env bash
# Build the general yos-host runner (epic #33, issue #37).
#
#   yos-host.mjs (+ .wasm) — the real generated bridge + impl/vfs + wasm3 +
#   yos_host_run.c, compiled to a browser wasm module. Runs an ARBITRARY guest
#   tool wasm the JS harness writes to /guest.wasm in MEMFS, so the browser and
#   desktop exercise the SAME tool artifacts.
#
# Shares the object cache (obj/) with build-phase1b.sh — the yos C only needs
# compiling once. Toolchain rules as in build-phase0.sh. Overrides:
# YOS_WASM_CLANG (unused here), EMCC, YOS_BUILD (native build dir for codegen).
set -euo pipefail
cd "$(dirname "$0")"

REPO_ROOT="$(cd ../../../../.. && pwd)"
WASM3_SRC="$REPO_ROOT/src/wasm3/source"
YOS_SRC="$REPO_ROOT/src/yos"
YOS_BUILD=${YOS_BUILD:-build-linux}
CODEGEN="$REPO_ROOT/$YOS_BUILD/src/yos/codegen"

EMCC=${EMCC:-emcc}
OBJCOPY=${OBJCOPY:-llvm-objcopy}

if [ ! -f "$CODEGEN/yos_bridge.c" ]; then
	echo "error: generated bridge not found at $CODEGEN/yos_bridge.c" >&2
	echo "       run the desktop codegen first ('make codegen'), or set YOS_BUILD." >&2
	exit 1
fi

OBJ=obj
mkdir -p "$OBJ"

INC=(-I "$YOS_SRC" -I "$REPO_ROOT/src" -I "$REPO_ROOT/include"
     -I "$YOS_SRC/generated" -I "$YOS_SRC/vfs" -I "$YOS_SRC/platform/posix"
     -I "$WASM3_SRC" -I "$CODEGEN" -I .)
CFLAGS=(-O2 "${INC[@]}" -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
        -Wno-implicit-function-declaration -Wno-error=implicit-function-declaration
        -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
        -Wno-implicit-fallthrough -ferror-limit=0)

# The generated bridge emits default impls for six wide-char stdio functions
# that impl/io/file.c also defines (correctly, via the FILE* table). wasm LLD
# has no --allow-multiple-definition and wasm objcopy can't weaken symbols, so
# rename the bridge's copies at compile time with -D macros: this renames both
# the definition and the bridge's own calls consistently (no source edit), and
# file.c's real definitions become the only ones under the canonical names.
# (The bridge's m3w_getwc etc. now call its renamed default; no coreutils tool
# uses wide-char stdio, so this is invisible. Proper fix is codegen not emitting
# defaults for hand-written functions.)
BRIDGE_DEFS=()
for sym in yos_getwc yos_getwchar yos_fgetwc yos_fputwc yos_putwc yos_putwchar; do
	BRIDGE_DEFS+=("-D${sym}=yosbridgedef_${sym#yos_}")
done

# Same yos C surface as build-phase1b.sh (impl/vfs first, then generated
# bridge), entry point yos_host_run.c. Unlike the phase1b slice this DOES
# include impl/io/file.c (the FILE* table — most tools output via printf/puts
# → fwrite → FILE*); the collision with the bridge's six default wide-char
# impls is resolved by weakening those symbols in the bridge object below.
SOURCES=(
	"$YOS_SRC/impl/io/io.c"          "$YOS_SRC/impl/io/dir.c"
	"$YOS_SRC/impl/io/file.c"        "$YOS_SRC/impl/io/fifo.c"
	"$YOS_SRC/impl/io/pty.c"         "$YOS_SRC/impl/io/cv_stat.c"
	"$YOS_SRC/impl/mem/mem.c"        "$YOS_SRC/impl/mem/alloc.c"
	"$YOS_SRC/impl/libc/posix.c"     "$YOS_SRC/impl/libc/printf.c"
	"$YOS_SRC/impl/libc/scanf.c"     "$YOS_SRC/impl/libc/env.c"
	"$YOS_SRC/impl/libc/getopt.c"    "$YOS_SRC/impl/libc/tz.c"
	"$YOS_SRC/impl/libc/libc_globals_link.c" "$YOS_SRC/impl/libc/aliases.c"
	"$YOS_SRC/impl/libc/callback.c"  "$YOS_SRC/impl/libc/cv_trace.c"
	"$YOS_SRC/impl/libc/f128.c"      "$YOS_SRC/impl/libc/i128.c"
	"$YOS_SRC/impl/libc/freebsd_userland.c" "$YOS_SRC/impl/libc/fmtcheck_freebsd.c"
	"$YOS_SRC/impl/libc/kqueue.c"    "$YOS_SRC/impl/libc/mbwc.c"
	"$YOS_SRC/impl/libc/pwd.c"       "$YOS_SRC/impl/libc/random.c"
	"$YOS_SRC/impl/libc/regex.c"     "$YOS_SRC/impl/libc/strto.c"
	"$YOS_SRC/impl/libc/sysctl.c"    "$YOS_SRC/impl/libc/syslog_extras.c"
	"$YOS_SRC/impl/proc/proc.c"      "$YOS_SRC/impl/proc/pthread.c"
	"$YOS_SRC/impl/proc/sig.c"       "$YOS_SRC/impl/proc/signal.c"
	"$YOS_SRC/vfs/file.c"            "$YOS_SRC/vfs/mount.c"
	"$YOS_SRC/vfs/procfs.c"
	"$YOS_SRC/ytrace/ytrace.c"       "$YOS_SRC/yperf/yperf.c"
	"$YOS_SRC/yplatform/time/default.c" "$YOS_SRC/yplatform/thread/default.c"
	"$YOS_SRC/yplatform/term/default.c"
	"$WASM3_SRC/m3_api_libc.c" "$WASM3_SRC/m3_atomic.c" "$WASM3_SRC/m3_bind.c"
	"$WASM3_SRC/m3_code.c" "$WASM3_SRC/m3_compile.c" "$WASM3_SRC/m3_core.c"
	"$WASM3_SRC/m3_emit.c" "$WASM3_SRC/m3_env.c" "$WASM3_SRC/m3_exec.c"
	"$WASM3_SRC/m3_function.c" "$WASM3_SRC/m3_info.c" "$WASM3_SRC/m3_module.c"
	"$WASM3_SRC/m3_optimize.c" "$WASM3_SRC/m3_parse.c"
	"$CODEGEN/yos_bridge.c"          "$CODEGEN/yos_remap.c"
	"$CODEGEN/yos_struct_convert.c"
	"$YOS_SRC/generated/struct_convert.c"
	"phase1b_bridge_support.c"
	"yos_host_run.c"
)

objs=()
compiled=0
for src in "${SOURCES[@]}"; do
	tag=$(echo "$src" | sed 's|.*/src/||; s|[/.]|_|g')
	obj="$OBJ/$tag.o"
	objs+=("$obj")
	# The bridge object needs the wide-char rename defines; a differently-named
	# object keeps it distinct from any phase1b-cached bridge object.
	extra=()
	if [ "$src" = "$CODEGEN/yos_bridge.c" ]; then
		obj="$OBJ/${tag}_renamed.o"
		objs[${#objs[@]}-1]="$obj"
		extra=("${BRIDGE_DEFS[@]}")
	fi
	if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
		"$EMCC" -c "${CFLAGS[@]}" "${extra[@]}" "$src" -o "$obj"
		compiled=$((compiled + 1))
	fi
done
echo "compiled $compiled/${#SOURCES[@]} objects (rest cached in $OBJ/)"

# FORCE_FILESYSTEM + FS export so the JS harness can write /guest.wasm and any
# input files into MEMFS before main runs. callMain export lets the harness
# re-run with different argv without re-instantiating (not required, but handy).
"$EMCC" -O2 "${objs[@]}" \
	-sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node \
	-sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1 -sERROR_ON_UNDEFINED_SYMBOLS=0 \
	-sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=FS,callMain,ENV \
	-o yos-host.mjs
echo "built yos-host.mjs ($(wc -c <yos-host.mjs) bytes) + yos-host.wasm ($(wc -c <yos-host.wasm) bytes)"
