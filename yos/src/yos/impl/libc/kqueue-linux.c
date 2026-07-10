/* impl/libc/kqueue-linux.c — Linux-host epoll-backed emulation of the
 * FreeBSD kqueue / kevent surface. nvim and libuv on the FreeBSD-
 * shaped guest call kqueue() / kevent() to drive their event loop;
 * with no implementation the loop never initialises and nvim can't
 * spawn its child server (E903).
 *
 * Scope:
 *   - kqueue / kqueue1 / kqueuex all create a host epoll fd, returned
 *     to the guest as a regular wfd. EV_RECEIPT and friends are
 *     accepted but mostly ignored.
 *   - kevent translates EVFILT_READ / EVFILT_WRITE / EV_SIGNAL to the
 *     equivalent epoll registrations + epoll_wait.
 *   - timeout: NULL → block forever (passes -1 to epoll_wait); ts
 *     pointer → convert FreeBSD timespec (int64 sec + long nsec) to ms.
 *   - changelist + eventlist may both be present (the typical libuv
 *     usage); we apply changes first, then wait + populate events.
 *
 * What is NOT supported (returns kevent EV_ERROR or skips):
 *   - EVFILT_TIMER, EVFILT_VNODE, EVFILT_PROC, EVFILT_AIO, EVFILT_USER,
 *     EVFILT_FS, EVFILT_LIO, EVFILT_NETDEV. libuv mostly uses
 *     EVFILT_READ/WRITE/SIGNAL for unix-style apps.
 *   - EV_ONESHOT/EV_CLEAR semantics: epoll's edge-trigger is close but
 *     not identical; we approximate.
 *
 * Hooked into hooks.yaml under runtime_owned so bridge.py emits no
 * conflicting body. main.c calls yos_kqueue_link() to bind us.
 *
 * NO #ifdef inside this file — meson selects it only on linux hosts.
 * darwin / freebsd hosts use kqueue-darwin.c (native passthrough).
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

#include <sys/epoll.h>
#include <sys/syscall.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#include "wasm3.h"
#include "m3_env.h"
#include "platform.h"
#include "yos/types.h"
#include <yos/ytrace/ytrace.h>

extern int yos_remap_errno_h2g(int);
extern int yos_fd_alloc(struct yos_exec_ctx *ctx, int host_fd);
extern int yos_fd_get  (struct yos_exec_ctx *ctx, int wasm_fd);

/* FreeBSD kevent constants (from sys/event.h, i386 wasm32 view). */
#define EVFILT_READ        (-1)
#define EVFILT_WRITE       (-2)
#define EVFILT_PROC        (-5)
#define EVFILT_SIGNAL      (-6)
#define EV_ADD             0x0001
#define EV_DELETE          0x0002
#define EV_ENABLE          0x0004
#define EV_DISABLE         0x0008
#define EV_ONESHOT         0x0010
#define EV_CLEAR           0x0020
#define EV_RECEIPT         0x0040
#define EV_DISPATCH        0x0080
#define EV_ERROR           0x4000
#define EV_EOF             0x8000

/* EVFILT_PROC fflags */
#define NOTE_EXIT          0x80000000

/* Per-kqueue map: guest pid → eventfd we registered with epoll for that
 * pid's exit. When yos_exit's child branch fires (proc.c), it walks
 * yos_proc_table and writes 1 to the matching eventfd, which makes
 * the parent's epoll_wait return with the watcher event so libuv's
 * uv__wait_children path picks it up via waitpid (yos_waitpid). */
#include <sys/eventfd.h>
struct proc_watch { int kq; int eventfd; uint32_t pid; uint32_t udata; };
#define MAX_PROC_WATCH 64
static struct proc_watch g_proc_watches[MAX_PROC_WATCH];
static pthread_mutex_t   g_proc_watches_lock = PTHREAD_MUTEX_INITIALIZER;

/* Called by impl/proc.c when a guest child exits. Wakes the parent's
 * libuv kqueue so its EVFILT_PROC | NOTE_EXIT watcher fires. */
void yos_kqueue_notify_exit(uint32_t pid)
{
    pthread_mutex_lock(&g_proc_watches_lock);
    for (int i = 0; i < MAX_PROC_WATCH; i++) {
        if (g_proc_watches[i].pid == pid && g_proc_watches[i].eventfd > 0) {
            uint64_t one = 1;
            ssize_t w = write(g_proc_watches[i].eventfd, &one, 8);
            (void)w;
        }
    }
    pthread_mutex_unlock(&g_proc_watches_lock);
}

static int proc_watch_add(int kq, uint32_t pid, uint32_t udata)
{
    pthread_mutex_lock(&g_proc_watches_lock);
    for (int i = 0; i < MAX_PROC_WATCH; i++) {
        if (g_proc_watches[i].eventfd < 0 ||
            g_proc_watches[i].eventfd == 0) {
            int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            if (efd < 0) { pthread_mutex_unlock(&g_proc_watches_lock); return -1; }
            struct epoll_event eev = {0};
            eev.events = EPOLLIN;
            /* Pack tag identical to read/write events but with EVFILT_PROC
             * filter so kevent unpack reports the right filter. ident =
             * the guest pid (libuv reads ident to identify which child). */
            eev.data.u64 = ((uint64_t)(udata & 0xffff) << 48) |
                           ((uint64_t)(uint16_t)EVFILT_PROC << 32) |
                           (uint64_t)pid;
            if (epoll_ctl(kq, EPOLL_CTL_ADD, efd, &eev) < 0) {
                close(efd);
                pthread_mutex_unlock(&g_proc_watches_lock);
                return -1;
            }
            g_proc_watches[i].kq = kq;
            g_proc_watches[i].eventfd = efd;
            g_proc_watches[i].pid = pid;
            g_proc_watches[i].udata = udata;
            pthread_mutex_unlock(&g_proc_watches_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_proc_watches_lock);
    return -1;
}

static void proc_watch_remove(int kq, uint32_t pid)
{
    pthread_mutex_lock(&g_proc_watches_lock);
    for (int i = 0; i < MAX_PROC_WATCH; i++) {
        if (g_proc_watches[i].kq == kq && g_proc_watches[i].pid == pid &&
            g_proc_watches[i].eventfd > 0) {
            epoll_ctl(kq, EPOLL_CTL_DEL, g_proc_watches[i].eventfd, NULL);
            close(g_proc_watches[i].eventfd);
            g_proc_watches[i].eventfd = -1;
            g_proc_watches[i].kq = -1;
            g_proc_watches[i].pid = 0;
        }
    }
    pthread_mutex_unlock(&g_proc_watches_lock);
}

/* FreeBSD struct kevent layout in wasm32 view. Even though we tell
 * clang `-D__i386__=1` for the FreeBSD headers, clang's wasm32 ABI
 * aligns int64_t / uint64_t to 8 bytes — NOT 4 as on real i386. So
 * `data` and `ext[4]` get 8-aligned with padding:
 *
 *   uintptr_t   ident       offset  0,  4 bytes
 *   short       filter      offset  4,  2 bytes
 *   ushort      flags       offset  6,  2 bytes
 *   uint        fflags      offset  8,  4 bytes
 *   (4 bytes padding to 8-align data)
 *   int64_t     data        offset 16,  8 bytes
 *   void *      udata       offset 24,  4 bytes
 *   (4 bytes padding to 8-align ext)
 *   uint64_t    ext[4]      offset 32, 32 bytes
 *   total = 64 bytes
 *
 * Earlier hand-counted 56 bytes (assuming i386-style 4-aligned
 * int64) — we wrote 8 bytes past each slot, which scribbled the
 * stack canary on libuv's per-poll kevent array and tripped
 * __stack_chk_fail. */
#define KE_SZ          64
#define KE_IDENT_OFF    0
#define KE_FILTER_OFF   4
#define KE_FLAGS_OFF    6
#define KE_FFLAGS_OFF   8
#define KE_DATA_OFF    16
#define KE_UDATA_OFF   24

static inline uint32_t ke_ident (const uint8_t *p) { uint32_t v; memcpy(&v, p+KE_IDENT_OFF, 4); return v; }
static inline int16_t  ke_filter(const uint8_t *p) { int16_t  v; memcpy(&v, p+KE_FILTER_OFF, 2); return v; }
static inline uint16_t ke_flags (const uint8_t *p) { uint16_t v; memcpy(&v, p+KE_FLAGS_OFF, 2); return v; }
static inline uint32_t ke_fflags(const uint8_t *p) { uint32_t v; memcpy(&v, p+KE_FFLAGS_OFF, 4); return v; }
static inline uint32_t ke_udata (const uint8_t *p) { uint32_t v; memcpy(&v, p+KE_UDATA_OFF, 4); return v; }

static inline void ke_set_filter(uint8_t *p, int16_t  v) { memcpy(p+KE_FILTER_OFF, &v, 2); }
static inline void ke_set_flags (uint8_t *p, uint16_t v) { memcpy(p+KE_FLAGS_OFF,  &v, 2); }
static inline void ke_set_fflags(uint8_t *p, uint32_t v) { memcpy(p+KE_FFLAGS_OFF, &v, 4); }
static inline void ke_set_data  (uint8_t *p, int64_t  v) { memcpy(p+KE_DATA_OFF,   &v, 8); }
static inline void ke_set_udata (uint8_t *p, uint32_t v) { memcpy(p+KE_UDATA_OFF,  &v, 4); }
static inline void ke_set_ident (uint8_t *p, uint32_t v) { memcpy(p+KE_IDENT_OFF,  &v, 4); }

static inline void write_errno(struct yos_exec_ctx *ctx, int e)
{
    if (ctx && ctx->memory && ctx->errno_off) {
        *(int *)(ctx->memory + ctx->errno_off) = yos_remap_errno_h2g(e);
    }
}

/* kqueue() — return a wasm fd pointing at a host epoll instance. */
static m3ApiRawFunction(m3_yos_kqueue)
{
    m3ApiReturnType(int32_t);
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    int hfd = epoll_create1(EPOLL_CLOEXEC);
    if (hfd < 0) {
        write_errno(ctx, errno);
        m3ApiReturn(-1);
    }
    int wfd = yos_fd_alloc(ctx, hfd);
    if (wfd < 0) {
        close(hfd);
        write_errno(ctx, EMFILE);
        m3ApiReturn(-1);
    }
    ydebug("kqueue() -> wfd=%d (hfd=%d)\n", wfd, hfd);
    m3ApiReturn(wfd);
}

/* kqueue1(int flags) — same as kqueue, flags ignored (we always set
 * CLOEXEC). */
static m3ApiRawFunction(m3_yos_kqueue1)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, flags);
    (void)flags;
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    int hfd = epoll_create1(EPOLL_CLOEXEC);
    if (hfd < 0) { write_errno(ctx, errno); m3ApiReturn(-1); }
    int wfd = yos_fd_alloc(ctx, hfd);
    if (wfd < 0) { close(hfd); write_errno(ctx, EMFILE); m3ApiReturn(-1); }
    m3ApiReturn(wfd);
}

/* kqueuex(unsigned flags) — FreeBSD 14 spelling, same shape. */
static m3ApiRawFunction(m3_yos_kqueuex)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, flags);
    (void)flags;
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    int hfd = epoll_create1(EPOLL_CLOEXEC);
    if (hfd < 0) { write_errno(ctx, errno); m3ApiReturn(-1); }
    int wfd = yos_fd_alloc(ctx, hfd);
    if (wfd < 0) { close(hfd); write_errno(ctx, EMFILE); m3ApiReturn(-1); }
    m3ApiReturn(wfd);
}

/* kevent(kq, changelist, nchanges, eventlist, nevents, timeout) */
static m3ApiRawFunction(m3_yos_kevent)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t,  kq_wfd);
    m3ApiGetArg(uint32_t, changelist);
    m3ApiGetArg(int32_t,  nchanges);
    m3ApiGetArg(uint32_t, eventlist);
    m3ApiGetArg(int32_t,  nevents);
    m3ApiGetArg(uint32_t, timeout_off);

    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    uint32_t mem_size = 0;
    ctx->memory = m3_GetMemory(runtime, &mem_size, 0);
    ctx->memory_size = mem_size;
    static int kevent_call_n = 0;
    int my_call = ++kevent_call_n;
    if (ytrace_default_enabled() && my_call < 30) {
        pid_t tid = yos_plat_gettid();
        ydebug("kevent#%d(tid=%d kq=%d nchanges=%d nevents=%d timeout=%s)\n",
               my_call, (int)tid, kq_wfd, nchanges, nevents,
               timeout_off ? "ts" : "BLOCK");
    }

    int kq = yos_fd_get(ctx, kq_wfd);
    if (kq < 0) { write_errno(ctx, EBADF); m3ApiReturn(-1); }

    /* Reject negative counts. Without this, `nchanges < 0` skipped
     * the apply-loop silently but `nevents < 0` reached epoll_wait
     * which interprets it as "block forever for any non-positive"
     * — masking real bugs. */
    if (nchanges < 0 || nevents < 0) {
        write_errno(ctx, EINVAL); m3ApiReturn(-1);
    }
    /* Pre-validate the FULL changelist + eventlist ranges in 64-bit
     * up front. Wrap-prone `off + i*KE_SZ` checks inside the loop
     * could pass on bogus inputs where the addition overflowed back
     * into the valid range. */
    if (nchanges > 0) {
        if (changelist == 0 ||
            (uint64_t)changelist + (uint64_t)nchanges * KE_SZ > (uint64_t)mem_size) {
            write_errno(ctx, EFAULT); m3ApiReturn(-1);
        }
    }
    if (nevents > 0) {
        if (eventlist == 0 ||
            (uint64_t)eventlist + (uint64_t)nevents * KE_SZ > (uint64_t)mem_size) {
            write_errno(ctx, EFAULT); m3ApiReturn(-1);
        }
    }

    /* Apply changelist. Each kevent describes a registration change. */
    for (int i = 0; i < nchanges; i++) {
        uint8_t *ke = ctx->memory + changelist + (uint32_t)i * KE_SZ;
        uint32_t ident   = ke_ident(ke);
        int16_t  filter  = ke_filter(ke);
        uint16_t flags   = ke_flags(ke);
        uint32_t fflags  = ke_fflags(ke);
        uint32_t udata   = ke_udata(ke);

        /* Resolve fd-style ident (READ/WRITE) to a host fd. SIGNAL
         * idents stay as signal numbers. */
        int target_hfd = -1;
        uint32_t ev_mask = 0;
        if (filter == EVFILT_READ)  ev_mask = EPOLLIN;
        if (filter == EVFILT_WRITE) ev_mask = EPOLLOUT;
        if (flags & EV_CLEAR)       ev_mask |= EPOLLET;

        if (filter == EVFILT_READ || filter == EVFILT_WRITE) {
            target_hfd = yos_fd_get(ctx, (int32_t)ident);
            if (target_hfd < 0) {
                /* Bad fd in change. POSIX: report EV_ERROR back into the
                 * eventlist for this change. We just skip; libuv handles. */
                continue;
            }

            struct epoll_event eev;
            /* Pack: low 32 bits = guest wfd (so we can put it back in
             * ident on the way out — libuv keys events on ident, not
             * udata, so returning ident=0 makes libuv think no
             * watcher matches and busy-loop on the same event). Next
             * 16 bits = filter. Top 16 bits = (truncated) udata. */
            eev.data.u64 = ((uint64_t)(udata & 0xffff) << 48) |
                           ((uint64_t)(uint16_t)(filter & 0xffff) << 32) |
                           (uint64_t)(uint32_t)ident;
            eev.events = ev_mask;

            int op = -1;
            if (flags & EV_ADD)       op = EPOLL_CTL_ADD;
            else if (flags & EV_DELETE) op = EPOLL_CTL_DEL;
            else if (flags & EV_ENABLE) op = EPOLL_CTL_MOD;
            else if (flags & EV_DISABLE) op = EPOLL_CTL_DEL;

            if (op >= 0) {
                int r = epoll_ctl(kq, op, target_hfd, &eev);
                if (r < 0 && errno == EEXIST && op == EPOLL_CTL_ADD) {
                    /* Already registered — modify instead. */
                    epoll_ctl(kq, EPOLL_CTL_MOD, target_hfd, &eev);
                }
            }
        } else if (filter == EVFILT_PROC) {
            /* libuv on __FreeBSD__ uses EVFILT_PROC|NOTE_EXIT instead of
             * SIGCHLD to detect child exit. ident is the guest pid. We
             * keep an eventfd per (kq, pid) registered with epoll;
             * impl/proc.c yos_exit pokes the eventfd when the child
             * pthread terminates. */
            (void)fflags;  /* assume NOTE_EXIT; that's all libuv asks for */
            if (flags & EV_ADD) {
                if (proc_watch_add(kq, ident, udata) < 0) {
                    ydebug("kevent: proc_watch_add(pid=%u) failed errno=%d\n",
                           ident, errno);
                }
            } else if (flags & (EV_DELETE | EV_DISABLE)) {
                proc_watch_remove(kq, ident);
            }
            continue;
        } else if (filter == EVFILT_SIGNAL) {
            /* Signals via kqueue → not supported here; libuv's signal
             * loop has its own pipe-based path that doesn't actually
             * need this to work. Skip silently. */
            continue;
        } else {
            /* Unsupported filter — silently skip. */
            ydebug("kevent: unsupported filter=%d, skipping\n", filter);
            continue;
        }
    }

    /* No events requested → return 0 (just-applied-changes mode). */
    if (nevents <= 0) m3ApiReturn(0);

    /* Compute timeout in ms. NULL pointer → block forever. */
    int timeout_ms = -1;
    if (timeout_off) {
        /* timespec on FreeBSD wasm32 is 12 bytes (int64 sec + int32 nsec). */
        if ((uint64_t)timeout_off + 12ULL > (uint64_t)mem_size) {
            write_errno(ctx, EFAULT); m3ApiReturn(-1);
        }
        int64_t  tv_sec;
        int32_t  tv_nsec;
        memcpy(&tv_sec,  ctx->memory + timeout_off + 0, 8);
        memcpy(&tv_nsec, ctx->memory + timeout_off + 8, 4);
        if (tv_sec == 0 && tv_nsec == 0) {
            timeout_ms = 0;
        } else {
            int64_t ms = tv_sec * 1000 + tv_nsec / 1000000;
            if (ms < 0) ms = 0;
            if (ms > 0x7fffffff) ms = 0x7fffffff;
            timeout_ms = (int)ms;
        }
    }

    /* Allocate scratch on the stack — bounded by libuv's typical 32. */
    if (nevents > 256) nevents = 256;
    struct epoll_event eevs[256];
    int n = epoll_wait(kq, eevs, nevents, timeout_ms);
    if (n < 0) { write_errno(ctx, errno); m3ApiReturn(-1); }
    if (ytrace_default_enabled() && my_call < 30) {
        for (int i = 0; i < n && i < 4; i++) {
            ydebug("  epoll_event[%d]: events=0x%x udata=%016lx\n",
                   i, eevs[i].events, (unsigned long)eevs[i].data.u64);
        }
        ydebug("kevent#%d returned %d events\n", my_call, n);
    }

    /* Marshal back into FreeBSD kevent structs in eventlist. The
     * eventlist range was pre-validated in 64-bit at function entry
     * for the user-supplied `nevents`; epoll_wait clamps `n` to that,
     * so the per-entry indexing is safe without a re-check here. */
    for (int i = 0; i < n; i++) {
        uint8_t *ke = ctx->memory + eventlist + (uint32_t)i * KE_SZ;
        memset(ke, 0, KE_SZ);
        uint64_t tag = eevs[i].data.u64;
        uint32_t ident_w = (uint32_t)(tag & 0xffffffff);
        int16_t  filter  = (int16_t)((tag >> 32) & 0xffff);
        uint32_t udata   = (uint32_t)(tag >> 48);
        uint32_t fflags_out = 0;
        if (filter == EVFILT_PROC) {
            /* libuv on __FreeBSD__ keys process watchers by udata (a
             * pointer to its uv_process_t), not by ident. Our 16-bit
             * udata-in-tag packing truncates the pointer, so we must
             * recover the full 32-bit udata from the proc_watch table
             * here. Also EV_ONESHOT semantics: kqueue auto-removes the
             * registration after the first fire — our impl must do the
             * same so libuv's loop->nfds bookkeeping stays in sync
             * (uv__wait_children does loop->nfds--, expecting EV_ONESHOT
             * to have already removed the kqueue entry). */
            pthread_mutex_lock(&g_proc_watches_lock);
            for (int j = 0; j < MAX_PROC_WATCH; j++) {
                if (g_proc_watches[j].kq == kq &&
                    g_proc_watches[j].pid == ident_w &&
                    g_proc_watches[j].eventfd > 0) {
                    udata = g_proc_watches[j].udata;
                    epoll_ctl(kq, EPOLL_CTL_DEL,
                              g_proc_watches[j].eventfd, NULL);
                    close(g_proc_watches[j].eventfd);
                    g_proc_watches[j].eventfd = -1;
                    g_proc_watches[j].kq = -1;
                    g_proc_watches[j].pid = 0;
                    break;
                }
            }
            pthread_mutex_unlock(&g_proc_watches_lock);
            fflags_out = NOTE_EXIT;
        }
        /* libuv (uv__io_poll) reads ev->ident to identify which fd
         * fired, then walks its `loop->watchers[fd]` to find the
         * watcher. Returning ident=0 makes it spin on every poll. */
        ke_set_ident(ke, ident_w);
        ke_set_filter(ke, filter);
        ke_set_flags(ke, (eevs[i].events & EPOLLERR) ? EV_ERROR : 0);
        ke_set_fflags(ke, fflags_out);
        ke_set_data(ke, 0);
        ke_set_udata(ke, udata);
    }
    m3ApiReturn(n);
}

void yos_kqueue_link(IM3Module mod)
{
    m3_LinkRawFunction(mod, "env", "kqueue",  "i()",       m3_yos_kqueue);
    m3_LinkRawFunction(mod, "env", "kqueue1", "i(i)",      m3_yos_kqueue1);
    m3_LinkRawFunction(mod, "env", "kqueuex", "i(i)",      m3_yos_kqueuex);
    m3_LinkRawFunction(mod, "env", "kevent",  "i(iiiiii)", m3_yos_kevent);
}

