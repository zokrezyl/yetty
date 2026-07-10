// Host-side implementation of `env.pthread_*` imports for wasm3mt.
//
// Construct a `yos_pthread_host` in the runner, then have it link its 4 imports
// onto every freshly loaded module (master + each per-thread sibling).
//
// All threading state lives in the host: a small table of host_thread structs
// keyed by a u32 tid that is also the stack-index handed to the guest's
// `__wasm3mt_thread_entry`. The shared mutex cell in wasm linear memory is
// driven by a Drepper-style futex through wasm3's atomic.wait/notify queue.

#ifndef WASM3MT_PTHREAD_HOST_H
#define WASM3MT_PTHREAD_HOST_H

#include <stdint.h>
#include "wasm3.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct yos_pthread_host yos_pthread_host;

// Call this from the runner *after* the master module is parsed and loaded.
// `wasm` and `wasm_len` must remain valid for the host's lifetime — they're
// re-parsed on every per-thread sibling runtime spawn.
//
// tls_pool_base / tls_arena_size define the per-process TLS arena pool: a
// contiguous region in wasm linear memory carved into MAX_THREADS slots, one
// per thread. Pass tls_arena_size=0 to disable (each thread's TLS arena is 0).
yos_pthread_host * yos_pthread_host_create  (IM3Environment  env,
                                     IM3Runtime      master,
                                     const uint8_t * wasm,
                                     uint32_t        wasm_len,
                                     uint32_t        per_thread_stack_bytes,
                                     uint32_t        tls_pool_base,
                                     uint32_t        tls_arena_size);

void           yos_pthread_host_destroy (yos_pthread_host * h);

// Link the imports onto a freshly loaded module. Call *after* m3_LoadModule.
M3Result       yos_pthread_host_link    (yos_pthread_host * h, IM3Module mod);

/* Look up the TLS arena base (TP) for a given tid. Returns 0 for unknown
 * tids or when the pool is disabled. */
uint32_t       yos_pthread_host_get_tls_arena (yos_pthread_host * h, uint32_t tid);

/* === clone(2) thread-spawn entry — internal C, NOT a wasm import. ===
 *
 * yos_proc_clone (the SYS_clone handler in yos-proc.c) calls this when
 * CLONE_VM is set. It is the *single* substrate for guest thread creation:
 * eventually the L1 pthread_create import will route through it too, then
 * disappear once musl wasm32 ships standard pthread_create on top of
 * __clone+futex.
 *
 * Args:
 *   fn_idx       wasm function-table index for the thread entry
 *   arg          single u32 passed to fn (musl's __pthread_start packs all
 *                state into one pointer)
 *   ctid_addr    CLONE_CHILD_CLEARTID address (wasm32 offset). On thread
 *                exit the worker atomically writes 0 here and issues
 *                SYS_futex(FUTEX_WAKE_PRIVATE, 1) — that's the contract
 *                pthread_join's futex_wait depends on.
 *   tls          CLONE_SETTLS arena base (wasm32 offset). 0 = use the
 *                per-process TLS pool slot.
 *   memory_base  exec_ctx->memory; needed because CHILD_CLEARTID writes
 *                through the host VA, not the wasm pointer.
 *   out_tid      receives the allocated tid (>= 1).
 *
 * Returns 0 on success or a negative errno. */
int            yos_clone_thread        (yos_pthread_host *h,
                                         uint32_t fn_idx,
                                         uint32_t arg,
                                         uint32_t ctid_addr,
                                         uint32_t tls,
                                         uint32_t child_stack,
                                         uint8_t *memory_base,
                                         uint32_t *out_tid);

#ifdef __cplusplus
}
#endif

#endif // WASM3MT_PTHREAD_HOST_H
