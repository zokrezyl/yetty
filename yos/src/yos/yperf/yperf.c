/*
 * yperf — per-call profile recorder. See include/yos/yperf/yperf.h
 * for the full overview.
 *
 * Implementation notes:
 *
 *   Per-thread ring: __thread struct yperf_thread keeps a 64K-record
 *   ring buffer + a 256-frame call stack. enter pushes; exit pops
 *   and writes one record. Ring overflow drops the oldest record
 *   (head wraps); the overflow counter goes into the file header
 *   so the reader knows how much was lost.
 *
 *   Symbol table: yperf_register_name stores (pc, name) into a
 *   global hash. yperf_dump walks the hash once and appends the
 *   table to the output file. Records reference pc; reader joins.
 *
 *   Output file layout (little-endian):
 *
 *      struct yperf_file_header {
 *          char     magic[8];      "YPERFv01"
 *          uint64_t header_size;
 *          uint64_t num_records;
 *          uint64_t num_symbols;
 *          uint64_t records_offset;
 *          uint64_t symbols_offset;
 *          uint64_t overflow_count;
 *      };
 *      record[num_records]:   24 bytes each
 *          uint64_t enter_ts_ns
 *          uint64_t exit_ts_ns
 *          uintptr_t fn_pc
 *      symbol[num_symbols]:
 *          uint32_t  name_len
 *          uintptr_t fn_pc
 *          char      name[name_len]
 *
 *   No locks on the hot path; thread state is __thread. The symbol
 *   table is locked once on each first-time registration with a
 *   tiny mutex — first-time only, no recurring cost.
 */

#include "yos/yperf/yperf.h"

#if YPERF_C_ENABLED

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <yos/ytrace/ytrace.h>   /* ywarn for status lines */

/* ── tunables ─────────────────────────────────────────────────────── */
/* Default per-thread ring size. Tunable at runtime via YPERF_RING_SIZE
 * (records, NOT bytes). Each record is 24 B, so 1M records ≈ 24 MB
 * per host thread — enough for the ~1.3M wasm calls a `nvim --headless
 * +q` startup makes. For really big captures (full editor session,
 * test suites) set YPERF_RING_SIZE=8000000 (≈ 192 MB/thread) or
 * higher. Overflow drops oldest records and increments the
 * overflow_count counter shown in `yperf report`. */
#define YPERF_RING_DEFAULT (1024u * 1024u)   /* 1M records ≈ 24 MB/thread */
#define YPERF_STACK_DEPTH  256
#define YPERF_SYMTAB_SIZE  16384              /* open-addressing capacity */

/* Per-thread ring. linked into a global list at first use so
 * yperf_dump can walk all of them on shutdown. */
/* On-disk record layout. Explicit uint64_t for fn_handle (not
 * uintptr_t!) because the file is read by wasm32 tools where
 * pointers are 4 bytes — using uintptr_t for the on-disk type
 * would silently re-layout the struct depending on the reader's
 * pointer size and corrupt every parse. */
struct yperf_record {
    uint64_t enter_ts_ns;
    uint64_t exit_ts_ns;
    uint64_t fn_handle;
};
_Static_assert(sizeof(struct yperf_record) == 24, "yperf record layout");

struct yperf_frame {
    uint64_t fn_handle;
    uint64_t enter_ts_ns;
};

struct yperf_thread {
    struct yperf_record *ring;     /* heap-alloc, size = g_ring_size */
    size_t               ring_size;
    size_t               head;     /* next write index */
    uint64_t             written;  /* total records ever written */
    struct yperf_frame   stack[YPERF_STACK_DEPTH];
    int                  depth;
    pid_t                tid;
    /* singly linked list pointer; protected by g_thread_list_lock. */
    struct yperf_thread *next;
};

/* ── globals ─────────────────────────────────────────────────────── */

_Atomic bool g_yperf_enabled = false;     /* exported — read inline by wasm3 */
static _Atomic bool g_yperf_initialised = false;
static const char  *g_yperf_file = NULL;
static size_t       g_ring_size  = YPERF_RING_DEFAULT;
static yperf_walker_fn g_walker = NULL;

static pthread_mutex_t g_thread_list_lock = PTHREAD_MUTEX_INITIALIZER;
static struct yperf_thread *g_threads = NULL;

/* Persistent symbol table — populated lazily on first yperf_enter
 * for each fn so symbols survive the originating proc's death.
 * Open-addressing hash, sized 2× expected to keep load factor low.
 * Fast path: read slot, if it matches the queried fn → done. Only
 * the WRITE side (first sighting) takes the lock. */
#define YPERF_SYMS_CAP  16384u
struct yperf_sym {
    _Atomic uintptr_t fn_handle;
    const char       *name;     /* owned via strdup */
};
static struct yperf_sym g_syms[YPERF_SYMS_CAP];
static pthread_mutex_t  g_syms_lock = PTHREAD_MUTEX_INITIALIZER;

static inline size_t yperf_sym_slot(uintptr_t h)
{
    /* Hash by a few mid-significant bits — M3Function* pointers are
     * heap-aligned so the low 4-6 bits are zero. */
    return (size_t)((h >> 4) * 0x9E3779B1u) & (YPERF_SYMS_CAP - 1);
}

/* Lazily insert (fn, name) into the symbol table. Returns immediately
 * if fn is already registered (lock-free fast path). */
static void yperf_sym_register(uintptr_t fn, const char *name)
{
    if (!fn || !name) return;
    size_t i = yperf_sym_slot(fn);
    /* Lock-free lookup: linear probe, stop on match or empty slot. */
    for (size_t step = 0; step < YPERF_SYMS_CAP; step++) {
        size_t idx = (i + step) & (YPERF_SYMS_CAP - 1);
        uintptr_t cur = atomic_load_explicit(&g_syms[idx].fn_handle,
                                              memory_order_acquire);
        if (cur == fn) return;          /* already registered */
        if (cur == 0) break;             /* candidate empty slot — fall through */
    }
    /* Slow path: take the lock and CAS into the first empty slot. */
    pthread_mutex_lock(&g_syms_lock);
    for (size_t step = 0; step < YPERF_SYMS_CAP; step++) {
        size_t idx = (i + step) & (YPERF_SYMS_CAP - 1);
        uintptr_t cur = atomic_load_explicit(&g_syms[idx].fn_handle,
                                              memory_order_relaxed);
        if (cur == fn) break;            /* raced — someone else won */
        if (cur == 0) {
            g_syms[idx].name = strdup(name);
            atomic_store_explicit(&g_syms[idx].fn_handle, fn,
                                   memory_order_release);
            break;
        }
    }
    pthread_mutex_unlock(&g_syms_lock);
}

/* ── per-thread state access ─────────────────────────────────────── */

static __thread struct yperf_thread *t_self = NULL;

static pid_t yperf_gettid(void)
{
#ifdef __linux__
    return (pid_t)syscall(SYS_gettid);
#else
    return (pid_t)getpid();
#endif
}

static struct yperf_thread *yperf_self(void)
{
    struct yperf_thread *t = t_self;
    if (t) return t;
    t = calloc(1, sizeof *t);
    if (!t) return NULL;
    /* Re-read YPERF_RING_SIZE every alloc — the guest-side wrapper
     * sets it AFTER yperf_init has already run (init happens at host
     * startup, before any wasm exists), so the cached g_ring_size
     * may be stale. This lookup happens once per new host thread
     * (= once per fork), not on the hot path. */
    size_t want = g_ring_size;
    const char *rs = getenv("YPERF_RING_SIZE");
    if (rs) {
        unsigned long long n = strtoull(rs, NULL, 10);
        if (n >= 1024) want = (size_t)n;
    }
    t->ring_size = want;
    t->ring = calloc(t->ring_size, sizeof *t->ring);
    if (!t->ring) {
        /* Fall back to the default — better to lose the YPERF_RING_SIZE
         * tuning than to silently drop every record from this thread. */
        t->ring_size = YPERF_RING_DEFAULT;
        t->ring = calloc(t->ring_size, sizeof *t->ring);
        if (!t->ring) { free(t); return NULL; }
    }
    t->tid = yperf_gettid();
    pthread_mutex_lock(&g_thread_list_lock);
    t->next = g_threads;
    g_threads = t;
    pthread_mutex_unlock(&g_thread_list_lock);
    t_self = t;
    return t;
}

static inline uint64_t yperf_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ── public API ──────────────────────────────────────────────────── */

void yperf_init(void)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_yperf_initialised, &expected, true))
        return;
    const char *e = getenv("YPERF");
    if (e && (strcmp(e, "yes") == 0 || strcmp(e, "1") == 0 ||
              strcmp(e, "true") == 0)) {
        atomic_store(&g_yperf_enabled, true);
    }
    /* YPERF_RING_SIZE: per-thread ring capacity in RECORDS (not bytes).
     * Clamped to a minimum of 1024 so a typo can't disable capture
     * entirely. Each record is 24 B so set it generously — capturing
     * an entire nvim startup needs ~2M; an editing session 10M+. */
    const char *rs = getenv("YPERF_RING_SIZE");
    if (rs) {
        unsigned long long n = strtoull(rs, NULL, 10);
        if (n < 1024) n = 1024;
        g_ring_size = (size_t)n;
    }
    g_yperf_file = getenv("YPERF_FILE");
    atexit(yperf_dump);
}

void yperf_set_enabled(bool on)
{
    if (!atomic_load(&g_yperf_initialised)) yperf_init();
    atomic_store(&g_yperf_enabled, on);
}

bool yperf_is_enabled(void)
{
    return atomic_load_explicit(&g_yperf_enabled, memory_order_relaxed);
}

void yperf_enter(yperf_fn_t fn, const char *name)
{
    /* Caller (wasm3 op_Entry) already gated on g_yperf_enabled —
     * just push the frame. The very first call from a host thread
     * lazily allocates the per-thread state; subsequent ones hit the
     * cached TLS pointer. We also lazily register the function's
     * name so the dumper can produce a symbol table even after the
     * originating proc has been reaped (which makes the proc-walker
     * approach insufficient). The fast path is lock-free for
     * already-registered fns. */
    yperf_sym_register((uintptr_t)fn, name);
    struct yperf_thread *t = t_self ? t_self : yperf_self();
    if (!t) return;
    if (t->depth >= YPERF_STACK_DEPTH) {
        /* stack overflow — drop this enter; the matching exit will
         * pop a frame from below it (unbalanced), but at worst we
         * mis-attribute one record. Better than corrupting the ring. */
        t->depth++;
        return;
    }
    t->stack[t->depth].fn_handle   = (uintptr_t)fn;
    t->stack[t->depth].enter_ts_ns = yperf_now_ns();
    t->depth++;
}

void yperf_exit(void)
{
    struct yperf_thread *t = t_self;
    if (!t || t->depth <= 0) return;
    if (t->depth > YPERF_STACK_DEPTH) {
        t->depth--;     /* paired with dropped enter */
        return;
    }
    t->depth--;
    struct yperf_frame *f = &t->stack[t->depth];
    struct yperf_record *r = &t->ring[t->head];
    r->enter_ts_ns = f->enter_ts_ns;
    r->exit_ts_ns  = yperf_now_ns();
    r->fn_handle   = f->fn_handle;
    t->head = (t->head + 1) % t->ring_size;
    t->written++;
}

void yperf_set_walker(yperf_walker_fn w)
{
    g_walker = w;
}

/* ── dumper ──────────────────────────────────────────────────────── */

/* Mutable state used by the walker callback. yperf_dump runs once
 * from atexit; no concurrency. */
static int      g_dump_fd     = -1;
static int      g_dump_failed = 0;
static uint64_t g_dump_syms   = 0;

static void emit_sym(yperf_fn_t fn, const char *name)
{
    if (g_dump_failed || !name) return;
    uint32_t nlen   = (uint32_t)strlen(name);
    uint64_t handle = (uint64_t)(uintptr_t)fn;   /* always-8B on disk */
    if (write(g_dump_fd, &nlen,   sizeof nlen)   != (ssize_t)sizeof nlen   ||
        write(g_dump_fd, &handle, sizeof handle) != (ssize_t)sizeof handle ||
        write(g_dump_fd, name,    nlen)          != (ssize_t)nlen) {
        g_dump_failed = 1;
        return;
    }
    g_dump_syms++;
}

struct yperf_file_header {
    char     magic[8];
    uint64_t header_size;
    uint64_t num_records;
    uint64_t num_symbols;
    uint64_t records_offset;
    uint64_t symbols_offset;
    uint64_t overflow_count;
};

void yperf_dump(void)
{
    /* Disable further recording so iteration is stable. */
    bool was_on = atomic_load(&g_yperf_enabled);
    atomic_store(&g_yperf_enabled, false);
    if (!was_on) return;

    /* Re-read env every dump so a setenv("YPERF_FILE", ...) from the
     * guest-side wrapper (which happens after yperf_init has already
     * cached a possibly-NULL value) actually lands in the file path.
     * Default is "yperf.data" in the current working directory —
     * matches `perf.data` from linux perf. The host's cwd at exit
     * is wherever the user invoked yos from, so the file lands next
     * to where they ran `yperf record`. */
    g_yperf_file = getenv("YPERF_FILE");
    if (!g_yperf_file) g_yperf_file = "yperf.data";

    int fd = open(g_yperf_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "yperf: open(%s) failed: %s\n",
                g_yperf_file, strerror(errno));
        return;
    }

    /* Tally totals — every thread contributes min(written, ring_size). */
    uint64_t total_records = 0;
    uint64_t overflow = 0;
    pthread_mutex_lock(&g_thread_list_lock);
    for (struct yperf_thread *t = g_threads; t; t = t->next) {
        uint64_t n = (t->written < t->ring_size) ? t->written : t->ring_size;
        total_records += n;
        if (t->written > t->ring_size)
            overflow += (t->written - t->ring_size);
    }
    pthread_mutex_unlock(&g_thread_list_lock);

    /* num_symbols is filled in after the walker runs — write header,
     * then records, then symbols, then rewind+rewrite the header
     * with the symbol count once known. */
    struct yperf_file_header h = {
        .magic            = {'Y','P','E','R','F','v','0','1'},
        .header_size      = sizeof h,
        .num_records      = total_records,
        .num_symbols      = 0,
        .records_offset   = sizeof h,
        .symbols_offset   = sizeof h + total_records * sizeof(struct yperf_record),
        .overflow_count   = overflow,
    };
    if (write(fd, &h, sizeof h) != (ssize_t)sizeof h) goto io_fail;

    /* Records: for each thread, dump ring in chronological order
     * (head points at next-write; oldest is at head if it wrapped). */
    pthread_mutex_lock(&g_thread_list_lock);
    for (struct yperf_thread *t = g_threads; t; t = t->next) {
        size_t live = (t->written < t->ring_size) ? t->written : t->ring_size;
        size_t start = (t->written < t->ring_size) ? 0 : t->head;
        for (size_t k = 0; k < live; k++) {
            const struct yperf_record *r = &t->ring[(start + k) % t->ring_size];
            if (write(fd, r, sizeof *r) != (ssize_t)sizeof *r) {
                pthread_mutex_unlock(&g_thread_list_lock);
                goto io_fail;
            }
        }
    }
    pthread_mutex_unlock(&g_thread_list_lock);

    /* Symbols: walk the registered runtime via the emit callback.
     * Each emitted symbol becomes a (name_len:u32, fn_handle:u64,
     * name:bytes) record. The reader joins records on fn_handle.
     * The emit fn is at file scope (see emit_sym below); it reads
     * state from these globals because the public callback type
     * carries no closure pointer. yperf_dump is one-shot from
     * atexit; serial access is guaranteed. */
    g_dump_fd      = fd;
    g_dump_failed  = 0;
    g_dump_syms    = 0;

    /* Walk the lazy symbol table first — these names were captured
     * inline on first call so they're still valid even if the
     * originating proc has since been reaped. The proc-walker after
     * adds anything that's still live but never made it into the
     * lazy table (e.g. functions that were compiled but never
     * actually called during the capture window). yperf_sym_register
     * is idempotent on the writing side but the read side here just
     * dumps every entry — duplicates between the two passes are
     * harmless to the reader because both map to the same fn id. */
    for (size_t i = 0; i < YPERF_SYMS_CAP; i++) {
        uintptr_t h = atomic_load_explicit(&g_syms[i].fn_handle,
                                            memory_order_acquire);
        if (h && g_syms[i].name) emit_sym((const void *)h, g_syms[i].name);
    }
    if (g_walker) g_walker(emit_sym);
    if (g_dump_failed) goto io_fail;

    /* Rewrite header with the real symbol count. */
    h.num_symbols = g_dump_syms;
    if (lseek(fd, 0, SEEK_SET) != 0 ||
        write(fd, &h, sizeof h) != (ssize_t)sizeof h)
        goto io_fail;

    close(fd);
    fprintf(stderr, "yperf: wrote %llu records (%llu lost), %llu symbols → %s\n",
            (unsigned long long)total_records,
            (unsigned long long)overflow,
            (unsigned long long)g_dump_syms,
            g_yperf_file);
    return;

io_fail:
    fprintf(stderr, "yperf: write(%s) failed: %s\n",
            g_yperf_file, strerror(errno));
    if (fd >= 0) close(fd);
}

void yperf_dump_and_reset(void)
{
    /* Take a snapshot through yperf_dump first — that disables the
     * enable flag, walks rings, writes the file. After it returns
     * we clear ring state in every linked thread so the next
     * yperf_set_enabled(true) starts a clean capture. */
    yperf_dump();
    pthread_mutex_lock(&g_thread_list_lock);
    for (struct yperf_thread *t = g_threads; t; t = t->next) {
        t->head    = 0;
        t->written = 0;
        t->depth   = 0;
    }
    pthread_mutex_unlock(&g_thread_list_lock);
    /* Re-arm the YPERF_FILE pointer so the next capture re-reads
     * env (the wrapper may have set a different filename). */
    g_yperf_file = getenv("YPERF_FILE");
}

#endif /* YPERF_C_ENABLED */
