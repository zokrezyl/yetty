/*
 * yctl — runtime control + observability surface for yos.
 *
 * When yos is launched with `--yctl-socket PATH`, a dedicated host
 * pthread binds an AF_UNIX SOCK_STREAM listener at PATH and serves
 * msgpack-RPC v2 requests against the live runtime. Both a host-side
 * yctl binary and a wasm32-side one (compiled from the same .c) can
 * connect — the wasm guest reaches the same socket through yos's
 * existing AF_UNIX socket bridges, so introspection from inside the
 * sandbox just works.
 *
 *   Concurrency model:
 *     - All accesses to `struct yos_runtime` / `struct yos_proc` go
 *       through `rt->proc_lock` and `proc->lock`; the daemon snapshots
 *       fields into a stack-local copy under the lock, releases, then
 *       encodes msgpack to the socket. No I/O under a lock.
 *     - Documented-atomic fields (sig_pending, g_yperf_enabled,
 *       g_ytrace default flag) use atomic loads.
 *     - Control verbs (proc.kill, trace.set, perf.set) go through the
 *       existing entry points (kill(2) on the host pid backing the
 *       guest, ytrace_set_all_enabled, yperf_set_enabled) — all
 *       already thread-safe.
 *
 *   Wire format:
 *     Standard msgpack-RPC v2 framing.
 *       request:  [0, msgid, method:str, params:array]
 *       response: [1, msgid, error:nil|str, result:any]
 *     One request per connection accept loop iteration; client closes
 *     when done. No concurrent client streams — one client at a time
 *     (sufficient for an interactive `yctl`).
 *
 *   Method set (v0):
 *     "version"           []                    -> "yctl 0.1"
 *     "proc.list"         []                    -> [{pid, ppid, state, comm}, ...]
 *     "proc.get"          [pid:int]             -> {pid, ppid, pgid, sid, tgid,
 *                                                  state, exit_code, comm,
 *                                                  exe, cwd, cmdline:[str,...]}
 *     "mem.regions"       [pid:int]             -> {memory_size, heap_end,
 *                                                  mmap_top, free:[{addr,len},...]}
 *     "fd.table"          [pid:int]             -> [{wfd, hfd}, ...]
 *     "sig.state"         [pid:int]             -> {mask, pending,
 *                                                   handlers:[uint32,...] (32 entries)}
 *     "proc.kill"         [pid:int, sig:int]    -> nil  (errno on failure)
 *     "trace.set"         [on:bool]             -> nil
 *     "perf.set"          [mode:str]            -> nil  ("on"|"off"|"stop")
 */
#ifndef YOS_YCTL_H
#define YOS_YCTL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yos_runtime;

/* Start the yctl daemon thread. Binds AF_UNIX SOCK_STREAM at sock_path
 * (unlinking any stale file first), spawns a detached pthread that runs
 * the accept loop. Returns 0 on success, -1 on bind/listen/thread error
 * (errno preserved). Safe to call at most once per process. */
int yctl_start(struct yos_runtime *rt, const char *sock_path);

#ifdef __cplusplus
}
#endif

#endif /* YOS_YCTL_H */
