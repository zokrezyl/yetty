/* Standard-stream FILE pointers for liblua.wasm (epic #33).
 *
 * FreeBSD's <stdio.h> maps stdin/stdout/stderr onto the extern data symbols
 * __stdinp/__stdoutp/__stderrp. Those live in the GUEST's libc data, which a
 * companion wasm library cannot reach — wasm-ld --allow-undefined resolves
 * undefined DATA symbols to address 0 — so Lua's io library was building
 * io.stdout from the bytes at guest address 0, and every io.write() from
 * nvim's runtime (vim/_defaults.lua) died with "attempt to use a closed
 * file".
 *
 * Define them as the host FILE layer's std-stream SENTINELS (1/2/3 → fd
 * 0/1/2, the same convention the yos-sysroot zsh build uses; see the FILE*
 * layer in yos_proc.mjs). The sentinel resolves to the CURRENT fd 0/1/2 at
 * every operation — NOT a descriptor captured at liblua load. That dynamic
 * binding is load-bearing for nvim: the embedded server wires its RPC channel
 * on stdin/stdout, then dup2()s stderr over fds 0/1 so later writes go to the
 * terminal. A FILE snapshotted before that remap (e.g. via fdopen, which dups)
 * would keep Lua's io.stdout aimed at the msgpack socket, and _defaults.lua's
 * terminal queries (OSC 11 / XTGETTCAP / DECRQSS) would corrupt the RPC
 * stream — the client then dies with "failed to decode msgpack".
 *
 * Compiled as C++ alongside the Lua sources (same CFLAGS as cxa_min.c).
 */

typedef struct __sFILE FILE;

extern "C" {
FILE *__stdinp = (FILE *)1;
FILE *__stdoutp = (FILE *)2;
FILE *__stderrp = (FILE *)3;
}
