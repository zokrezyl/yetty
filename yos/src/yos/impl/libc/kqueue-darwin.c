/* impl/libc/kqueue-darwin.c — native kqueue passthrough for darwin
 * (and FreeBSD) hosts. Translates the guest's FreeBSD-shape kevent ↔
 * the host's struct kevent and lets the kernel multiplex.
 *
 * NO #ifdef inside this file — meson selects it only when the host
 * is darwin (or freebsd). Linux uses kqueue-linux.c (epoll-backed).
 * Shared comments + the cross-platform yos_proc_watches_* surface
 * live in kqueue.c.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

/* Darwin / FreeBSD host implementation: pass through to native kqueue
 *
 * Differences from the Linux/epoll-based body below:
 *   - kqueue()/kqueue1()/kqueuex() create a real host kqueue.
 *   - kevent() marshalls the guest's 64-byte wasm32 FreeBSD-shape
 *     kevent ↔ the host's 32-byte struct kevent and lets the kernel
 *     do the actual multiplexing.
 *   - Guest-pid exit notifications (EVFILT_PROC|NOTE_EXIT) can't use
 *     darwin's native EVFILT_PROC because guest "pids" are pthread
 *     tids, not host pids. Instead we register an EVFILT_USER on the
 *     host kqueue with a synthetic ident; impl/proc.c calls
 *     yos_kqueue_notify_exit() when a guest child exits, which fires
 *     NOTE_TRIGGER on that EVFILT_USER and the kevent loop turns the
 *     wake into a synthetic EVFILT_PROC|NOTE_EXIT for the guest.
 */
#include <stdio.h>
#include <sys/event.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/select.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdlib.h>
#include <semaphore.h>
#include <sys/ioctl.h>

#include "wasm3.h"
#include "m3_env.h"
#include "platform.h"
#include "yos/types.h"
#include <yos/ytrace/ytrace.h>

extern int yos_remap_errno_h2g(int);
extern int yos_fd_alloc(struct yos_exec_ctx *ctx, int host_fd);
extern int yos_fd_get  (struct yos_exec_ctx *ctx, int wasm_fd);

/* Hand-rolled m3ApiRawFunction wrappers don't go through the
 * codegen-emitted m3w_<name> trampolines, so we feed the crash-dump
 * ring buffer ourselves and emit a ytrace line per call. */
extern const char *yos_brg_last_call;
extern void yos_brg_record(const char *name);
extern void yos_brg_record_args(uint64_t,uint64_t,uint64_t,uint64_t);
#define KQ_TRACE(name) do {                                          \
    yos_brg_last_call = (name); yos_brg_record(name);                \
    ytrace("%s(...)", (name));                                       \
} while (0)
#define KQ_TRACE4(name, a, b, c, d) do {                             \
    yos_brg_last_call = (name); yos_brg_record(name);                \
    yos_brg_record_args((uint64_t)(a),(uint64_t)(b),                 \
                        (uint64_t)(c),(uint64_t)(d));                \
    ytrace("%s(0x%llx, 0x%llx, 0x%llx, 0x%llx)", (name),             \
           (unsigned long long)(a), (unsigned long long)(b),         \
           (unsigned long long)(c), (unsigned long long)(d));        \
} while (0)

/* Guest (FreeBSD-flavored) kevent constants. Same numeric values on
 * darwin/FreeBSD so we can pass filter/flags through unchanged for
 * the file-descriptor cases. */
#define G_EVFILT_READ        (-1)
#define G_EVFILT_WRITE       (-2)
#define G_EVFILT_PROC        (-5)
#define G_EVFILT_SIGNAL      (-6)
#define G_EV_ADD             0x0001
#define G_EV_DELETE          0x0002
#define G_EV_ENABLE          0x0004
#define G_EV_DISABLE         0x0008
#define G_EV_ONESHOT         0x0010
#define G_EV_CLEAR           0x0020
#define G_EV_RECEIPT         0x0040
#define G_EV_DISPATCH        0x0080
#define G_EV_ERROR           0x4000
#define G_EV_EOF             0x8000
#define G_NOTE_EXIT          0x80000000

/* ── TTY/EVFILT_READ workaround ────────────────────────────────────
 *
 * macOS' kqueue refuses EVFILT_READ on character-device fds (TTYs,
 * pty slaves) — kevent returns EV_ERROR with EINVAL. libuv handles
 * this on darwin via `uv__stream_try_select`: probe with a 1ns
 * kevent, and if it fails, run a select(2) loop in a worker thread
 * that pokes a self-pipe (which kqueue *can* watch) to wake up the
 * main loop, where libuv then read()s from the TTY directly.
 *
 * BUT — our nvim wasm is built `wasm32-unknown-unknown -D__i386__=1`,
 * so libuv's `#if defined(__APPLE__)` block is *not* compiled in.
 * The wasm libuv blindly registers TTY fds on our kqueue and treats
 * the EV_ERROR as "registration accepted but never fires" — the TUI
 * blocks in kevent forever, never reading user keystrokes.
 *
 * Fix: emulate libuv's darwin trick at the kevent-bridge layer.
 * When the guest tries to register EV_ADD/EVFILT_READ on a TTY fd,
 * intercept it: create a self-pipe, register the read end on the
 * host kqueue (pipes work), and spawn a select-thread that blocks
 * on the TTY fd and writes a wake byte to the pipe whenever it's
 * readable. On the kevent return path, translate notify-pipe events
 * back into synthetic EVFILT_READ events for the original guest fd
 * and post a semaphore so the select-thread can resume. */
struct tty_watcher {
    int             kq;            /* host kqueue this is registered on */
    uint32_t        guest_ident;   /* guest's wfd — what to return as ident */
    uint32_t        guest_udata;   /* guest's udata to echo */
    int             tty_fd;        /* host TTY fd we select(2) on */
    int             notify_rd;     /* self-pipe read end (registered on kq) */
    int             notify_wr;     /* self-pipe write end (select-thread) */
    pthread_t       thread;
    sem_t          *processed;     /* posted by guest after consuming event */
    int             stop;
};

#define MAX_TTY_WATCHERS 16
static struct tty_watcher *g_tty_watchers[MAX_TTY_WATCHERS];
static pthread_mutex_t     g_tty_watchers_lock = PTHREAD_MUTEX_INITIALIZER;

static void *tty_select_thread(void *arg)
{
    struct tty_watcher *w = (struct tty_watcher *)arg;
    int loops = 0;
    /* On entry, dump fd info so we can confirm tty_fd is the pty slave. */
    if (ytrace_default_enabled()) {
        struct stat sb;
        if (fstat(w->tty_fd, &sb) == 0) {
            const char *kind = "?";
            if (S_ISREG(sb.st_mode))  kind = "REG";
            else if (S_ISCHR(sb.st_mode)) kind = "CHR";
            else if (S_ISFIFO(sb.st_mode)) kind = "FIFO";
            else if (S_ISSOCK(sb.st_mode)) kind = "SOCK";
            ydebug("tty_select_thread: tty_fd=%d fstat mode=0%o kind=%s "
                   "isatty=%d\n",
                   w->tty_fd, sb.st_mode, kind, isatty(w->tty_fd));
        } else {
            ydebug("tty_select_thread: tty_fd=%d fstat FAILED errno=%d\n",
                   w->tty_fd, errno);
        }
    }
    while (!w->stop) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(w->tty_fd, &rfds);
        int max_fd = w->tty_fd;
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int r = select(max_fd + 1, &rfds, NULL, NULL, &tv);
        loops++;
        if (ytrace_default_enabled() && (loops <= 5 || r > 0)) {
            ydebug("tty_select_thread: loop=%d select tty_fd=%d r=%d "
                   "isset=%d errno=%d isatty=%d\n",
                   loops, w->tty_fd, r,
                   r > 0 ? FD_ISSET(w->tty_fd, &rfds) : 0,
                   r < 0 ? errno : 0,
                   isatty(w->tty_fd));
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (w->stop) break;
        if (r == 0) {
            /* Diagnostic: if select timed out but bytes are sitting
             * on the fd, something is wrong with select(). */
            if (ytrace_default_enabled() && loops <= 5) {
                int avail = -1;
                ioctl(w->tty_fd, FIONREAD, &avail);
                ydebug("tty_select_thread: TIMEOUT FIONREAD=%d\n", avail);
            }
            continue;  /* timeout, no data — keep waiting */
        }
        if (FD_ISSET(w->tty_fd, &rfds)) {
            /* Wake the kqueue. */
            char c = '*';
            (void)!write(w->notify_wr, &c, 1);
            ydebug("tty_select_thread: poked notify pipe\n");
            /* Block until the guest's kevent loop consumed the event
             * AND drained the bytes from the TTY. Otherwise our next
             * select() would return immediately (level-triggered) and
             * we'd spin. */
            sem_wait(w->processed);
        }
    }
    return NULL;
}

/* Allocate + register + start the select-thread. Returns 0 on success. */
static int tty_watcher_add(int kq, uint32_t guest_ident,
                           uint32_t guest_udata, int tty_fd)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;
    int fl;
    if ((fl = fcntl(pipefd[0], F_GETFD)) >= 0)
        fcntl(pipefd[0], F_SETFD, fl | FD_CLOEXEC);
    if ((fl = fcntl(pipefd[1], F_GETFD)) >= 0)
        fcntl(pipefd[1], F_SETFD, fl | FD_CLOEXEC);

    struct tty_watcher *w = (struct tty_watcher *)calloc(1, sizeof *w);
    if (!w) { close(pipefd[0]); close(pipefd[1]); return -1; }

    w->kq          = kq;
    w->guest_ident = guest_ident;
    w->guest_udata = guest_udata;
    w->tty_fd      = tty_fd;
    w->notify_rd   = pipefd[0];
    w->notify_wr   = pipefd[1];

    /* Use a named-style sem allocated on the heap — POSIX doesn't
     * portably support unnamed sems on darwin, but sem_open works. */
    char nm[64];
    snprintf(nm, sizeof nm, "/yos-tty-%d-%d-%lld",
             (int)getpid(), tty_fd, (long long)time(NULL));
    sem_unlink(nm);
    w->processed = sem_open(nm, O_CREAT | O_EXCL, 0600, 0);
    if (w->processed == SEM_FAILED) {
        ydebug("tty_watcher_add: sem_open failed: %d\n", errno);
        close(pipefd[0]); close(pipefd[1]); free(w);
        return -1;
    }
    sem_unlink(nm);  /* anonymous-style: name no longer needed */

    /* Register the notify pipe READ end on the host kqueue. We tunnel
     * (guest_ident << 32) | guest_udata in the kevent's udata so the
     * return path can map back without an extra table walk. */
    uint64_t packed_udata = ((uint64_t)guest_ident << 32) | (uint64_t)guest_udata;
    struct kevent reg;
    EV_SET(&reg, w->notify_rd, EVFILT_READ, EV_ADD, 0, 0,
           (void *)(uintptr_t)packed_udata);
    if (kevent(kq, &reg, 1, NULL, 0, NULL) < 0) {
        ydebug("tty_watcher_add: kevent EV_ADD failed: %d\n", errno);
        sem_close(w->processed);
        close(pipefd[0]); close(pipefd[1]); free(w);
        return -1;
    }

    if (pthread_create(&w->thread, NULL, tty_select_thread, w) != 0) {
        ydebug("tty_watcher_add: pthread_create failed: %d\n", errno);
        EV_SET(&reg, w->notify_rd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(kq, &reg, 1, NULL, 0, NULL);
        sem_close(w->processed);
        close(pipefd[0]); close(pipefd[1]); free(w);
        return -1;
    }
    pthread_detach(w->thread);

    pthread_mutex_lock(&g_tty_watchers_lock);
    int slot = -1;
    for (int i = 0; i < MAX_TTY_WATCHERS; i++) {
        if (!g_tty_watchers[i]) { slot = i; g_tty_watchers[i] = w; break; }
    }
    pthread_mutex_unlock(&g_tty_watchers_lock);

    if (slot < 0) {
        /* Out of slots — let it leak the thread; not critical for our
         * use case (one TTY per process). */
        ydebug("tty_watcher_add: no slot, leaking watcher\n");
    }
    ydebug("tty_watcher_add: kq=%d guest_ident=%u tty_fd=%d notify_rd=%d\n",
           kq, guest_ident, tty_fd, w->notify_rd);
    return 0;
}

/* Look up the watcher whose notify_rd was the host-kevent ident in a
 * returned event. Used to translate notify-pipe wakeups back into
 * EVFILT_READ events for the guest's TTY fd. */
static struct tty_watcher *tty_watcher_by_notify_rd(int kq, int notify_rd)
{
    pthread_mutex_lock(&g_tty_watchers_lock);
    for (int i = 0; i < MAX_TTY_WATCHERS; i++) {
        struct tty_watcher *w = g_tty_watchers[i];
        if (w && w->kq == kq && w->notify_rd == notify_rd) {
            pthread_mutex_unlock(&g_tty_watchers_lock);
            return w;
        }
    }
    pthread_mutex_unlock(&g_tty_watchers_lock);
    return NULL;
}

/* Remove + cleanup, given the guest's wfd. Used on EV_DELETE. */
static int tty_watcher_remove(int kq, uint32_t guest_ident)
{
    pthread_mutex_lock(&g_tty_watchers_lock);
    struct tty_watcher *w = NULL;
    for (int i = 0; i < MAX_TTY_WATCHERS; i++) {
        if (g_tty_watchers[i] && g_tty_watchers[i]->kq == kq
            && g_tty_watchers[i]->guest_ident == guest_ident) {
            w = g_tty_watchers[i];
            g_tty_watchers[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&g_tty_watchers_lock);
    if (!w) return -1;

    w->stop = 1;
    /* Unblock the select-thread by closing the tty_fd dup-side... we
     * don't actually own a dup, so just rely on the thread exiting on
     * the next select-wakeup. Post the sem so it can return from a
     * waiting state. */
    sem_post(w->processed);

    struct kevent del;
    EV_SET(&del, w->notify_rd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    kevent(kq, &del, 1, NULL, 0, NULL);
    /* Don't sem_close/free — the detached thread may still touch it.
     * Leak is bounded (one watcher per TTY-watcher lifetime). */
    return 0;
}

/* Per-kqueue map: guest pid → host EVFILT_USER ident registered on
 * that kq. Triggered by yos_kqueue_notify_exit when impl/proc.c sees
 * a guest child terminate. */
struct proc_watch {
    int       kq;          /* -1 if slot free */
    uintptr_t user_ident;  /* synthetic ident on host kqueue */
    uint32_t  pid;         /* guest pid */
    uint32_t  udata;       /* guest's udata to echo back */
};
#define MAX_PROC_WATCH 64
static struct proc_watch g_proc_watches[MAX_PROC_WATCH];
static pthread_mutex_t   g_proc_watches_lock = PTHREAD_MUTEX_INITIALIZER;
/* Bias the synthetic EVFILT_USER ident above any realistic fd value
 * so it can't collide with EVFILT_READ/WRITE idents on the same kq.
 * Each registration gets a fresh value. */
static uintptr_t g_proc_watch_counter = (uintptr_t)0x40000000;

void yos_kqueue_notify_exit(uint32_t pid)
{
    pthread_mutex_lock(&g_proc_watches_lock);
    for (int i = 0; i < MAX_PROC_WATCH; i++) {
        if (g_proc_watches[i].kq >= 0 && g_proc_watches[i].pid == pid) {
            struct kevent trig;
            EV_SET(&trig, g_proc_watches[i].user_ident, EVFILT_USER,
                   0, NOTE_TRIGGER, 0, NULL);
            kevent(g_proc_watches[i].kq, &trig, 1, NULL, 0, NULL);
        }
    }
    pthread_mutex_unlock(&g_proc_watches_lock);
}

static int proc_watch_add(int kq, uint32_t pid, uint32_t udata)
{
    pthread_mutex_lock(&g_proc_watches_lock);
    for (int i = 0; i < MAX_PROC_WATCH; i++) {
        if (g_proc_watches[i].kq < 0 || g_proc_watches[i].kq == 0) {
            uintptr_t ident = ++g_proc_watch_counter;
            struct kevent reg;
            EV_SET(&reg, ident, EVFILT_USER, EV_ADD | EV_CLEAR,
                   0, 0, NULL);
            if (kevent(kq, &reg, 1, NULL, 0, NULL) < 0) {
                pthread_mutex_unlock(&g_proc_watches_lock);
                return -1;
            }
            g_proc_watches[i].kq         = kq;
            g_proc_watches[i].user_ident = ident;
            g_proc_watches[i].pid        = pid;
            g_proc_watches[i].udata      = udata;
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
        if (g_proc_watches[i].kq == kq && g_proc_watches[i].pid == pid) {
            struct kevent del;
            EV_SET(&del, g_proc_watches[i].user_ident, EVFILT_USER,
                   EV_DELETE, 0, 0, NULL);
            kevent(kq, &del, 1, NULL, 0, NULL);
            g_proc_watches[i].kq  = -1;
            g_proc_watches[i].pid = 0;
        }
    }
    pthread_mutex_unlock(&g_proc_watches_lock);
}

/* Guest kevent layout — see the long comment in the Linux block.
 * Wasm32 + clang 8-aligns int64_t/uint64_t even with -D__i386__=1, so
 * data lands at +16 (not +12), giving a 64-byte struct. */
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

static inline void ke_set_ident (uint8_t *p, uint32_t v) { memcpy(p+KE_IDENT_OFF,  &v, 4); }
static inline void ke_set_filter(uint8_t *p, int16_t  v) { memcpy(p+KE_FILTER_OFF, &v, 2); }
static inline void ke_set_flags (uint8_t *p, uint16_t v) { memcpy(p+KE_FLAGS_OFF,  &v, 2); }
static inline void ke_set_fflags(uint8_t *p, uint32_t v) { memcpy(p+KE_FFLAGS_OFF, &v, 4); }
static inline void ke_set_data  (uint8_t *p, int64_t  v) { memcpy(p+KE_DATA_OFF,   &v, 8); }
static inline void ke_set_udata (uint8_t *p, uint32_t v) { memcpy(p+KE_UDATA_OFF,  &v, 4); }

static inline void write_errno(struct yos_exec_ctx *ctx, int e)
{
    if (ctx && ctx->memory && ctx->errno_off) {
        *(int *)(ctx->memory + ctx->errno_off) = yos_remap_errno_h2g(e);
    }
}

static int new_kqueue_fd(void)
{
    int fd = kqueue();
    if (fd < 0) return -1;
    /* darwin/freebsd's kqueue() doesn't take a CLOEXEC flag; set it
     * after the fact so guest fork-exec doesn't leak the kq. */
    int fl = fcntl(fd, F_GETFD);
    if (fl >= 0) fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
    return fd;
}

/* kqueue() — return a wasm fd pointing at a host kqueue instance. */
static m3ApiRawFunction(m3_yos_kqueue)
{
    m3ApiReturnType(int32_t);
    KQ_TRACE("kqueue");
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    int hfd = new_kqueue_fd();
    if (hfd < 0) { write_errno(ctx, errno); m3ApiReturn(-1); }
    int wfd = yos_fd_alloc(ctx, hfd);
    if (wfd < 0) { close(hfd); write_errno(ctx, EMFILE); m3ApiReturn(-1); }
    ydebug("kqueue() -> wfd=%d (hfd=%d)\n", wfd, hfd);
    m3ApiReturn(wfd);
}

static m3ApiRawFunction(m3_yos_kqueue1)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, flags); (void)flags;
    KQ_TRACE("kqueue1");
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    int hfd = new_kqueue_fd();
    if (hfd < 0) { write_errno(ctx, errno); m3ApiReturn(-1); }
    int wfd = yos_fd_alloc(ctx, hfd);
    if (wfd < 0) { close(hfd); write_errno(ctx, EMFILE); m3ApiReturn(-1); }
    m3ApiReturn(wfd);
}

static m3ApiRawFunction(m3_yos_kqueuex)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, flags); (void)flags;
    KQ_TRACE("kqueuex");
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    int hfd = new_kqueue_fd();
    if (hfd < 0) { write_errno(ctx, errno); m3ApiReturn(-1); }
    int wfd = yos_fd_alloc(ctx, hfd);
    if (wfd < 0) { close(hfd); write_errno(ctx, EMFILE); m3ApiReturn(-1); }
    m3ApiReturn(wfd);
}

/* kevent(kq, changelist, nchanges, eventlist, nevents, timeout)
 *
 * Pack/unpack of host udata: guest's udata is 4 bytes; host's udata
 * is a void * (8 bytes). For EVFILT_READ/WRITE we tunnel the guest
 * wfd alongside guest udata as
 *     host_udata = (uint64_t)wfd << 32 | (uint32_t)guest_udata
 * so we can hand wfd back as the kevent ident on the way out (libuv
 * keys watchers on ident). EVFILT_USER (proc-watch synthetic) gets
 * its pid/udata recovered from g_proc_watches by ident match.
 */
static m3ApiRawFunction(m3_yos_kevent)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t,  kq_wfd);
    m3ApiGetArg(uint32_t, changelist);
    m3ApiGetArg(int32_t,  nchanges);
    m3ApiGetArg(uint32_t, eventlist);
    m3ApiGetArg(int32_t,  nevents);
    m3ApiGetArg(uint32_t, timeout_off);
    KQ_TRACE4("kevent", kq_wfd,
              ((uint64_t)changelist << 32) | (uint32_t)nchanges,
              ((uint64_t)eventlist  << 32) | (uint32_t)nevents,
              timeout_off);

    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    uint32_t mem_size = 0;
    ctx->memory = m3_GetMemory(runtime, &mem_size, 0);
    ctx->memory_size = mem_size;
    static int kevent_call_n = 0;
    int my_call = ++kevent_call_n;
    if (ytrace_default_enabled() && my_call < 30) {
        ydebug("kevent#%d(kq=%d nchanges=%d nevents=%d timeout=%s)\n",
               my_call, kq_wfd, nchanges, nevents,
               timeout_off ? "ts" : "BLOCK");
    }

    int kq = yos_fd_get(ctx, kq_wfd);
    if (kq < 0) { write_errno(ctx, EBADF); m3ApiReturn(-1); }

    /* Reject negative counts AND cap at 256 (stack-array bound) so
     * `int i < nchanges` can't run past the buffer or sign-overflow. */
    if (nchanges < 0 || nevents < 0) { write_errno(ctx, EINVAL); m3ApiReturn(-1); }
    if (nchanges > 256) { write_errno(ctx, EINVAL); m3ApiReturn(-1); }
    if (nevents  > 256) nevents = 256;
    /* Validate the full changelist range up-front (wrap-safe).
     * Without this, `changelist + i*KE_SZ` in uint32 wraps for large
     * inputs and lets the per-iteration check pass on a bogus range. */
    if (nchanges > 0 && changelist != 0) {
        if ((uint64_t)changelist + (uint64_t)nchanges * KE_SZ > (uint64_t)mem_size) {
            write_errno(ctx, EFAULT); m3ApiReturn(-1);
        }
    } else if (nchanges > 0) {
        write_errno(ctx, EFAULT); m3ApiReturn(-1);
    }
    /* Validate eventlist target buffer the same way. */
    if (nevents > 0 && eventlist != 0) {
        if ((uint64_t)eventlist + (uint64_t)nevents * KE_SZ > (uint64_t)mem_size) {
            write_errno(ctx, EFAULT); m3ApiReturn(-1);
        }
    } else if (nevents > 0) {
        write_errno(ctx, EFAULT); m3ApiReturn(-1);
    }
    struct kevent host_changes[256];
    struct kevent host_events[256];
    int n_host_changes = 0;
    for (int i = 0; i < nchanges; i++) {
        const uint8_t *ke = ctx->memory + changelist + (uint32_t)i * KE_SZ;
        uint32_t ident  = ke_ident(ke);
        int16_t  filter = ke_filter(ke);
        uint16_t flags  = ke_flags(ke);
        uint32_t fflags = ke_fflags(ke);
        uint32_t udata  = ke_udata(ke);
        if (ytrace_default_enabled()) {
            ydebug("kevent change[%d]: ident=%u filter=%d flags=0x%x "
                   "fflags=0x%x udata=0x%x\n",
                   i, ident, filter, flags, fflags, udata);
        }

        if (filter == G_EVFILT_PROC) {
            (void)fflags;  /* assume NOTE_EXIT */
            if (flags & G_EV_ADD) {
                if (proc_watch_add(kq, ident, udata) < 0) {
                    ydebug("kevent: proc_watch_add(pid=%u) failed errno=%d\n",
                           ident, errno);
                }
            } else if (flags & (G_EV_DELETE | G_EV_DISABLE)) {
                proc_watch_remove(kq, ident);
            }
            continue;
        }
        if (filter == G_EVFILT_SIGNAL) {
            /* libuv has its own pipe-based signal path on FreeBSD; we
             * silently accept the registration as a no-op. */
            continue;
        }
        if (filter != G_EVFILT_READ && filter != G_EVFILT_WRITE) {
            ydebug("kevent: unsupported filter=%d, skipping\n", filter);
            continue;
        }

        int host_fd = yos_fd_get(ctx, (int32_t)ident);
        if (host_fd < 0) {
            /* Stale guest fd in changelist — POSIX says report
             * EV_ERROR back into eventlist for this entry; we just
             * skip (libuv tolerates it). */
            continue;
        }
        ydebug("kevent change[%d]: wfd=%u → hfd=%d\n", i, ident, host_fd);

        /* (TTY workaround removed: empirical test on macOS 24.4 / xnu-11417
         * shows kqueue *does* accept EVFILT_READ on pty slaves — kevent
         * returns without EV_ERROR and fires when bytes arrive. The
         * select-thread fallback was unnecessary; the real fix was the
         * ioctl FIONBIO translation in impl/io/io.c.) */

        uint64_t packed_udata = ((uint64_t)ident << 32) | (uint64_t)udata;
        EV_SET(&host_changes[n_host_changes],
               host_fd, filter, flags, fflags, 0,
               (void *)(uintptr_t)packed_udata);
        n_host_changes++;
    }

    /* `nevents <= 0` is just-apply-changes mode; do the kevent call
     * with a NULL eventlist + 0-timeout so the kernel processes the
     * changes immediately. */
    struct timespec ts = {0}, *ts_p = NULL;
    if (timeout_off) {
        /* Guest timespec on FreeBSD i386 (-D__i386__=1) is 8 bytes:
         *   int32_t tv_sec  @0
         *   int32_t tv_nsec @4
         * (modern FreeBSD has __int64_t time_t but our headers'
         * x86/_types.h selects the i386 branch with __int32_t when
         * targeting i386). Read accordingly — mis-reading 8+4 bytes
         * gives kevent absurd seconds and EINVAL. */
        if (timeout_off + 8 > mem_size) {
            write_errno(ctx, EFAULT); m3ApiReturn(-1);
        }
        int32_t tv_sec, tv_nsec;
        memcpy(&tv_sec,  ctx->memory + timeout_off + 0, 4);
        memcpy(&tv_nsec, ctx->memory + timeout_off + 4, 4);
        ts.tv_sec  = (time_t)tv_sec;
        ts.tv_nsec = (long)tv_nsec;
        ts_p = &ts;
    }

    if (nevents <= 0) {
        int r = kevent(kq, host_changes, n_host_changes,
                       NULL, 0, ts_p);
        ydebug("kevent#%d apply-only -> %d errno=%d\n",
               my_call, r, r < 0 ? errno : 0);
        if (r < 0) { write_errno(ctx, errno); m3ApiReturn(-1); }
        m3ApiReturn(0);
    }

    int n = kevent(kq, host_changes, n_host_changes,
                   host_events, nevents, ts_p);
    ydebug("kevent#%d returned %d events errno=%d\n",
           my_call, n, n < 0 ? errno : 0);
    if (n < 0) { write_errno(ctx, errno); m3ApiReturn(-1); }
    if (ytrace_default_enabled()) {
        for (int i = 0; i < n && i < 8; i++) {
            ydebug("  event[%d]: ident=%lu filter=%d flags=0x%x "
                   "fflags=0x%x data=%lld udata=%p\n",
                   i, (unsigned long)host_events[i].ident,
                   host_events[i].filter, host_events[i].flags,
                   host_events[i].fflags, (long long)host_events[i].data,
                   host_events[i].udata);
        }
    }

    /* Marshal back into FreeBSD-shape kevents. */
    for (int i = 0; i < n; i++) {
        if (eventlist + (uint32_t)(i+1) * KE_SZ > mem_size) {
            write_errno(ctx, EFAULT); m3ApiReturn(-1);
        }
        uint8_t *ke = ctx->memory + eventlist + (uint32_t)i * KE_SZ;
        memset(ke, 0, KE_SZ);

        int16_t  hfilter = host_events[i].filter;
        uint64_t hudata  = (uint64_t)(uintptr_t)host_events[i].udata;
        uint32_t ident_w = (uint32_t)(hudata >> 32);
        uint32_t udata   = (uint32_t)(hudata & 0xffffffff);
        int16_t  out_filter = hfilter;
        uint32_t out_fflags = (uint32_t)host_events[i].fflags;
        uint16_t out_flags  = 0;

        if (hfilter == EVFILT_USER) {
            /* Synthesize EVFILT_PROC|NOTE_EXIT for the guest. EV_ONESHOT
             * semantics: kqueue caller (libuv's uv__wait_children) does
             * loop->nfds-- expecting the registration to be already
             * gone. Match by removing here. */
            pthread_mutex_lock(&g_proc_watches_lock);
            for (int j = 0; j < MAX_PROC_WATCH; j++) {
                if (g_proc_watches[j].kq == kq &&
                    g_proc_watches[j].user_ident == host_events[i].ident) {
                    ident_w = g_proc_watches[j].pid;
                    udata   = g_proc_watches[j].udata;
                    struct kevent del;
                    EV_SET(&del, host_events[i].ident, EVFILT_USER,
                           EV_DELETE, 0, 0, NULL);
                    kevent(kq, &del, 1, NULL, 0, NULL);
                    g_proc_watches[j].kq  = -1;
                    g_proc_watches[j].pid = 0;
                    break;
                }
            }
            pthread_mutex_unlock(&g_proc_watches_lock);
            out_filter = G_EVFILT_PROC;
            out_fflags = G_NOTE_EXIT;
        }

        /* TTY workaround: a wakeup from our self-pipe means the
         * underlying TTY is readable. Drain the wake byte, post the
         * select-thread's semaphore so it can re-arm, and rewrite the
         * event to look like a direct EVFILT_READ on the guest's TTY
         * fd (libuv keys watchers on ident, so the guest's wfd has to
         * appear here). */
        if (hfilter == EVFILT_READ) {
            struct tty_watcher *tw = tty_watcher_by_notify_rd(
                kq, (int)host_events[i].ident);
            if (tw) {
                char drain;
                while (read(tw->notify_rd, &drain, 1) > 0) { /* drain all */ }
                ident_w = tw->guest_ident;
                udata   = tw->guest_udata;
                out_filter = G_EVFILT_READ;
                out_fflags = 0;
                /* Tell the select-thread we're done so it can call
                 * select() again. By the time the guest's read_cb
                 * runs and read()s from the TTY, the buffer will be
                 * empty and select() will block until more data. */
                sem_post(tw->processed);
            }
        }

        if (host_events[i].flags & EV_EOF)   out_flags |= G_EV_EOF;
        if (host_events[i].flags & EV_ERROR) out_flags |= G_EV_ERROR;

        ke_set_ident (ke, ident_w);
        ke_set_filter(ke, out_filter);
        ke_set_flags (ke, out_flags);
        ke_set_fflags(ke, out_fflags);
        ke_set_data  (ke, (int64_t)host_events[i].data);
        ke_set_udata (ke, udata);
    }
    m3ApiReturn(n);
}

void yos_kqueue_link(IM3Module mod)
{
    /* Lazy-init proc_watch table — but ONLY on the very first link.
     * The asyncify-fork child re-links its imports after exec, and
     * blindly resetting the table here would wipe the PARENT's
     * already-registered EVFILT_PROC|NOTE_EXIT watcher (which lives
     * in this process-global table because g_proc_watches is shared
     * across all runtimes). With the table cleared, the child's exit
     * notify can't find the parent's watch, the parent's libuv loop
     * never wakes from kevent, and :q!/exit hangs forever. */
    static pthread_once_t init_once = PTHREAD_ONCE_INIT;
    static void (*init_fn)(void) = NULL;
    /* C90-friendly init: a tiny static helper run by pthread_once. */
    extern void yos_proc_watches_init(void);
    pthread_once(&init_once, yos_proc_watches_init);
    m3_LinkRawFunction(mod, "env", "kqueue",  "i()",       m3_yos_kqueue);
    m3_LinkRawFunction(mod, "env", "kqueue1", "i(i)",      m3_yos_kqueue1);
    m3_LinkRawFunction(mod, "env", "kqueuex", "i(i)",      m3_yos_kqueuex);
    m3_LinkRawFunction(mod, "env", "kevent",  "i(iiiiii)", m3_yos_kevent);
    (void)init_fn;
}

void yos_proc_watches_init(void)
{
    for (int i = 0; i < MAX_PROC_WATCH; i++) g_proc_watches[i].kq = -1;
}

