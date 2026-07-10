// Host-side wasm3mt pthread_* implementations.
//
// Layout and call flow:
//
//   guest pthread_create(t, attr, fn, arg)
//        │  emits a wasm `call` to the env.pthread_create import
//        ▼
//   host_pthread_create
//        │  pthread_creates an OS thread that runs worker_main
//        ▼
//   worker_main
//        │  spins up a sibling wasm3 runtime, parses+loads+links+compiles
//        │  under setup_mu, calls __wasm3mt_thread_entry(tid, fn_idx, arg)
//        ▼
//   guest __wasm3mt_thread_entry: sets per-thread __stack_pointer, then
//                                 call_indirects the user start function.
//
// Synchronisation primitives implemented in the host:
//   * mutex   — Drepper 3-state futex on a u32 cell (0/1/2).
//   * cond    — sequence-counter futex condvar.
//   * TLS     — fixed-size per-thread `void *` array, indexed by pthread_key_t.
//
// "Current host_thread" is found via a real OS pthread_key_t set in
// worker_main. The wasm "main" thread (the one calling yos_pthread_host_create)
// is registered as tid 0 with its own pre-allocated host_thread.

/* _GNU_SOURCE: needed so <unistd.h> exposes the `long syscall(long, …)`
 * prototype glibc keeps under that gate. Required for the SYS_futex
 * call in worker_main's CHILD_CLEARTID path. Includes _POSIX_C_SOURCE
 * implicitly. */
#define _GNU_SOURCE

#include "impl/proc/pthread.h"
#include <yos/yos_pthread.h>
#include "yos/types.h"   /* struct yos_exec_ctx — needed for the
                          * yos_link_imports call in worker_main */

#include "m3_env.h"
#include "m3_compile.h"  /* CompileFunction — needed to JIT pthread_once init routines */

#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/syscall.h>     /* SYS_futex */
#include <unistd.h>          /* syscall() */
/* CLEARTID exit-wake done via the proc table's wait_cond
 * (see impl/proc.c). No Linux futex syscall on the host. */
#include <stdatomic.h>
#include <stdbool.h>
#include <limits.h>

extern int32_t  m3Atomic_Wait32 (volatile uint32_t * addr,
                                 uint32_t expected, int64_t timeout_ns);
extern uint32_t m3Atomic_Notify (void * addr, uint32_t count);


//---------------------------------------------------------------------------
// Constants
//---------------------------------------------------------------------------

#define WASM3MT_MAX_THREADS  128   // must match guest crt.c
#define WASM3MT_MAX_TLS_KEYS 64


//---------------------------------------------------------------------------
// State
//---------------------------------------------------------------------------

struct host_thread
{
    yos_pthread_host *   host;
    uint32_t         tid;
    pthread_t        os_tid;
    bool             os_started;     // true once pthread_create succeeded
    bool             joined;
    bool             is_master;       // tid 0, not pthread_join'able

    uint32_t         fn_idx;
    uint32_t         arg;
    int32_t          retval;
    M3Result         trap;
    char             err [128];

    /* TLS arena base for this thread (wasm pointer). Equals
     * host->tls_pool_base + tid * host->tls_arena_size when the per-process
     * TLS pool is configured; 0 otherwise. This is the value the guest
     * should load into __yos_thread_pointer when the thread starts. */
    uint32_t         tls_arena;

    /* CLONE_CHILD_CLEARTID bookkeeping. Set by yos_pthread_host_spawn
     * (the syscall-clone entry); zeroed for L1-import-spawned threads.
     * On thread exit, if ctid_addr != 0 the worker atomically stores 0
     * at memory_base + ctid_addr and issues SYS_futex(FUTEX_WAKE, 1).
     * That's what unblocks pthread_join's futex_wait without us having
     * to know join was even called. */
    uint32_t         ctid_addr;
    uint8_t *        memory_base;
    /* Guest stack TOP for this thread (the child_stack the guest's
     * pthread_create/clone shim allocated). worker_main points the
     * sibling runtime's __stack_pointer here BEFORE dispatching the
     * entry — without it every thread inherits the module's INITIAL
     * stack pointer and runs on the MAIN thread's stack, silently
     * corrupting it (fzy's 32 KB match() frames turned that into
     * option-flag flips and OOB traps). 0 = legacy L1 spawn, keep the
     * (broken-by-design) old behaviour rather than guess an address. */
    uint32_t         child_stack;
    /* Guest offset of a HOST-provisioned stack region (L1 import path);
     * freed at join. 0 when the guest supplied its own stack. */
    uint32_t         owned_stack_base;

    // POSIX TLS values, indexed by pthread_key_t. NULL = unset.
    void *           tls [WASM3MT_MAX_TLS_KEYS];
};

struct yos_pthread_host
{
    IM3Environment      env;
    IM3Runtime          master;
    const uint8_t *     wasm;
    uint32_t            wasm_len;
    uint32_t            per_thread_stack;

    pthread_mutex_t     setup_mu;       // serialises wasm3 env access
    pthread_mutex_t     threads_mu;
    struct host_thread *threads [WASM3MT_MAX_THREADS];

    /* Per-process TLS arena pool. The pool is a contiguous region of wasm
     * linear memory carved into WASM3MT_MAX_THREADS equal-sized arenas,
     * one per thread slot. Slot N's arena starts at tls_pool_base + N *
     * tls_arena_size. The "associative" mapping is implicit (tid -> arena
     * via arithmetic) and uses the same lock as the thread-slot table.
     * If tls_arena_size == 0 the pool is disabled and tls_arena is 0 for
     * all threads. */
    uint32_t            tls_pool_base;
    uint32_t            tls_arena_size;

    // pthread_key_t allocator. Keys are simple monotonically increasing ints,
    // never reused. pthread_key_delete is a no-op for POC.
    _Atomic uint32_t    next_key;
};

// OS-level pthread_key that lets every host import find its current
// host_thread (and thus tid + tls array) without searching.
static pthread_key_t  g_ht_key;
static pthread_once_t g_ht_key_once = PTHREAD_ONCE_INIT;

static void g_ht_key_init (void) { pthread_key_create (&g_ht_key, NULL); }

static struct host_thread *
current_ht (yos_pthread_host * h)
{
    struct host_thread * ht = (struct host_thread *) pthread_getspecific (g_ht_key);
    if (ht) return ht;
    // Fallback: must be the master / a thread the host didn't track. Return
    // master so pthread_self() etc. work for the wasm "main" thread.
    return h->threads [0];
}


//---------------------------------------------------------------------------
// Lifecycle helpers
//---------------------------------------------------------------------------

static M3Result host_link_imports (yos_pthread_host * h, IM3Module mod);

static int
host_alloc_tid (yos_pthread_host * h, struct host_thread * ht)
{
    pthread_mutex_lock (&h->threads_mu);
    int tid = -1;
    // Scan from 1 — slot 0 is reserved for the master.
    for (uint32_t i = 1; i < WASM3MT_MAX_THREADS; ++i) {
        if (!h->threads[i]) {
            h->threads[i] = ht;
            tid = (int) i;
            ht->tid       = i;
            ht->tls_arena = h->tls_arena_size
                              ? h->tls_pool_base + i * h->tls_arena_size
                              : 0u;
            break;
        }
    }
    pthread_mutex_unlock (&h->threads_mu);
    return tid;
}

static struct host_thread *
host_lookup_thread (yos_pthread_host * h, uint32_t tid)
{
    if (tid >= WASM3MT_MAX_THREADS) return NULL;
    pthread_mutex_lock (&h->threads_mu);
    struct host_thread * ht = h->threads[tid];
    pthread_mutex_unlock (&h->threads_mu);
    return ht;
}

static void
host_free_thread_slot (yos_pthread_host * h, uint32_t tid)
{
    if (tid == 0) return;  // never free the master slot
    pthread_mutex_lock (&h->threads_mu);
    if (tid < WASM3MT_MAX_THREADS) h->threads[tid] = NULL;
    pthread_mutex_unlock (&h->threads_mu);
}


//---------------------------------------------------------------------------
// Worker thread entry
//---------------------------------------------------------------------------

// m3_FreeRuntime touches the *shared* environment's code-page free list
// (Environment_ReleaseCodePages writes env->pagesReleased without any lock).
// Under churn, concurrent worker frees race and crash. Serialise.
static void
free_runtime_locked (yos_pthread_host * h, IM3Runtime rt)
{
    pthread_mutex_lock (&h->setup_mu);
    m3_FreeRuntime (rt);
    pthread_mutex_unlock (&h->setup_mu);
}

static void *
worker_main (void * arg)
{
    struct host_thread * ht = (struct host_thread *) arg;
    yos_pthread_host *       h  = ht->host;

    pthread_setspecific (g_ht_key, ht);

    IM3Runtime rt = m3_NewSiblingRuntime (h->master, h->per_thread_stack, NULL);
    if (!rt) {
        snprintf (ht->err, sizeof ht->err, "NewSiblingRuntime failed");
        ht->trap = (M3Result) ht->err;
        return NULL;
    }

    /* Parse + load under setup_mu (m3 env code-page allocator isn't
     * fully reentrant; we serialise the small wasm3-internal critical
     * section). */
    pthread_mutex_lock (&h->setup_mu);
    IM3Module mod = NULL;
    M3Result  r = m3_ParseModule (h->env, &mod, h->wasm, h->wasm_len);
    if (!r) r = m3_LoadModule    (rt, mod);
    if (!r) r = host_link_imports (h, mod);
    pthread_mutex_unlock (&h->setup_mu);

    /* Bind the FULL yos kernel surface (__yos_syscall, m3_setjmp,
     * m3_longjmp, soft-f128 helpers, the wildcard unresolved-stub) on
     * the sibling. The master's bindings live in main.c::yos_link_imports
     * and only apply to the master's loaded module — every sibling
     * runtime parses + loads its own copy and needs its own bindings.
     *
     * Critical: do this OUTSIDE setup_mu — yos_link_imports calls back
     * into yos_pthread_host_link which itself takes setup_mu, and
     * pthread_mutex on a non-recursive lock would deadlock. */
    if (!r) {
        extern void yos_link_imports (IM3Module module, struct yos_exec_ctx *ctx);
        struct yos_exec_ctx *master_ctx =
            h->master ? (struct yos_exec_ctx *)h->master->userdata : NULL;
        if (master_ctx) {
            /* Wire the sibling's userdata to the SAME ctx as the
             * master — guest threads share memory + fd table + proc
             * table, so syscalls dispatch through one ctx.
             *
             * We must NOT mutate master_ctx->runtime here, even
             * "temporarily and restore" — the master's wasm thread
             * is concurrently running and may dereference
             * master_ctx->runtime in any bridge (e.g. env.fork →
             * yos_fork reads it for m3_FindFunction on
             * "asyncify_get_state"). Pointing it at our half-loaded
             * sibling runtime mid-setup surfaced as a SIGSEGV in
             * m3_FindFunction (functions[i].import.moduleUtf8 deref,
             * because our sibling's Module_AddFunction was still
             * in-flight: numFunctions++ done, functions realloc
             * pending). The link path doesn't need ctx->runtime to
             * be the sibling's — yos_link_imports just calls
             * m3_LinkRawFunctionEx (module, ..., ctx) and the lazy
             * pthread_host_create is skipped because the master
             * already owns one. So leave master_ctx->runtime alone. */
            rt->userdata = master_ctx;
            yos_link_imports (mod, master_ctx);
        }
    }

    /* Compile under setup_mu again — code-page allocator concern. */
    pthread_mutex_lock (&h->setup_mu);
    if (!r) r = m3_CompileModule (mod);
    pthread_mutex_unlock (&h->setup_mu);
    if (r) {
        snprintf (ht->err, sizeof ht->err, "setup: %s", r);
        ht->trap = (M3Result) ht->err;
        free_runtime_locked (h, rt);
        return NULL;
    }

    /* Look up the user start function by its function-table index.
     * fn_idx is what the guest passed to clone() (or the legacy
     * yos_pthread_create import) — it's a wasm32 function pointer,
     * which IS a slot in the module's __indirect_function_table.
     * wasm3 keeps that table at module->table0; entries are
     * IM3Function pointers we can m3_Call directly. No
     * `__wasm3mt_thread_entry` guest-side helper needed: clone()
     * dispatches fn(arg) straight from the host. */
    if (ht->fn_idx == 0 || ht->fn_idx >= mod->table0Size ||
        mod->table0[ht->fn_idx] == NULL) {
        snprintf (ht->err, sizeof ht->err,
                  "clone: fn_idx %u out of table (size %u)",
                  ht->fn_idx, mod->table0Size);
        ht->trap = (M3Result) ht->err;
        free_runtime_locked (h, rt);
        return NULL;
    }
    IM3Function entry = mod->table0[ht->fn_idx];

    /* Point this thread's guest stack at the child_stack the guest's
     * clone shim allocated. __stack_pointer is not exported (lld only
     * exports mutable globals behind a feature flag), but lld always
     * emits it as the FIRST mutable i32 global — the same internals
     * access as table0 above. Skipping this ran the thread on the
     * module's initial stack == the MAIN thread's stack. */
    if (ht->child_stack) {
        bool sp_set = false;
        for (u32 gi = 0; gi < mod->numGlobals; ++gi) {
            if (mod->globals[gi].isMutable
                && mod->globals[gi].type == c_m3Type_i32) {
                mod->globals[gi].intValue = (i64) ht->child_stack;
                sp_set = true;
                break;
            }
        }
        if (!sp_set)
            fprintf (stderr, "wasm3mt: thread %u: no mutable i32 global — "
                     "cannot relocate guest stack\n", ht->tid);
    }

    /* Call fn(arg) — single i32 arg as the wasm signature requires. */
    int32_t a0 = (int32_t) ht->arg;
    const void * argp [1] = { &a0 };
    r = m3_Call (entry, 1, argp);
    if (r) {
        ht->trap = r;
        free_runtime_locked (h, rt);
        return NULL;
    }

    int32_t ret = 0;
    const void * outp [1] = { &ret };
    M3Result rr = m3_GetResults (entry, 1, outp);
    if (!rr) ht->retval = ret;

    /* CLONE_CHILD_CLEARTID: zero the wasm-side tid word. host_pthread_join
     * waits on a host pthread_cond (set up below at thread spawn) — the
     * cond_signal there is what wakes the joiner, not a futex syscall. */
    if (ht->ctid_addr && ht->memory_base) {
        uint32_t * tid_word = (uint32_t *)(ht->memory_base + ht->ctid_addr);
        __atomic_store_n (tid_word, 0u, __ATOMIC_SEQ_CST);
    }

    free_runtime_locked (h, rt);
    return NULL;
}


//---------------------------------------------------------------------------
// Mutex primitives (Drepper 3-state futex)
//---------------------------------------------------------------------------

static int
mu_lock_cell (uint32_t * cell)
{
    if (!cell) return -1;
    uint32_t c = 0;
    if (__atomic_compare_exchange_n (cell, &c, 1u,
                                     0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return 0;
    if (c != 2u)
        c = __atomic_exchange_n (cell, 2u, __ATOMIC_ACQUIRE);
    while (c != 0u) {
        m3Atomic_Wait32 ((volatile uint32_t *) cell, 2u, -1);
        c = __atomic_exchange_n (cell, 2u, __ATOMIC_ACQUIRE);
    }
    return 0;
}

static int
mu_unlock_cell (uint32_t * cell)
{
    if (!cell) return -1;
    uint32_t prev = __atomic_exchange_n (cell, 0u, __ATOMIC_RELEASE);
    if (prev == 2u)
        m3Atomic_Notify ((void *) cell, 1u);
    return 0;
}


//---------------------------------------------------------------------------
// Internal thread-spawn substrate — the SINGLE place a guest thread
// gets created. yos_proc_clone (the SYS_clone handler) calls this, and
// for back-compat with already-built modules the L1 yos_pthread_create
// import below is also a thin shim over the same function. Once musl
// wasm32 ships standard pthread_create on top of __clone+SYS_clone the
// L1 import goes away.
//---------------------------------------------------------------------------

int
yos_clone_thread (yos_pthread_host *h,
                  uint32_t fn_idx,
                  uint32_t arg,
                  uint32_t ctid_addr,
                  uint32_t tls,
                  uint32_t child_stack,
                  uint8_t *memory_base,
                  uint32_t *out_tid)
{
    if (!h) return -EINVAL;

    struct host_thread *ht = (struct host_thread *) calloc (1, sizeof *ht);
    if (!ht) return -ENOMEM;

    ht->host        = h;
    ht->fn_idx      = fn_idx;
    ht->arg         = arg;
    ht->ctid_addr   = ctid_addr;
    ht->memory_base = memory_base;
    ht->child_stack = child_stack;

    int tid = host_alloc_tid (h, ht);
    if (tid < 0) { free (ht); return -EAGAIN; }
    ht->tid = (uint32_t) tid;

    /* CLONE_SETTLS: caller-supplied TLS base wins over the per-process
     * pool slot. If 0, the slot's pool address is left in place by
     * host_alloc_tid. */
    if (tls) ht->tls_arena = tls;

    /* Cap host pthread stack — default 8 MB × 128 threads would burn a
     * gigabyte of VM with most unused. 1 MB is comfortable for wasm3's
     * opcode-dispatch chain plus libc bridge frames; the original
     * 256 KB was too tight on darwin once macOS' default 16 KB pages
     * and debug-build wasm3 (no sibling-call elim) were in play, and
     * an nvim run blew the guard page in op_SetSlot_i32's `call *%rax`
     * to the next opcode handler. */
    pthread_attr_t attr;
    pthread_attr_init        (&attr);
    pthread_attr_setstacksize (&attr, 1024 * 1024);

    int rc = pthread_create (&ht->os_tid, &attr, worker_main, ht);
    pthread_attr_destroy    (&attr);
    if (rc != 0) {
        host_free_thread_slot (h, ht->tid);
        free (ht);
        return -EAGAIN;
    }
    ht->os_started = true;

    if (out_tid) *out_tid = ht->tid;
    return 0;
}

//---------------------------------------------------------------------------
// Legacy L1 import — calls the same internal helper. Slated for removal
// once musl wasm32 pthread_create is rewritten to call SYS_clone.
//---------------------------------------------------------------------------

static m3ApiRawFunction (host_pthread_create)
{
    m3ApiReturnType  (int32_t)
    m3ApiGetArgMem   (uint32_t *, t_ptr)
    m3ApiGetArgMem   (void *,     attr_ptr)
    m3ApiGetArg      (uint32_t,   fn_idx)
    m3ApiGetArg      (uint32_t,   arg)
    (void) attr_ptr;

    yos_pthread_host * h = (yos_pthread_host *) _ctx->userdata;

    /* The L1 import has no guest-allocated stack (the sysroot wrapper
     * never makes one), so provision it HOST-side from the guest
     * allocator — the host owns that allocator (impl/mem/alloc.c), so
     * a plain yos_malloc hands back a guest region no other allocation
     * will overlap. Without this the new thread inherited the module's
     * INITIAL __stack_pointer and ran on the MAIN thread's stack: fzy's
     * ~32 KB match() frames flipped option flags and walked off into
     * OOB traps. 256 KB matches the comfortable end of what the
     * interpreted guests need; the region is freed on join via
     * ht->owned_stack_base. */
    enum { YOS_L1_THREAD_STACK = 256 * 1024 };
    uint32_t stack_base = 0, stack_top = 0;
    {
        extern uint32_t yos_malloc(struct yos_exec_ctx *, uint32_t);
        struct yos_exec_ctx *ctx =
            (struct yos_exec_ctx *) m3_GetUserData (runtime);
        if (ctx) {
            stack_base = yos_malloc (ctx, YOS_L1_THREAD_STACK);
            if (stack_base) {
                /* Top, 16-byte aligned downward (wasm32 C ABI). */
                stack_top = (stack_base + YOS_L1_THREAD_STACK) & ~15u;
            }
        }
    }

    uint32_t tid = 0;
    int rc = yos_clone_thread (h, fn_idx, arg,
                               /*ctid_addr=*/0, /*tls=*/0,
                               /*child_stack=*/stack_top,
                               /*memory_base=*/NULL, &tid);
    if (rc != 0) {
        if (stack_base) {
            extern void yos_free(struct yos_exec_ctx *, uint32_t);
            struct yos_exec_ctx *ctx =
                (struct yos_exec_ctx *) m3_GetUserData (runtime);
            if (ctx) yos_free (ctx, stack_base);
        }
        m3ApiReturn (-1);
    }
    if (stack_base) {
        struct host_thread * ht = host_lookup_thread (h, tid);
        if (ht) ht->owned_stack_base = stack_base;
    }

    if (t_ptr) *t_ptr = tid;
    m3ApiReturn (0);
}

static m3ApiRawFunction (host_pthread_join)
{
    m3ApiReturnType  (int32_t)
    m3ApiGetArg      (uint32_t,   tid)
    m3ApiGetArgMem   (uint32_t *, retval_ptr)

    yos_pthread_host * h = (yos_pthread_host *) _ctx->userdata;

    struct host_thread * ht = host_lookup_thread (h, tid);
    if (!ht || ht->joined || !ht->os_started || ht->is_master)
        m3ApiReturn (-1);

    pthread_join (ht->os_tid, NULL);
    ht->joined = true;

    if (ht->trap)
        fprintf (stderr, "wasm3mt: thread %u trap: %s\n", tid,
                 (const char *) ht->trap);

    if (retval_ptr) *retval_ptr = (uint32_t) ht->retval;

    /* Release a host-provisioned L1 stack region (see host_pthread_create). */
    if (ht->owned_stack_base) {
        extern void yos_free(struct yos_exec_ctx *, uint32_t);
        struct yos_exec_ctx *jctx =
            (struct yos_exec_ctx *) m3_GetUserData (runtime);
        if (jctx) yos_free (jctx, ht->owned_stack_base);
    }

    host_free_thread_slot (h, tid);
    free (ht);

    m3ApiReturn (0);
}

static m3ApiRawFunction (host_pthread_self)
{
    m3ApiReturnType (uint32_t)
    yos_pthread_host * h = (yos_pthread_host *) _ctx->userdata;
    struct host_thread * ht = current_ht (h);
    m3ApiReturn (ht ? ht->tid : 0);
}

/* Returns the calling thread's TLS arena base (TP). The guest's thread-entry
 * stub calls this once at startup and stores the result in the per-sibling
 * __yos_thread_pointer wasm global so __get_tp() reads it inline thereafter. */
static m3ApiRawFunction (host_pthread_get_tp)
{
    m3ApiReturnType (uint32_t)
    yos_pthread_host * h = (yos_pthread_host *) _ctx->userdata;
    struct host_thread * ht = current_ht (h);
    m3ApiReturn (ht ? ht->tls_arena : 0u);
}


//---------------------------------------------------------------------------
// Imports — mutex
//---------------------------------------------------------------------------

static m3ApiRawFunction (host_pthread_mutex_init)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, cell)
    m3ApiGetArgMem  (void *,     attr)
    (void) attr;

    if (cell) __atomic_store_n (cell, 0u, __ATOMIC_SEQ_CST);
    m3ApiReturn (0);
}

static m3ApiRawFunction (host_pthread_mutex_destroy)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, cell)
    if (cell) __atomic_store_n (cell, 0u, __ATOMIC_SEQ_CST);
    m3ApiReturn (0);
}

static m3ApiRawFunction (host_pthread_mutex_lock)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, cell)
    m3ApiReturn (mu_lock_cell (cell));
}

static m3ApiRawFunction (host_pthread_mutex_trylock)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, cell)
    if (!cell) m3ApiReturn (-1);

    uint32_t expected = 0;
    if (__atomic_compare_exchange_n (cell, &expected, 1u,
                                     0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        m3ApiReturn (0);
    m3ApiReturn (16); // EBUSY
}

static m3ApiRawFunction (host_pthread_mutex_unlock)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, cell)
    m3ApiReturn (mu_unlock_cell (cell));
}

//---------------------------------------------------------------------------
// Imports — spinlock
//
// pthread_spinlock_t is an opaque int in the FreeBSD guest. We treat the
// 32-bit cell as a plain test-and-set lock with C11 atomics — the same
// implementation works on Linux and darwin (darwin's libSystem has no
// pthread_spin_* at all, so a tier-1 passthrough would fail to link).
//---------------------------------------------------------------------------
static m3ApiRawFunction (host_pthread_spin_init)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, cell)
    m3ApiGetArg     (int32_t,    pshared) (void) pshared;
    if (!cell) m3ApiReturn (22);  // EINVAL
    __atomic_store_n (cell, 0u, __ATOMIC_SEQ_CST);
    m3ApiReturn (0);
}
static m3ApiRawFunction (host_pthread_spin_destroy)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, cell) (void) cell;
    m3ApiReturn (0);
}
static m3ApiRawFunction (host_pthread_spin_lock)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, cell)
    if (!cell) m3ApiReturn (22);  // EINVAL
    for (;;) {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n (cell, &expected, 1u, 0,
                                         __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            m3ApiReturn (0);
        /* Busy-wait with a relaxed load; this is a SPIN lock, so no
         * futex/cond fallback. Pause hint would go here on archs that
         * have one. */
    }
}
static m3ApiRawFunction (host_pthread_spin_trylock)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, cell)
    if (!cell) m3ApiReturn (22);
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n (cell, &expected, 1u, 0,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        m3ApiReturn (0);
    m3ApiReturn (16);  // EBUSY
}
static m3ApiRawFunction (host_pthread_spin_unlock)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, cell)
    if (!cell) m3ApiReturn (22);
    __atomic_store_n (cell, 0u, __ATOMIC_RELEASE);
    m3ApiReturn (0);
}


// Mutex attr stubs — wasm code may legitimately call these even if we don't
// honour kind / type / pshared / robust. Return 0 so it thinks they succeed.
static m3ApiRawFunction (host_pthread_mutexattr_noop_1)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (void *, p) (void) p;
    m3ApiReturn (0);
}
static m3ApiRawFunction (host_pthread_mutexattr_noop_2)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (void *, p) (void) p;
    m3ApiGetArg     (int32_t, x) (void) x;
    m3ApiReturn (0);
}


//---------------------------------------------------------------------------
// Imports — cond
//
// Layout: u32 cell holding a sequence counter. cond_wait records the seq
// before unlocking the mutex, then m3Atomic_Wait32(&seq, expected, ∞).
// cond_signal/broadcast atomically bumps seq and notifies; futex_wait sees
// the seq advance and returns "not equal".
//---------------------------------------------------------------------------

static m3ApiRawFunction (host_pthread_cond_init)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, c)
    m3ApiGetArgMem  (void *,     attr) (void) attr;
    if (c) __atomic_store_n (c, 0u, __ATOMIC_SEQ_CST);
    m3ApiReturn (0);
}

static m3ApiRawFunction (host_pthread_cond_destroy)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, c) (void) c;
    m3ApiReturn (0);
}

static m3ApiRawFunction (host_pthread_cond_wait)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, c)
    m3ApiGetArgMem  (uint32_t *, m)
    if (!c || !m) m3ApiReturn (-1);

    uint32_t seq = __atomic_load_n (c, __ATOMIC_SEQ_CST);
    if (mu_unlock_cell (m) != 0) m3ApiReturn (-1);

    // Sleep while *c == seq.
    m3Atomic_Wait32 ((volatile uint32_t *) c, seq, -1);

    int rc = mu_lock_cell (m);
    m3ApiReturn (rc);
}

static m3ApiRawFunction (host_pthread_cond_signal)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, c)
    if (!c) m3ApiReturn (-1);
    __atomic_fetch_add (c, 1u, __ATOMIC_SEQ_CST);
    m3Atomic_Notify ((void *) c, 1u);
    m3ApiReturn (0);
}

static m3ApiRawFunction (host_pthread_cond_broadcast)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, c)
    if (!c) m3ApiReturn (-1);
    __atomic_fetch_add (c, 1u, __ATOMIC_SEQ_CST);
    m3Atomic_Notify ((void *) c, UINT32_MAX);
    m3ApiReturn (0);
}

// Helper — current monotonic clock in ns.
static int64_t
mono_now_ns (void)
{
    struct timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return (int64_t) ts.tv_sec * 1000000000LL + (int64_t) ts.tv_nsec;
}

// pthread_cond_timedwait(cond*, mutex*, timespec*) -> i32
// FreeBSD i386 struct timespec: int64 tv_sec + long tv_nsec. We read
// it from wasm memory and convert to ns. Returns 0 on signal,
// ETIMEDOUT (110) on timeout, -1 on bad args.
static m3ApiRawFunction (host_pthread_cond_timedwait)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, c)
    m3ApiGetArgMem  (uint32_t *, m)
    m3ApiGetArgMem  (uint8_t  *, ts_p)

    if (!c || !m || !ts_p) m3ApiReturn (-1);

    /* FreeBSD wasm32 timespec layout: 8-byte int64 tv_sec at offset 0,
     * 4-byte int32 tv_nsec at offset 8 (alignment of int64_t on i386
     * is 4). Total 12 bytes. */
    int64_t tv_sec;
    int32_t tv_nsec;
    memcpy(&tv_sec,  ts_p + 0, 8);
    memcpy(&tv_nsec, ts_p + 8, 4);
    int64_t abstime_ns = tv_sec * 1000000000LL + (int64_t)tv_nsec;

    int64_t now = mono_now_ns ();
    int64_t timeout = abstime_ns - now;
    if (timeout < 0) timeout = 0;

    uint32_t seq = __atomic_load_n (c, __ATOMIC_SEQ_CST);
    if (mu_unlock_cell (m) != 0) m3ApiReturn (-1);

    int32_t r = m3Atomic_Wait32 ((volatile uint32_t *) c, seq, timeout);

    int rc = mu_lock_cell (m);
    if (rc != 0) m3ApiReturn (rc);
    m3ApiReturn (r == 2 ? 110 /*ETIMEDOUT*/ : 0);
}


//---------------------------------------------------------------------------
// Imports — pthread_rwlock_t  (16 B: mu, state, cond_seq, writers_waiting)
//
//   _internal[0] = u32  internal mutex (Drepper futex on this very cell)
//   _internal[1] = u32  state: 0=free, 1..N=N readers, 0xFFFFFFFF=writer held
//   _internal[2] = u32  cond_seq  (futex generation for waiters)
//   _internal[3] = u32  writers_waiting  (writer-preferring policy)
//
// Writer-preferring: a new reader cannot acquire if any writer is waiting.
// This prevents continuous reader traffic from starving the writer.
// Notify on every unlock (not just transition-to-free) so writers and
// queued readers both get a chance to retry.
//---------------------------------------------------------------------------

#define RW_FREE     0u
#define RW_WRITER   0xFFFFFFFFu

/* FreeBSD's pthread_rwlock_t is a 4-byte pointer (`struct pthread_rwlock *`)
 * — the wasm guest's storage for one rwlock is exactly 4 bytes. Earlier
 * impl wrote 4×u32 = 16 bytes inline, stomping the two adjacent fields.
 * For a uv_loop_t.cloexec_lock that's `closing_handles` + the first 4 bytes
 * of `process_handles` getting zeroed at every uv_loop_init → uv_rwlock_init,
 * which deinitialised libuv's process queue and crashed uv__io_poll's
 * EVFILT_PROC handler with a stale pointer. Now we pack everything into
 * the single 4-byte cell:
 *   value == 0xFFFFFFFF   : writer holds
 *   value > 0 (< 0xFFFFFFFF): reader count
 *   value == 0            : free
 * Wait/notify uses the same cell — every state-changing op increments
 * the cell as part of its CAS, and notify wakes everyone. */
static int
rw_init (uint32_t * rw)
{
    if (!rw) return -1;
    __atomic_store_n (rw, 0u, __ATOMIC_SEQ_CST);
    return 0;
}

static int
rw_rdlock_impl (uint32_t * rw, int try_only)
{
    if (!rw) return -1;
    while (1) {
        uint32_t st = __atomic_load_n (rw, __ATOMIC_ACQUIRE);
        if (st != RW_WRITER && st < (RW_WRITER - 1u)) {
            uint32_t want = st + 1u;
            if (__atomic_compare_exchange_n (rw, &st, want, 0,
                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return 0;
            continue;  /* CAS lost — retry */
        }
        if (try_only) return 16; /* EBUSY */
        m3Atomic_Wait32 ((volatile uint32_t *) rw, st, -1);
    }
}

static int
rw_wrlock_impl (uint32_t * rw, int try_only)
{
    if (!rw) return -1;
    while (1) {
        uint32_t st = __atomic_load_n (rw, __ATOMIC_ACQUIRE);
        if (st == RW_FREE) {
            if (__atomic_compare_exchange_n (rw, &st, RW_WRITER, 0,
                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return 0;
            continue;  /* CAS lost — retry */
        }
        if (try_only) return 16; /* EBUSY */
        m3Atomic_Wait32 ((volatile uint32_t *) rw, st, -1);
    }
}

static int
rw_unlock_impl (uint32_t * rw)
{
    if (!rw) return -1;
    while (1) {
        uint32_t st = __atomic_load_n (rw, __ATOMIC_ACQUIRE);
        uint32_t new_st;
        if (st == RW_WRITER)    new_st = RW_FREE;
        else if (st > 0u)       new_st = st - 1u;
        else                    return 22; /* EINVAL — unlock of free lock */
        if (__atomic_compare_exchange_n (rw, &st, new_st, 0,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            m3Atomic_Notify ((void *) rw, UINT32_MAX);
            return 0;
        }
    }
}

static m3ApiRawFunction (host_pthread_rwlock_init)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, rw)
    m3ApiGetArgMem  (void *,     attr) (void) attr;
    m3ApiReturn (rw_init (rw));
}
static m3ApiRawFunction (host_pthread_rwlock_destroy)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, rw) (void) rw;
    m3ApiReturn (0);
}
static m3ApiRawFunction (host_pthread_rwlock_rdlock)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, rw)
    m3ApiReturn (rw_rdlock_impl (rw, 0));
}
static m3ApiRawFunction (host_pthread_rwlock_wrlock)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, rw)
    m3ApiReturn (rw_wrlock_impl (rw, 0));
}
static m3ApiRawFunction (host_pthread_rwlock_tryrdlock)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, rw)
    m3ApiReturn (rw_rdlock_impl (rw, 1));
}
static m3ApiRawFunction (host_pthread_rwlock_trywrlock)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, rw)
    m3ApiReturn (rw_wrlock_impl (rw, 1));
}
static m3ApiRawFunction (host_pthread_rwlock_unlock)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, rw)
    m3ApiReturn (rw_unlock_impl (rw));
}


//---------------------------------------------------------------------------
// Imports — pthread_barrier_t (20 B: mu, generation, arrived, count, cond_seq)
//---------------------------------------------------------------------------

static m3ApiRawFunction (host_pthread_barrier_init)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, b)
    m3ApiGetArgMem  (void *,     attr) (void) attr;
    m3ApiGetArg     (uint32_t,   count)

    if (!b || count == 0) m3ApiReturn (22 /*EINVAL*/);
    __atomic_store_n (&b[0], 0u, __ATOMIC_SEQ_CST);    // mu
    __atomic_store_n (&b[1], 0u, __ATOMIC_SEQ_CST);    // generation
    __atomic_store_n (&b[2], 0u, __ATOMIC_SEQ_CST);    // arrived
    __atomic_store_n (&b[3], count, __ATOMIC_SEQ_CST); // count
    __atomic_store_n (&b[4], 0u, __ATOMIC_SEQ_CST);    // cond_seq
    m3ApiReturn (0);
}

static m3ApiRawFunction (host_pthread_barrier_destroy)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, b) (void) b;
    m3ApiReturn (0);
}

static m3ApiRawFunction (host_pthread_barrier_wait)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, b)
    if (!b) m3ApiReturn (22);

    mu_lock_cell (&b[0]);
    uint32_t gen      = __atomic_load_n (&b[1], __ATOMIC_ACQUIRE);
    uint32_t arrived  = __atomic_load_n (&b[2], __ATOMIC_ACQUIRE) + 1u;
    uint32_t count    = __atomic_load_n (&b[3], __ATOMIC_ACQUIRE);
    __atomic_store_n (&b[2], arrived, __ATOMIC_RELEASE);

    if (arrived == count) {
        __atomic_store_n (&b[2], 0u, __ATOMIC_RELEASE);
        __atomic_fetch_add (&b[1], 1u, __ATOMIC_RELEASE);
        __atomic_fetch_add (&b[4], 1u, __ATOMIC_SEQ_CST);
        m3Atomic_Notify ((void *) &b[4], UINT32_MAX);
        mu_unlock_cell (&b[0]);
        m3ApiReturn (-1); // PTHREAD_BARRIER_SERIAL_THREAD
    }

    while (__atomic_load_n (&b[1], __ATOMIC_ACQUIRE) == gen) {
        uint32_t seq = __atomic_load_n (&b[4], __ATOMIC_SEQ_CST);
        mu_unlock_cell (&b[0]);
        m3Atomic_Wait32 ((volatile uint32_t *) &b[4], seq, -1);
        mu_lock_cell (&b[0]);
    }
    mu_unlock_cell (&b[0]);
    m3ApiReturn (0);
}


//---------------------------------------------------------------------------
// Imports — pthread_once  (8 B: state, cond_seq)
//
// state: 0=initial, 1=running, 2=done.
// The init routine is a wasm function index (table entry); we look it up on
// the *calling thread's* runtime and m3_Call it directly. wasm3 supports
// nested calls (op_CallRawFunction backs up `runtime->stack` before invoking
// the import, so a re-entrant m3_Call from inside picks up correctly).
//---------------------------------------------------------------------------

/* Single-cell once: state values are 0 (NEW), 1 (IN_PROGRESS), 2 (DONE).
 * Waiters park on the cell itself with memory.atomic.wait32, the runner
 * notifies on transition to DONE. */
static m3ApiRawFunction (host_pthread_once)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, ostate)
    m3ApiGetArg     (uint32_t,   fn_idx)

    if (!ostate) m3ApiReturn (-1);

    uint32_t expected = 0;
    if (__atomic_compare_exchange_n (ostate, &expected, 1u,
                                     0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
    {
        // We won the race — run the init routine.
        IM3Module mod = runtime->modules;
        IM3Function fn = (mod && fn_idx < mod->table0Size) ? mod->table0[fn_idx] : NULL;
        M3Result r = NULL;
        if (!fn) {
            r = "yos_pthread_once: init fn index out of table0";
        } else if (!fn->compiled) {
            /* Indirect-call targets are JIT-compiled lazily; pthread_once's
             * init routine is rarely also a direct caller, so we hit this
             * path on first call. Mirrors what wasm3's call_indirect op
             * does inline (m3_exec.h ~line 572). */
            r = CompileFunction (fn);
        }
        if (!r) r = m3_Call (fn, 0, NULL);
        __atomic_store_n (ostate, 2u, __ATOMIC_SEQ_CST);
        m3Atomic_Notify ((void *) ostate, UINT32_MAX);
        m3ApiReturn (r ? -1 : 0);
    }

    // Lost the race. Park on the cell until it transitions to DONE.
    while (__atomic_load_n (ostate, __ATOMIC_SEQ_CST) != 2u)
        m3Atomic_Wait32 ((volatile uint32_t *) ostate, 1u, -1);
    m3ApiReturn (0);
}


//---------------------------------------------------------------------------
// Helpers — wasm3mt_now_ns(), wasm3mt_yield()
//---------------------------------------------------------------------------

static m3ApiRawFunction (host_wasm3mt_now_ns)
{
    m3ApiReturnType (int64_t)
    m3ApiReturn (mono_now_ns ());
}

static m3ApiRawFunction (host_wasm3mt_yield)
{
    sched_yield ();
    m3ApiSuccess ();
}


//---------------------------------------------------------------------------
// Imports — TLS (pthread_key_t)
//---------------------------------------------------------------------------

static m3ApiRawFunction (host_pthread_key_create)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArgMem  (uint32_t *, key_ptr)
    m3ApiGetArgMem  (void *,     dtor) (void) dtor;

    if (!key_ptr) m3ApiReturn (-1);
    yos_pthread_host * h = (yos_pthread_host *) _ctx->userdata;
    uint32_t k = atomic_fetch_add (&h->next_key, 1u);
    if (k >= WASM3MT_MAX_TLS_KEYS) m3ApiReturn (-1);
    *key_ptr = k;
    m3ApiReturn (0);
}

static m3ApiRawFunction (host_pthread_key_delete)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArg     (uint32_t, key) (void) key;
    // No-op: we don't reuse keys, and we don't clear them in live threads.
    m3ApiReturn (0);
}

static m3ApiRawFunction (host_pthread_setspecific)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArg     (uint32_t, key)
    m3ApiGetArg     (uint32_t, value)
    yos_pthread_host * h = (yos_pthread_host *) _ctx->userdata;
    if (key >= WASM3MT_MAX_TLS_KEYS) m3ApiReturn (-1);

    struct host_thread * ht = current_ht (h);
    if (!ht) m3ApiReturn (-1);
    ht->tls [key] = (void *)(uintptr_t) value;
    m3ApiReturn (0);
}

static m3ApiRawFunction (host_pthread_getspecific)
{
    m3ApiReturnType (uint32_t)
    m3ApiGetArg     (uint32_t, key)
    yos_pthread_host * h = (yos_pthread_host *) _ctx->userdata;
    if (key >= WASM3MT_MAX_TLS_KEYS) m3ApiReturn (0u);

    struct host_thread * ht = current_ht (h);
    if (!ht) m3ApiReturn (0u);
    m3ApiReturn ((uint32_t)(uintptr_t) ht->tls [key]);
}


//---------------------------------------------------------------------------
// Linker
//---------------------------------------------------------------------------

#define LINK(name, sig, fn)                                                \
    do {                                                                   \
        M3Result _r = m3_LinkRawFunctionEx (mod, "env", name, sig, fn, h); \
        if (_r && _r != m3Err_functionLookupFailed) {                      \
            fprintf(stderr, "yos pthread LINK(%s, %s) failed: %s\n",       \
                    name, sig, _r);                                        \
            return _r;                                                     \
        }                                                                  \
    } while (0)

static M3Result
host_link_imports (yos_pthread_host * h, IM3Module mod)
{
    LINK ("pthread_create",        "i(**ii)",  host_pthread_create);
    LINK ("pthread_join",          "i(i*)",    host_pthread_join);
    LINK ("pthread_self",          "i()",      host_pthread_self);
    LINK ("pthread_get_tp",        "i()",      host_pthread_get_tp);

    LINK ("pthread_mutex_init",    "i(**)",    host_pthread_mutex_init);
    LINK ("pthread_mutex_destroy", "i(*)",     host_pthread_mutex_destroy);
    LINK ("pthread_mutex_lock",    "i(*)",     host_pthread_mutex_lock);
    LINK ("pthread_mutex_trylock", "i(*)",     host_pthread_mutex_trylock);
    LINK ("pthread_mutex_unlock",  "i(*)",     host_pthread_mutex_unlock);

    LINK ("pthread_spin_init",     "i(*i)",  host_pthread_spin_init);
    LINK ("pthread_spin_destroy",  "i(*)",   host_pthread_spin_destroy);
    LINK ("pthread_spin_lock",     "i(*)",   host_pthread_spin_lock);
    LINK ("pthread_spin_trylock",  "i(*)",   host_pthread_spin_trylock);
    LINK ("pthread_spin_unlock",   "i(*)",   host_pthread_spin_unlock);

    LINK ("pthread_mutexattr_init",    "i(*)",   host_pthread_mutexattr_noop_1);
    LINK ("pthread_mutexattr_destroy", "i(*)",   host_pthread_mutexattr_noop_1);
    LINK ("pthread_mutexattr_settype", "i(*i)",  host_pthread_mutexattr_noop_2);

    LINK ("pthread_cond_init",       "i(**)",  host_pthread_cond_init);
    LINK ("pthread_cond_destroy",    "i(*)",   host_pthread_cond_destroy);
    LINK ("pthread_cond_wait",       "i(**)",  host_pthread_cond_wait);
    LINK ("pthread_cond_timedwait",  "i(***)", host_pthread_cond_timedwait);
    LINK ("pthread_cond_signal",     "i(*)",   host_pthread_cond_signal);
    LINK ("pthread_cond_broadcast",  "i(*)",   host_pthread_cond_broadcast);

    LINK ("pthread_key_create",     "i(**)",   host_pthread_key_create);
    LINK ("pthread_key_delete",     "i(i)",    host_pthread_key_delete);
    LINK ("pthread_setspecific",    "i(ii)",   host_pthread_setspecific);
    LINK ("pthread_getspecific",    "i(i)",    host_pthread_getspecific);

    LINK ("pthread_rwlock_init",       "i(**)", host_pthread_rwlock_init);
    LINK ("pthread_rwlock_destroy",    "i(*)",  host_pthread_rwlock_destroy);
    LINK ("pthread_rwlock_rdlock",     "i(*)",  host_pthread_rwlock_rdlock);
    LINK ("pthread_rwlock_wrlock",     "i(*)",  host_pthread_rwlock_wrlock);
    LINK ("pthread_rwlock_tryrdlock",  "i(*)",  host_pthread_rwlock_tryrdlock);
    LINK ("pthread_rwlock_trywrlock",  "i(*)",  host_pthread_rwlock_trywrlock);
    LINK ("pthread_rwlock_unlock",     "i(*)",  host_pthread_rwlock_unlock);

    LINK ("pthread_barrier_init",    "i(**i)", host_pthread_barrier_init);
    LINK ("pthread_barrier_destroy", "i(*)",   host_pthread_barrier_destroy);
    LINK ("pthread_barrier_wait",    "i(*)",   host_pthread_barrier_wait);

    LINK ("pthread_once",  "i(*i)", host_pthread_once);

    LINK ("wasm3mt_now_ns", "I()", host_wasm3mt_now_ns);
    LINK ("wasm3mt_yield",  "v()", host_wasm3mt_yield);

    return m3Err_none;
}

#undef LINK


//---------------------------------------------------------------------------
// Public API
//---------------------------------------------------------------------------

yos_pthread_host *
yos_pthread_host_create (IM3Environment env, IM3Runtime master,
                     const uint8_t * wasm, uint32_t wasm_len,
                     uint32_t per_thread_stack_bytes,
                     uint32_t tls_pool_base, uint32_t tls_arena_size)
{
    /* env is required (the imports are bound through the wasm3 environment).
     * master/wasm are needed only by yos_pthread_create (sibling-runtime
     * spawn); leave them NULL here when binding for a guest that doesn't
     * actually create threads — the mutex/cond/rwlock/etc. handlers don't
     * touch master/wasm. */
    if (!env) return NULL;
    if (per_thread_stack_bytes == 0) per_thread_stack_bytes = 64 * 1024;

    pthread_once (&g_ht_key_once, g_ht_key_init);

    yos_pthread_host * h = (yos_pthread_host *) calloc (1, sizeof *h);
    if (!h) return NULL;

    h->env              = env;
    h->master           = master;
    h->wasm             = wasm;
    h->wasm_len         = wasm_len;
    h->per_thread_stack = per_thread_stack_bytes;
    h->tls_pool_base    = tls_pool_base;
    h->tls_arena_size   = tls_arena_size;
    atomic_store (&h->next_key, 0u);

    pthread_mutex_init (&h->setup_mu,   NULL);
    pthread_mutex_init (&h->threads_mu, NULL);

    // Reserve tid 0 for the wasm "main" caller (the thread that runs the
    // exported `run` function on the master runtime). pthread_self() etc.
    // will see this for the main thread.
    struct host_thread * mht = (struct host_thread *) calloc (1, sizeof *mht);
    if (!mht) { free (h); return NULL; }
    mht->host      = h;
    mht->tid       = 0;
    mht->is_master = true;
    mht->tls_arena = tls_arena_size ? tls_pool_base : 0u;  // master gets slot 0
    h->threads[0]  = mht;
    pthread_setspecific (g_ht_key, mht);

    return h;
}

void
yos_pthread_host_destroy (yos_pthread_host * h)
{
    if (!h) return;
    if (h->threads[0]) { free (h->threads[0]); h->threads[0] = NULL; }
    pthread_mutex_destroy (&h->setup_mu);
    pthread_mutex_destroy (&h->threads_mu);
    free (h);
}

M3Result
yos_pthread_host_link (yos_pthread_host * h, IM3Module mod)
{
    if (!h || !mod) return "yos_pthread_host_link: invalid args";
    pthread_mutex_lock (&h->setup_mu);
    M3Result r = host_link_imports (h, mod);
    pthread_mutex_unlock (&h->setup_mu);
    return r;
}

uint32_t
yos_pthread_host_get_tls_arena (yos_pthread_host * h, uint32_t tid)
{
    if (!h || tid >= WASM3MT_MAX_THREADS) return 0u;
    pthread_mutex_lock (&h->threads_mu);
    struct host_thread * ht = h->threads[tid];
    uint32_t arena = ht ? ht->tls_arena : 0u;
    pthread_mutex_unlock (&h->threads_mu);
    return arena;
}
