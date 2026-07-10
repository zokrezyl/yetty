#!/usr/bin/env bash
# Build the two wasm modules that share one linear memory:
#   guest.wasm   — the app, imports env.write / env.strlen
#   bridge.wasm  — the yos-host bridge, exports them, imports host.write_bytes
#
# Both import their linear memory (env.memory) instead of defining it,
# so the embedder owns ONE Memory and hands it to both. That is what
# makes a guest offset and a bridge offset the same address.
set -euo pipefail
cd "$(dirname "$0")"

CC=${YOS_WASM_CLANG:-clang}
# -fno-builtin so the guest's strlen stays a real env.strlen import
# (otherwise clang folds strlen-of-a-literal and the cross-module call
# vanishes, hiding the pure-wasm-bridge path we want to exercise).
COMMON=(-target wasm32-unknown-unknown -nostdlib -O2 -fno-builtin -Wl,--no-entry -Wl,--import-memory)

# Guest: tiny static footprint (one string near the bottom of memory).
"$CC" "${COMMON[@]}" \
	-Wl,--export=_start \
	-o guest.wasm guest.c

# Bridge: park its (near-empty) data + stack high above the guest's
# footprint so the two modules' static regions never overlap in the
# shared memory. This is the proof-slice stand-in for the real layout
# contract (yos-host private state vs. per-guest memory).
"$CC" "${COMMON[@]}" \
	-Wl,--global-base=8388608 \
	-Wl,-z,stack-size=65536 \
	-o bridge.wasm bridge.c

# Interactive shell stand-in guest (imports env.write; exports
# shell_start + feed_byte that the terminal drives per keystroke).
"$CC" "${COMMON[@]}" \
	-Wl,--export=shell_start -Wl,--export=feed_byte \
	-o shell_demo.wasm shell_demo.c

echo "built guest.wasm ($(wc -c <guest.wasm)) bridge.wasm ($(wc -c <bridge.wasm)) shell_demo.wasm ($(wc -c <shell_demo.wasm))"

# Real-threads demo: shared memory + atomics so worker threads address
# the same linear memory; exported function table so a worker can call a
# thread's start routine by pointer.
"$CC" -target wasm32-unknown-unknown -nostdlib -O2 -matomics -mbulk-memory \
	-Wl,--no-entry -Wl,--import-memory -Wl,--shared-memory \
	-Wl,--max-memory=67108864 -Wl,--export-table \
	-Wl,--export=run_racy -Wl,--export=run_locked -Wl,--export=thread_fn \
	-o threads_demo.wasm threads_demo.c
echo "built threads_demo.wasm ($(wc -c <threads_demo.wasm))"

# Fork-child thread-memory regression guests (issue #23). Shared memory +
# atomics like threads_demo, but they also fork() and create a thread in the
# CHILD — so they run through mt_engine (which calls __main_argc_argv and
# drives fork via asyncify) and must be post-processed with
# `wasm-opt --asyncify`, exactly like the perfstress_mt guest.
#   fork_thread_demo     — one fork child creates a thread (child memory ownership)
#   fork_thread_siblings — two sibling children each thread (memPool-reuse safety)
WASM_OPT=${YOS_WASM_OPT:-wasm-opt}
build_mt_asyncify() {
	local src=$1 out=$2 thread_export=$3
	"$CC" -target wasm32-unknown-unknown -nostdlib -O2 -matomics -mbulk-memory \
		-Wl,--no-entry -Wl,--import-memory -Wl,--shared-memory \
		-Wl,--max-memory=67108864 -Wl,--export-table \
		-Wl,--export=__main_argc_argv -Wl,"--export=$thread_export" \
		-o "$out.raw.wasm" "$src"
	"$WASM_OPT" --asyncify --enable-threads --enable-bulk-memory -O2 \
		"$out.raw.wasm" -o "$out.wasm"
	rm -f "$out.raw.wasm"
	echo "built $out.wasm ($(wc -c <"$out.wasm"))"
}
build_mt_asyncify fork_thread_demo.c fork_thread_demo child_thread
build_mt_asyncify fork_thread_siblings.c fork_thread_siblings sib_thread
