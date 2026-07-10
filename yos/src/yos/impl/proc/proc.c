#define _GNU_SOURCE
#define _GNU_SOURCE   /* for the syscall() prototype in <unistd.h> */
#include "yos/types.h"
#include <yos/ytrace/ytrace.h>
#include "impl/proc/clone-abi.h"
#include "impl/errno_helpers.h"   /* yos_errno_neg — exec failure POSIX errno */
#include "impl/io/io-internal.h"  /* wstr_check */
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>  /* execvp_path_search: stat() the PATH candidates */
#include <sys/syscall.h>
#include <pthread.h>     /* pthread_self() — used by fork trace lines */
/* CLEARTID exit-wake done via pthread_cond_broadcast on the proc's
 * wait_cond (see below). No Linux futex syscall needed on the host;
 * any guest libc that uses futex semantics goes through a small
 * portable shim. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>   /* mmap/munmap for fork's memory snapshot */

#include "wasm3.h"
#include "m3_env.h"
#include "impl/proc/pthread.h"   /* yos_pthread_host typedef + destroy */
#include "platform.h"            /* yos_plat_exit */

/* ============================================================================
 * Asyncify Helpers
 * ============================================================================ */

static void call_asyncify(IM3Runtime rt, const char *name, uint32_t arg)
{
    IM3Function f;
    if (m3_FindFunction(&f, rt, name) == NULL) {
        if (arg != (uint32_t)-1)
            m3_CallV(f, arg);
        else
            m3_CallV(f);
    }
}

static int get_asyncify_state(IM3Runtime rt)
{
    IM3Function f;
    if (m3_FindFunction(&f, rt, "asyncify_get_state"))
        return -1;
    m3_CallV(f);
    int32_t state;
    m3_GetResultsV(f, &state);
    return state;
}

/* Is this ctx currently REWINDING out of an asyncify fork? A composite
 * fork bridge (forkpty) needs to know whether it is on its first entry
 * (set up the pty, then unwind through yos_fork) or replaying on the
 * rewind (finish the parent/child half). */
int yos_fork_rewinding(struct yos_exec_ctx *ctx)
{
    IM3Runtime wrt = ctx ? (IM3Runtime)ctx->runtime : NULL;
    return wrt && get_asyncify_state(wrt) == ASYNCIFY_REWINDING;
}

/* ============================================================================
 * Process Table Operations
 * ============================================================================ */

struct yos_proc *yos_proc_alloc(struct yos_runtime *rt, int32_t ppid)
{
    pthread_mutex_lock(&rt->proc_lock);

    for (int i = 0; i < YOS_MAX_PROCS; i++) {
        if (rt->procs[i].state == YOS_PROC_FREE) {
            struct yos_proc *p = &rt->procs[i];
            memset(p, 0, sizeof(*p));
            p->pid = rt->next_pid++;
            p->ppid = ppid;
            p->pgid = p->pid;
            p->sid = p->pid;
            p->tgid = p->pid;   /* default; clone(CLONE_THREAD) overrides */
            p->tid_address = 0;
            p->state = YOS_PROC_READY;
            p->vfork_parent_pid = -1;
            pthread_mutex_init(&p->lock, NULL);
            pthread_cond_init(&p->wait_cond, NULL);
            pthread_cond_init(&p->vfork_cond, NULL);

            pthread_mutex_unlock(&rt->proc_lock);
            ydebug("allocated pid=%d ppid=%d\n", p->pid, ppid);
            return p;
        }
    }

    pthread_mutex_unlock(&rt->proc_lock);
    return NULL;
}

struct yos_proc *yos_proc_find(struct yos_runtime *rt, int32_t pid)
{
    pthread_mutex_lock(&rt->proc_lock);

    for (int i = 0; i < YOS_MAX_PROCS; i++) {
        if (rt->procs[i].pid == pid && rt->procs[i].state != YOS_PROC_FREE) {
            pthread_mutex_unlock(&rt->proc_lock);
            return &rt->procs[i];
        }
    }

    pthread_mutex_unlock(&rt->proc_lock);
    return NULL;
}

/* Post-exit proc-table maintenance. Called from yos_exit AND from
 * signal_pump's SIG_DFL terminate path (which bypasses yos_exit by
 * calling pthread_exit directly). Two responsibilities:
 *
 *   1. Reparent any children of the just-exiting proc to init (pid 1).
 *      POSIX semantics: orphans get inherited by init, not left
 *      pointing at a dead parent.
 *   2. Auto-reap THIS proc when the parent ignores SIGCHLD or when
 *      the parent IS init — POSIX kernels discard the zombie in
 *      either case (the latter because init is the universal reaper).
 *      Without this, every closed telnet session leaves an
 *      un-reapable zombie + an orphan zsh that nothing ever cleans.
 *
 * Safe to call after the proc has been marked ZOMBIE. Caller holds
 * no locks. Returns 1 if the proc was auto-reaped (state set to
 * FREE), 0 if it remains ZOMBIE awaiting an explicit waitpid. */
void yos_proc_post_exit_cleanup(struct yos_exec_ctx *ctx)
{
    if (!ctx || !ctx->proc || !ctx->rt) return;
    int32_t exiting_pid = ctx->proc->pid;
    int32_t parent_pid  = ctx->proc->ppid;

    /* Close all host fds the proc holds. Critical because signal_pump
     * may reach here via SIG_DFL terminate WITHOUT going through
     * yos_exit, which is the other place that closes fds. Without
     * this, an orphan shell still holds its PTY slave fd, telnetd
     * never sees master EOF, and the session lingers.
     *
     * Walk wasm fds 0..MAX. The `hfd >= 3` filter keeps yos's own
     * host stdin/stdout/stderr (host fds 0/1/2) intact while still
     * closing the guest's dup'd copies — yos_fd_table_init F_DUPFD's
     * fd_map[0,1,2] to host fds >= 3 at startup, and tcpserver-forked
     * children dup2 the socket over wasm fd 0/1/2 (the host fd is
     * a fresh dup, also >= 3). Both shapes get closed. */
    for (int i = 0; i < YOS_FD_MAX; i++) {
        int hfd = ctx->fd_map[i];
        if (hfd >= 3) {
            close(hfd);
            ctx->fd_map[i] = -1;
        }
    }
    for (int i = 0; i < 256; i++) {
        FILE *fp = (FILE *)ctx->file_slots[i];
        if (!fp) continue;
        ctx->file_slots[i] = NULL;
        ctx->file_wfds[i]  = -1;
        ctx->file_modes[i][0] = '\0';
        fclose(fp);
    }

    /* FreeBSD SIGCHLD = 20 (matches default_action_is_terminate table
     * in sig.c). Auto-reap on either:
     *   - parent has SIGCHLD=SIG_IGN (POSIX SA_NOCLDWAIT semantics)
     *   - this proc was orphaned (its real parent already died and we
     *     forcibly set ppid=1). Init normally reaps orphans; yos's
     *     init doesn't, so we do it here.
     * Do NOT auto-reap just because ppid==1 — a legitimate child of
     * the root guest (pid 1) still needs waitpid by its parent. */
    const int FBSD_SIGCHLD = 20;
    int parent_ignores_chld = (ctx->proc->was_orphaned != 0);
    /* Collect any children of the exiting proc; we'll SIGHUP them
     * after dropping the lock. POSIX: when a session leader (which
     * here is roughly "the proc that opened the controlling tty's
     * master fd") terminates, children get SIGHUP so an orphaned
     * shell exits naturally instead of camping on a dead PTY.
     * yos doesn't track session leadership explicitly, so we apply
     * the SIGHUP-orphans rule whenever a proc dies — practical
     * enough for telnetd→zsh and matches what users expect when
     * their controlling tty hangs up. */
    int32_t hup_pids[YOS_MAX_PROCS];
    int hup_count = 0;
    pthread_mutex_lock(&ctx->rt->proc_lock);
    for (int i = 0; i < YOS_MAX_PROCS; i++) {
        struct yos_proc *p = &ctx->rt->procs[i];
        if (p->state == YOS_PROC_FREE) continue;
        if (p->ppid == exiting_pid && p->pid != exiting_pid) {
            p->ppid = 1;
            p->was_orphaned = 1;
            if (hup_count < YOS_MAX_PROCS)
                hup_pids[hup_count++] = p->pid;
        }
        if (p->pid == parent_pid && p->ctx_handle) {
            struct yos_exec_ctx *pctx =
                (struct yos_exec_ctx *)p->ctx_handle;
            if (pctx->sig_ignore_mask & (1u << FBSD_SIGCHLD))
                parent_ignores_chld = 1;
        }
    }
    if (parent_ignores_chld && ctx->proc->state == YOS_PROC_ZOMBIE) {
        pthread_mutex_lock(&ctx->proc->lock);
        ctx->proc->state      = YOS_PROC_FREE;
        ctx->proc->ctx_handle = NULL;
        if (ctx->proc->cmdline) {
            for (int j = 0; j < ctx->proc->cmdline_argc; j++)
                free(ctx->proc->cmdline[j]);
            free(ctx->proc->cmdline);
            ctx->proc->cmdline = NULL;
            ctx->proc->cmdline_argc = 0;
        }
        pthread_mutex_unlock(&ctx->proc->lock);
        ydebug("post_exit_cleanup: auto-reap pid=%d ppid=%d "
               "(ignores=%d init=%d)\n",
               exiting_pid, parent_pid, parent_ignores_chld,
               parent_pid == 1);
    }
    pthread_mutex_unlock(&ctx->rt->proc_lock);

    ydebug("post_exit_cleanup: pid=%d hup_count=%d\n",
           exiting_pid, hup_count);

    /* Deliver SIGHUP to orphaned children outside the rt lock — the
     * deliver path takes its own per-proc lock and we don't want to
     * nest. Same wake-channel as kill: per-ctx sig_pending + SIGUSR2
     * wake. Most shells (zsh, bash) exit on SIGHUP with default
     * disposition, so orphaned interactive shells release their PTY
     * slave fds promptly when their controlling-tty owner dies. */
    extern int yos_proc_kill_by_pid(struct yos_runtime *, int32_t, int32_t);
    for (int i = 0; i < hup_count; i++) {
        const int FBSD_SIGHUP = 1;
        int rc = yos_proc_kill_by_pid(ctx->rt, hup_pids[i], FBSD_SIGHUP);
        ydebug("post_exit_cleanup: SIGHUP -> orphan pid=%d (rc=%d)\n",
               hup_pids[i], rc);
    }

    /* A forkpty child's exit is announced to the parent via SIGCHLD: a
     * pty child is a hand-rolled fork, so no EVFILT_PROC filter watches
     * it — the parent's sigaction (nvim's libuv SIGCHLD watcher) is how
     * it learns the exit and waitpid()s ("[Process exited N]", jobwait,
     * on_exit callbacks). Default SIGCHLD disposition is ignore, so a
     * parent without a handler is unaffected. Scoped to forkpty
     * children so ordinary fork/wait flows (zsh, tmux) keep their
     * existing wait/sigsuspend-driven delivery. */
    if (ctx->is_forkpty_child) {
        int rc = yos_proc_kill_by_pid(ctx->rt, parent_pid, FBSD_SIGCHLD);
        ydebug("post_exit_cleanup: SIGCHLD -> forkpty parent pid=%d (rc=%d)\n",
               parent_pid, rc);
    }
}

int32_t yos_exit(struct yos_exec_ctx *ctx, int32_t code)
{
    ydebug("exit(%d) is_child=%d\n", code, ctx->is_child);

    /* Release any libarchive handles the guest leaked, BEFORE the fd
     * table is torn down (yos_proc_post_exit_cleanup, below, closes the
     * archive's read fd). Doing it first lets each leaked archive close
     * its own yos fd + read scratch cleanly via archive_read_free; the
     * fd-table sweep then has nothing of ours left to double-close. This
     * single early call covers every exit shape — forked-child
     * pthread_exit and main-process yos_plat_exit alike — both of which
     * bypass main()'s post-run teardown. */
#ifdef YOS_HAVE_LIBARCHIVE
    {
        extern void yos_libarchive_ctx_free(struct yos_exec_ctx *);
        yos_libarchive_ctx_free(ctx);
    }
#endif
    {
        /* Host iconv_t handles the guest leaked (impl/libc/iconv.c). */
        extern void yos_iconv_ctx_free(struct yos_exec_ctx *);
        yos_iconv_ctx_free(ctx);
    }

    /* Asyncify-fork-child silence workaround.
     *
     * When a forked child wasm-zsh tries to exec a missing command it
     * walks PATH, every execve returns -1/ENOENT, and zsh's source code
     * is supposed to reach `zerr("command not found: %s", arg0)` →
     * zwarning → env.fputc. Under asyncify-based fork the child's
     * zwarning path is never reached: the trace shows the child does
     * the whole PATH search then jumps to _exit(1) (via the fatal:
     * label, errflag-driven) without any env import write. The single-
     * command case (no fork, exec-in-place) does print correctly.
     *
     * Until the asyncify-fork code-path bug is rooted out, synthesise
     * the diagnostic at the bridge boundary: when a child exits with
     * non-zero code, the most recent execve attempt failed, and the
     * child wrote NOTHING to stderr since that attempt, write the
     * standard "zsh: command not found: <name>" / equivalent line to
     * host fd 2 ourselves. Without this the user sees dead silence on
     * every `bad-cmd; <anything>` invocation — interactive zsh, scripts
     * with multiple commands, anything that forces a fork. */
    /* This synthesized diagnostic is an asyncify-fork compatibility
     * shim, NOT generic exit semantics — under a correct fork the child
     * would emit its own "command not found" via stderr stdio. It is on
     * by default so interactive shells stay usable; YOS_NO_FORK_DIAG=1
     * disables it to observe the raw (silent) corruption the regression
     * tests pin (tests/integration/zsh/external-{missing,diagnostic}).
     * Remove this whole block once the proc-side fork-snapshot bug that
     * swallows the child's own stderr is rooted out. */
    if (ctx->is_child && code != 0 &&
        ctx->last_failed_exec_path[0] != '\0' &&
        !ctx->stderr_written_since_exec &&
        getenv("YOS_NO_FORK_DIAG") == NULL) {
        const char *path = ctx->last_failed_exec_path;
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        const char *kind = (ctx->last_failed_exec_errno == ENOENT)
            ? "command not found" : strerror(ctx->last_failed_exec_errno);
        /* zsh's user-visible name is the BASENAME of the failed exec
         * (PATH search prepends dirs to the original argv[0], so the
         * leaf is what the user typed). */
        char msg[512];
        int n = snprintf(msg, sizeof(msg),
                         "zsh:1: %s: %s\n", kind, base);
        if (n > 0) {
            /* Write to the child's wfd 2 mapping if available, else
             * host stderr directly. */
            int hfd = ctx->fd_map[2];
            if (hfd < 0) hfd = 2;
            ssize_t _ = write(hfd, msg, (size_t)n);
            (void)_;
        }
    }
    if (ctx->proc) {
        /* Signal vfork parent if applicable */
        if (ctx->proc->vfork_parent_pid > 0) {
            struct yos_proc *parent = yos_proc_find(ctx->rt, ctx->proc->vfork_parent_pid);
            if (parent) {
                pthread_mutex_lock(&parent->lock);
                parent->vfork_child_done = 1;
                pthread_cond_signal(&parent->vfork_cond);
                pthread_mutex_unlock(&parent->lock);
            }
            ctx->proc->vfork_parent_pid = -1;
        }

        /* CLONE_CHILD_CLEARTID / set_tid_address: zero the wasm-side
         * tid word. The host-side wakeup happens via the
         * `wait_cond` broadcast a few lines below, which is what
         * pthread_join uses on this side. Guest-side futex semantics
         * (if a guest's libc uses them) go through impl/futex.c. */
        if (ctx->proc->tid_address && ctx->memory &&
            ctx->proc->tid_address + 4 <= ctx->memory_size) {
            uint32_t *tid_word = (uint32_t *)(ctx->memory + ctx->proc->tid_address);
            __atomic_store_n(tid_word, 0u, __ATOMIC_SEQ_CST);
        }

        /* Only transition to ZOMBIE from a live state. SIGKILL's
         * deliver_to_proc path marks the proc ZOMBIE up front and
         * the parent's waitpid may have already reaped (state ==
         * FREE) before this host thread reached its _exit. Re-marking
         * a FREE slot as ZOMBIE resurrects a stale (pid, ppid) tuple
         * in the proc table — a subsequent waitpid(-1, WNOHANG)
         * "finds" the same already-reaped child as a phantom zombie
         * and chaos churn flags it as a leak. */
        pthread_mutex_lock(&ctx->proc->lock);
        if (ctx->proc->state == YOS_PROC_READY ||
            ctx->proc->state == YOS_PROC_RUNNING) {
            ctx->proc->state = YOS_PROC_ZOMBIE;
            ctx->proc->exit_code = code;
            ctx->proc->exited = 1;
        }
        pthread_cond_broadcast(&ctx->proc->wait_cond);
        pthread_mutex_unlock(&ctx->proc->lock);

        /* Reparent orphans + auto-reap if parent ignores SIGCHLD or
         * is init. Helper is shared with signal_pump's SIG_DFL
         * terminate path so kill-via-signal cleans up the same way. */
        yos_proc_post_exit_cleanup(ctx);

        /* Runtime-wide "something exited" event. main.c's shutdown
         * wait blocks on this so yos doesn't tear down while a
         * forked child is still alive. Per-proc wait_cond above
         * is for waitpid() consumers; this is the rt-wide one. */
        pthread_mutex_lock(&ctx->rt->proc_lock);
        pthread_cond_broadcast(&ctx->rt->any_exit_cond);
        pthread_mutex_unlock(&ctx->rt->proc_lock);
    }

    /* Forked children run in separate threads - only terminate the thread */
    if (ctx->is_child) {
        /* Flush host stdio. impl/file.c keeps a single global FILE*
         * table where handles 1/2/3 alias the host's stdin/stdout/
         * stderr, so a guest's printf("...") lands in the host's
         * glibc stdout buffer. yos_exit doesn't go through libc's
         * exit(3) on the guest side — pthread_exit is the only
         * thing left — so the atexit handler that would normally
         * fflush these never runs. Without the explicit flush, an
         * external command's output stays buffered and only reaches
         * the user when SOMETHING ELSE writes to stdout next: in an
         * interactive zsh that's the next command, so the user sees
         * `command1: <silence>` followed by the previous command's
         * output glued onto the next command's. fflush(NULL) flushes
         * every open output stream including stdout / stderr. */
        fflush(NULL);
        /* Fd + FILE* close + reparent + auto-reap all live in
         * yos_proc_post_exit_cleanup(). yos_exit is one of two
         * places that calls it; the other is signal_pump's SIG_DFL
         * terminate path. Both flows now produce identical cleanup. */

        /* Notify any libuv-style EVFILT_PROC|NOTE_EXIT watcher in the
         * parent runtime. libuv on __FreeBSD__ uses kqueue PROC events
         * (not SIGCHLD) to detect child exit; without this notify the
         * parent's event loop never wakes and :q!/exit hangs. */
        extern void yos_kqueue_notify_exit(uint32_t);
        if (ctx->proc) yos_kqueue_notify_exit((uint32_t)ctx->proc->pid);
        pthread_exit((void *)(intptr_t)code);
    }

    /* Main process - terminate the whole program. The platform helper
     * picks exit() vs _exit() — on POSIX we go through exit() so atexit
     * + stdio-flush run; on Windows _exit() to skip debug-CRT teardown
     * that deadlocks when our pthread shim's worker threads are still
     * alive. fflush(NULL) below covers the stdio side either way. */
    fflush(NULL);
    yos_plat_exit(code);
    return 0; /* unreachable */
}

/* ============================================================================
 * Fork Implementation (asyncify-based)
 * ============================================================================ */

int32_t yos_fork(struct yos_exec_ctx *ctx)
{
    if (!ctx->proc || !ctx->rt) {
        ydebug("fork: invalid context\n");
        return -EINVAL;
    }

    IM3Runtime wrt = (IM3Runtime)ctx->runtime;
    IM3Module mod = (IM3Module)ctx->module;

    if (!wrt || !mod) {
        ydebug("fork: no wasm runtime\n");
        return -ENOMEM;
    }

    int state = get_asyncify_state(wrt);

    /* If asyncify not available, fork cannot work */
    if (state < 0) {
        ydebug("fork: asyncify not available - WASM must be compiled with asyncify\n");
        return -ENOSYS;
    }

    /* REWIND path: returning from fork after rewind */
    if (state == ASYNCIFY_REWINDING) {
        call_asyncify(wrt, "asyncify_stop_rewind", -1);
        ydebug("fork rewind complete, returning %d\n", ctx->fork_return);
        return ctx->fork_return;
    }

    /* FIRST CALL path: trigger unwind */
    ydebug("fork called by pid=%d, triggering unwind\n", ctx->proc->pid);

    /* Per-runtime fd-table fork happens later in fork_thread_func once
     * the child ctx exists; nothing to do here. */

    /* Get memory for asyncify buffer */
    uint32_t mem_size;
    uint8_t *mem = m3_GetMemory(wrt, &mem_size, 0);
    if (!mem || mem_size == 0) {
        ydebug("fork: no wasm memory\n");
        return -ENOMEM;
    }

    /* Allocate asyncify buffer once (at end of memory), reuse for all forks */
    if (ctx->asyncify_ptr == 0) {
        ctx->asyncify_ptr = mem_size - ASYNCIFY_BUF_SIZE;
        ydebug("asyncify buffer at %u\n", ctx->asyncify_ptr);
    }

    /* Reset asyncify buffer header for this fork */
    uint32_t *buf = (uint32_t *)(mem + ctx->asyncify_ptr);
    buf[0] = ctx->asyncify_ptr + 8;
    buf[1] = ctx->asyncify_ptr + ASYNCIFY_BUF_SIZE;

    /* Allocate child process slot */
    struct yos_proc *child_proc = yos_proc_alloc(ctx->rt, ctx->proc->pid);
    if (!child_proc) {
        return -EAGAIN;
    }
    child_proc->pgid = ctx->proc->pgid;
    child_proc->sid = ctx->proc->sid;
    /* fork(2) inherits comm/exe/cwd from the parent. Without this
     * copy the child appears in /proc/<pid>/stat with an empty
     * COMMAND column until something execve's into it — surprising
     * for short-lived helpers and breaks `ps` output for any forked-
     * but-not-yet-exec'd process. execve replaces these on the
     * exec path. */
    memcpy(child_proc->comm, ctx->proc->comm, sizeof(child_proc->comm));
    memcpy(child_proc->exe,  ctx->proc->exe,  sizeof(child_proc->exe));
    memcpy(child_proc->cwd,  ctx->proc->cwd,  sizeof(child_proc->cwd));

    /* Store child pid as return value for parent (child gets 0) */
    ctx->fork_return = child_proc->pid;
    ctx->fork_pending = 1;

    ydebug("starting unwind, child will be pid=%d\n", child_proc->pid);
    call_asyncify(wrt, "asyncify_start_unwind", ctx->asyncify_ptr);

    return child_proc->pid;
}

/* Live-region descriptor used by snapshot_wasm_memory / restore.
 * Offsets are within the wasm linear memory; len is bytes.
 *
 * The cap is 16 K entries (256 KiB of struct space). With page
 * coalescing, a typical guest produces a handful of runs (data
 * segment + heap + asyncify buffer + a few mmap allocations). The
 * cap exists only so a pathologically fragmented heap doesn't
 * unboundedly grow the snapshot metadata — on overflow we keep
 * copying to dst and signal the child to do a single full-size
 * memcpy at restore instead of replaying the (incomplete) list. */
#define YOS_FORK_MAX_REGIONS 16384
struct yos_fork_region {
    uint32_t off;
    uint32_t len;
};

/* Thread argument for child process */
typedef struct {
    struct yos_runtime *rt;
    struct yos_proc *proc;
    uint8_t *memory_snapshot;
    size_t memory_size;
    uint32_t asyncify_ptr;
    /* TODO(setjmp-refactor): ctx now has sj_slots[16] instead of a single
     * sj_asyncify_ptr — fork's setjmp-state handoff needs reworking to
     * carry all live slots. Leaving sj_discard_ptr only for now; nvim
     * doesn't fork, so no regression on the path being investigated. */
    uint32_t sj_discard_ptr;
    int64_t *wasm_globals;
    uint32_t wasm_globals_count;
    uint32_t heap_end;
    /* Allocator bookmarks (impl/alloc.c). These live in the host-side
     * yos_exec_ctx but reference wasm-memory offsets. The free-list
     * body is IN the wasm linear memory, so it's carried across by the
     * memory snapshot — but the bookmark fields must be transferred
     * explicitly, otherwise the child sees alloc_hi=0 and runs
     * alloc_init_locked on top of the parent's already-carved heap.
     * Worse: the parent's alloc_init pushed heap_end to memory_size/2,
     * so the child's re-init computes hi==lo and returns -1 → every
     * malloc in the child returns 0 → "failure allocating argument
     * space" out of telnetd's first addarg. */
    uint32_t alloc_lo;
    uint32_t alloc_hi;
    uint32_t alloc_free_head;
    /* mmap cursor for anonymous mmap2 carving (impl/mem/mem.c). Without
     * this in the snapshot, the child's mmap_top stays at calloc'd 0
     * (or re-initialises to memory_size/2), and the child's first
     * mmap returns the SAME wasm address parent's first mmap returned
     * — clobbering parent's region byte-for-byte. */
    uint32_t mmap_top;
    /* Free list — body lives in wasm linear memory (rides on the
     * memory snapshot), but the per-ctx bookmarks are host-side. */
    struct yos_free_region free_list[YOS_MAX_FREE_REGIONS];
    int free_count;
    int argc;
    char **argv;
    int envc;
    char **envp;
    uint8_t *wasm_bytes;
    size_t wasm_bytes_size;
    /* Live-region map produced by snapshot_wasm_memory. Each entry is
     * a (offset, length) pair INTO memory_snapshot that the child
     * should memcpy onto its fresh linear memory. Everything outside
     * these regions is implicitly zero (child's freshly resized
     * memory comes pre-zeroed by mmap). Replaces a brute-force 256-
     * MiB memcpy with O(live-bytes) memcpys — typically <1 MiB for
     * a small guest. */
    struct yos_fork_region live_regions[YOS_FORK_MAX_REGIONS];
    uint32_t live_region_count;
    /* Snapshot of parent's fd_map; child duplicates each entry to a
     * fresh host fd at startup so the runtimes have independent
     * close/dup2 semantics. */
    int parent_fd_map[YOS_FD_MAX];
    /* Parent's tracked cwd. Without this the child's ctx->cwd is the
     * calloc'd zero-string and getcwd returns nothing useful — it also
     * decouples relative-path lookup from the host process cwd, which
     * is shared across forks (they're all pthreads of one host pid). */
    char parent_cwd[PATH_MAX];
    /* Parent's per-process signal state — child inherits everything
     * on fork per POSIX. handlers stay valid because they're wasm
     * function-table indices, and the child runs the same wasm
     * binary post-fork (asyncify rewind) so the table is identical. */
    uint32_t parent_sig_handlers[32];
    uint32_t parent_sig_ignore_mask;
    uint32_t parent_sig_mask;
    uint32_t parent_sig_pending;
    /* Per-ctx locale name. host setlocale() is process-wide; we keep
     * the FreeBSD-libc query view per-ctx via ctx->locale_name. */
    char     parent_locale_name[64];
    /* Per-ctx FILE* table. We dup the underlying host FILE* into
     * child-owned FILE*s via fdopen(dup(fileno)) so each side's
     * fclose only affects its own copy. file_dup_fps[] holds the
     * already-dup'd child FILE*s (allocated in yos_fork_pump); the
     * child thread just copies them into child_ctx->file_slots. */
    void    *parent_file_dup_fps[256];
    int32_t  parent_file_dup_wfds[256];
    char     parent_file_modes[256][8];
    /* Parent's env_store. Holds wasm-memory offsets into env name
     * and value strings. The strings themselves live in the wasm
     * linear memory and are copied by the memory snapshot/restore;
     * we just need the index table copied here so the child sees
     * the same env entries the parent had at fork time. Without
     * this, child's setenv mutations leak back into the parent's
     * shared env_store. */
    struct {
        struct yos_env_entry e[YOS_ENV_MAX];
        int    count;
        int    initialised;
    } parent_env_store;
    /* Parent's umask. The host kernel's per-process umask is shared
     * across every yos guest (they're host pthreads of one process),
     * so child's umask() would leak back into parent without this. */
    unsigned short parent_umask;
    /* forkpty stash (see types.h): the child's rewound forkpty bridge
     * wires the slave onto its stdio using these guest fd numbers. */
    int forkpty_pending;
    int32_t forkpty_master_wfd;
    int32_t forkpty_slave_wfd;
} fork_thread_arg_t;

/* Release every dup the parent thread stashed in fork_thread_arg.
 * Used on any failure path where the child can't take ownership of
 * the dups — left unreleased, they'd burn one host fd per failed
 * fork until yos --server hits RLIMIT_NOFILE. parent_fd_map entries
 * are raw host fds (close them); parent_file_dup_fps are fdopen'd
 * FILE* handles around their own host fd (fclose closes both). */
static void release_parent_dups(fork_thread_arg_t *a)
{
    for (int i = 0; i < YOS_FD_MAX; i++) {
        if (a->parent_fd_map[i] >= 0) {
            close(a->parent_fd_map[i]);
            a->parent_fd_map[i] = -1;
        }
    }
    for (int i = 0; i < 256; i++) {
        FILE *fp = (FILE *)a->parent_file_dup_fps[i];
        if (!fp) continue;
        a->parent_file_dup_fps[i] = NULL;
        a->parent_file_dup_wfds[i] = -1;
        a->parent_file_modes[i][0] = '\0';
        fclose(fp);
    }
}

static void *fork_thread_func(void *arg)
{
    ydebug("fork_thread_func: child thread tid=%d\n", (int)(uintptr_t)pthread_self());
    fork_thread_arg_t *fork_thread_arg = (fork_thread_arg_t *)arg;
    /* Label this thread's per-thread trace file with the inheriting
     * proc's comm so YTRACE_FILE_PREFIX produces readable filenames
     * (e.g. "trace-zsh-1234" rather than "trace-yos-1234"). The
     * child inherits comm from the parent at fork; execve later
     * relabels via yos_ytrace_set_comm in the exec block. */
    if (fork_thread_arg->proc && fork_thread_arg->proc->comm[0])
        yos_ytrace_set_comm(fork_thread_arg->proc->comm);

    /* Serialise the wasm3 module-load path across concurrent forks.
     * yos's fork model spawns a host pthread that calls
     * m3_NewEnvironment / m3_NewRuntime / m3_ParseModule /
     * m3_LoadModule. When two ctxs fork at the same wall-clock
     * time (e.g. two perf-stresses in two telnet sessions under
     * `yos --server`), the second host thread's load races with
     * the first in wasm3 internals and crashes the host process
     * with SIGSEGV inside m3Error. The crash dump consistently
     * shows last_bridge=fork after a mid-test wait3 phase.
     *
     * Until the wasm3 source is audited for thread-safety, lock
     * around the full setup. Module load is fast (sub-ms) so
     * concurrent fork throughput barely suffers; correctness
     * comes first. */
    static pthread_mutex_t fork_setup_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&fork_setup_lock);

    /* Create new wasm3 environment and runtime for child */
    IM3Environment env = m3_NewEnvironment();
    /* 4 MiB wasm3 operand stack — matches the initial-process budget
     * in main.c::load_wasm_module. 64 KiB was too small for nvim's
     * Vim-script eval chain reached via fork+exec under the
     * stub-reduction regression that motivated issue #6. */
    IM3Runtime rt = m3_NewRuntime(env, 4 * 1024 * 1024, NULL);
    if (!rt) {
        pthread_mutex_unlock(&fork_setup_lock);
        release_parent_dups(fork_thread_arg);
        munmap(fork_thread_arg->memory_snapshot, fork_thread_arg->memory_size);
        free(fork_thread_arg->wasm_globals);
        free(fork_thread_arg);
        return NULL;
    }

    /* Create child exec context */
    struct yos_exec_ctx *child_ctx = calloc(1, sizeof(struct yos_exec_ctx));
    if (!child_ctx) {
        pthread_mutex_unlock(&fork_setup_lock);
        release_parent_dups(fork_thread_arg);
        m3_FreeRuntime(rt);
        m3_FreeEnvironment(env);
        munmap(fork_thread_arg->memory_snapshot, fork_thread_arg->memory_size);
        free(fork_thread_arg->wasm_globals);
        free(fork_thread_arg);
        return NULL;
    }

    child_ctx->rt = fork_thread_arg->rt;
    child_ctx->proc = fork_thread_arg->proc;
    /* Record this thread on the child proc so kill(child_pid)/tkill in
     * the guest namespace can resolve the guest pid back to a real
     * pthread via deliver_to_proc(). */
    if (child_ctx->proc) {
        child_ctx->proc->thread     = pthread_self();
        child_ctx->proc->ctx_handle = child_ctx;
    }
    child_ctx->runtime = rt;
    child_ctx->heap_end = fork_thread_arg->heap_end;
    /* Inherit allocator bookmarks so the child reuses the parent's
     * carved heap instead of re-running alloc_init_locked on top of
     * it. See header comment on fork_thread_arg_t::alloc_lo. */
    child_ctx->alloc_lo        = fork_thread_arg->alloc_lo;
    child_ctx->alloc_hi        = fork_thread_arg->alloc_hi;
    child_ctx->alloc_free_head = fork_thread_arg->alloc_free_head;
    /* Inherit mmap cursor + free list so child's next mmap2 carves
     * AFTER the parent's existing mmap regions, not at the same
     * starting address. */
    child_ctx->mmap_top   = fork_thread_arg->mmap_top;
    child_ctx->free_count = fork_thread_arg->free_count;
    memcpy(child_ctx->free_list, fork_thread_arg->free_list,
           sizeof(child_ctx->free_list));
    child_ctx->asyncify_ptr = fork_thread_arg->asyncify_ptr;
    child_ctx->sj_discard_ptr  = fork_thread_arg->sj_discard_ptr;
    /* TODO(setjmp-refactor): copy parent's sj_slots[] into child. */
    child_ctx->fork_return = 0;  /* child gets 0 from fork */
    child_ctx->is_child = 1;
    child_ctx->forkpty_pending    = fork_thread_arg->forkpty_pending;
    child_ctx->forkpty_master_wfd = fork_thread_arg->forkpty_master_wfd;
    child_ctx->forkpty_slave_wfd  = fork_thread_arg->forkpty_slave_wfd;
    /* errno_off MUST match the parent's (set in main.c::load_wasm_module).
     * Without this child_ctx->errno_off stays at calloc'd 0, env.__error
     * returns 0, and the wasm guest reads/writes errno through
     * memory[0..3] — which is also where nvim's wasm-libc keeps the
     * thread-pointer / stack-protector canary. Result: errno-reads
     * after a failed lstat see the canary value (nonzero, constant)
     * instead of ENOENT, libuv's `UV__ERR(errno)` produces a bogus
     * negative number, nvim's `os_fileinfo_link` thinks every swap-name
     * variant exists, and findswapname surfaces "E326: Too many swap
     * files found" + E303. */
    child_ctx->errno_off = 0x108;
    /* Give the child its own host fds for each of the parent's open
     * wasm fds, so close/dup2 in one runtime doesn't trample the
     * other's. POSIX fork preserves FD_CLOEXEC; F_DUPFD strips it,
     * so re-set it on the dup when the source had it. The signal
     * pipe libuv uses for spawn-success detection has CLOEXEC and
     * relies on this — without it the post-pseudo-exec failure path
     * keeps writing into a pipe the parent treats as live, so the
     * parent thinks the spawn failed and never paints. */
    /* Copy the parent's fd dups straight in. The parent thread
     * already F_DUPFD'd each entry in yos_fork_pump (BEFORE
     * resuming the parent's wasm code), so parent_fd_map here
     * holds STABLE host fds that point to the kernel objects the
     * parent had at fork time — NOT host fd numbers that race
     * against the parent's post-fork close/dup2 traffic. */
    for (int i = 0; i < YOS_FD_MAX; i++) {
        child_ctx->fd_map[i] = fork_thread_arg->parent_fd_map[i];
    }
    child_ctx->argc = fork_thread_arg->argc;
    child_ctx->argv = fork_thread_arg->argv;
    child_ctx->envc = fork_thread_arg->envc;
    child_ctx->envp = fork_thread_arg->envp;
    child_ctx->wasm_bytes = fork_thread_arg->wasm_bytes;
    child_ctx->wasm_bytes_size = fork_thread_arg->wasm_bytes_size;
    memcpy(child_ctx->cwd, fork_thread_arg->parent_cwd, sizeof(child_ctx->cwd));
    /* POSIX fork: child inherits parent's full signal state. */
    memcpy(child_ctx->sig_handlers, fork_thread_arg->parent_sig_handlers,
           sizeof(child_ctx->sig_handlers));
    child_ctx->sig_ignore_mask = fork_thread_arg->parent_sig_ignore_mask;
    child_ctx->sig_mask    = fork_thread_arg->parent_sig_mask;
    child_ctx->sig_pending = fork_thread_arg->parent_sig_pending;
    memcpy(child_ctx->locale_name, fork_thread_arg->parent_locale_name,
           sizeof(child_ctx->locale_name));
    /* Inherit per-ctx FILE* table. yos_fork_pump pre-dup'd each
     * live parent FILE* via fdopen(dup(fileno(...))) so the child
     * gets independent host FILE*s — fclose on one side doesn't
     * affect the other. */
    for (int i = 0; i < 256; i++) {
        child_ctx->file_slots[i] = fork_thread_arg->parent_file_dup_fps[i];
        child_ctx->file_wfds[i]  = fork_thread_arg->parent_file_dup_wfds[i];
        memcpy(child_ctx->file_modes[i], fork_thread_arg->parent_file_modes[i],
               sizeof(child_ctx->file_modes[i]));
    }
    /* POSIX fork: child inherits the env at the moment of fork.
     * Subsequent setenv/unsetenv calls in either side stay local
     * to that ctx. */
    memcpy(&child_ctx->env_store, &fork_thread_arg->parent_env_store,
           sizeof(child_ctx->env_store));
    /* POSIX fork: child inherits parent's umask. */
    child_ctx->umask = fork_thread_arg->parent_umask;
    pthread_mutex_init(&child_ctx->mem_lock, NULL);

    rt->userdata = child_ctx;

    /* Parse and load module */
    IM3Module mod;
    M3Result res = m3_ParseModule(env, &mod, fork_thread_arg->wasm_bytes, fork_thread_arg->wasm_bytes_size);
    if (res) {
        pthread_mutex_unlock(&fork_setup_lock);
        /* child_ctx already adopted parent_fd_map / file_slots above;
         * close THOSE (not the parent_* copies) so we don't
         * double-close the same kernel objects. */
        for (int i = 0; i < YOS_FD_MAX; i++) {
            if (child_ctx->fd_map[i] >= 0) close(child_ctx->fd_map[i]);
        }
        for (int i = 0; i < 256; i++) {
            FILE *fp = (FILE *)child_ctx->file_slots[i];
            if (fp) fclose(fp);
        }
        free(child_ctx);
        m3_FreeRuntime(rt);
        m3_FreeEnvironment(env);
        munmap(fork_thread_arg->memory_snapshot, fork_thread_arg->memory_size);
        free(fork_thread_arg->wasm_globals);
        free(fork_thread_arg);
        return NULL;
    }

    res = m3_LoadModule(rt, mod);
    if (res) {
        pthread_mutex_unlock(&fork_setup_lock);
        for (int i = 0; i < YOS_FD_MAX; i++) {
            if (child_ctx->fd_map[i] >= 0) close(child_ctx->fd_map[i]);
        }
        for (int i = 0; i < 256; i++) {
            FILE *fp = (FILE *)child_ctx->file_slots[i];
            if (fp) fclose(fp);
        }
        free(child_ctx);
        m3_FreeRuntime(rt);
        m3_FreeEnvironment(env);
        munmap(fork_thread_arg->memory_snapshot, fork_thread_arg->memory_size);
        free(fork_thread_arg->wasm_globals);
        free(fork_thread_arg);
        return NULL;
    }

    /* Module loaded; release the setup lock so the next concurrent
     * fork can start its own setup in parallel with our child's
     * runtime initialisation below. */
    pthread_mutex_unlock(&fork_setup_lock);

    child_ctx->module = mod;

    /* Link syscall functions to child runtime */
    extern void yos_link_imports(IM3Module module, struct yos_exec_ctx *ctx);
    yos_link_imports(mod, child_ctx);

    /* Grow memory to match parent BEFORE restoring snapshot. Pages
     * = parent's memory_size / 64 KiB. Matching exactly avoids both
     * over-commit (child paying for memory the parent never sized
     * to) and under-commit (snapshot bytes that don't fit). */
    extern M3Result ResizeMemory(IM3Runtime, uint32_t);
    ResizeMemory(rt, fork_thread_arg->memory_size / 65536);

    /* Restore memory from snapshot. Child's freshly resized linear
     * memory is zero from mmap, so we only need to memcpy the LIVE
     * regions the parent recorded. live_region_count==0 means the
     * parent had to fall back (region overflow / no /proc/self/maps),
     * in which case copy the whole thing — correctness over speed. */
    uint32_t mem_size;
    uint8_t *mem = m3_GetMemory(rt, &mem_size, 0);
    if (mem && fork_thread_arg->memory_size <= mem_size) {
        if (fork_thread_arg->live_region_count > 0) {
            uint64_t bytes_copied = 0;
            for (uint32_t i = 0; i < fork_thread_arg->live_region_count; i++) {
                uint32_t off = fork_thread_arg->live_regions[i].off;
                uint32_t len = fork_thread_arg->live_regions[i].len;
                if (off + len > fork_thread_arg->memory_size) continue;
                memcpy(mem + off,
                       fork_thread_arg->memory_snapshot + off,
                       len);
                bytes_copied += len;
            }
            ydebug("child: restored %u regions, %llu bytes (skipped "
                   "%llu zero bytes of %u total)\n",
                   fork_thread_arg->live_region_count,
                   (unsigned long long)bytes_copied,
                   (unsigned long long)(fork_thread_arg->memory_size - bytes_copied),
                   fork_thread_arg->memory_size);
        } else {
            memcpy(mem, fork_thread_arg->memory_snapshot,
                   fork_thread_arg->memory_size);
            ydebug("child: restored %u bytes of memory (full fallback)\n",
                   fork_thread_arg->memory_size);
        }
    } else {
        ydebug("child: FAILED to restore memory! have %u, need %u\n",
               mem_size, fork_thread_arg->memory_size);
    }
    child_ctx->memory = mem;
    child_ctx->memory_size = mem_size;

    /* Restore globals (critical: includes stack pointer!) */
    for (uint32_t i = 0; i < fork_thread_arg->wasm_globals_count && i < mod->numGlobals; i++) {
        mod->globals[i].intValue = fork_thread_arg->wasm_globals[i];
    }
    ydebug("child: restored %u globals\n", fork_thread_arg->wasm_globals_count);

    munmap(fork_thread_arg->memory_snapshot, fork_thread_arg->memory_size);
    free(fork_thread_arg->wasm_globals);
    free(fork_thread_arg);

    /* Start rewind - when _start is called, it will rewind to fork point */
    call_asyncify(rt, "asyncify_start_rewind", child_ctx->asyncify_ptr);

    ydebug("child pid=%d starting rewind\n", child_ctx->proc->pid);

    /* Child exec loop - run _start, reload module if exec happens */
    for (;;) {
        IM3Function start_fn;
        res = m3_FindFunction(&start_fn, rt, "_start");
        if (res) {
            ydebug("child: _start not found: %s\n", res);
            child_ctx->proc->state = YOS_PROC_ZOMBIE;
            child_ctx->proc->exit_code = 127;
            child_ctx->proc->exited = 1;
            break;
        }

        res = m3_CallV(start_fn);

        /* Drive setjmp/longjmp AND fork asyncify round-trips until
         * they settle. main.c's loop only services the initial
         * process — a forked child that itself calls fork() (nvim
         * spawning its UI helper, zsh running $(command)
         * substitution, …) lands here, and without pumping fork
         * the grand-child thread never gets spawned and the parent
         * falls through to "child finished, mark zombie". Mirror
         * main.c's pump loop. */
        extern void yos_setjmp_pump(struct yos_exec_ctx *);
        extern void yos_fork_pump  (struct yos_exec_ctx *);
        for (;;) {
            int progress = 0;
            if (child_ctx->fork_pending) {
                yos_fork_pump(child_ctx);
                progress = 1;
            }
            if (child_ctx->setjmp_pending || child_ctx->longjmp_pending) {
                yos_setjmp_pump(child_ctx);
                progress = 1;
            }
            if (child_ctx->pump_trap) break;
            if (!progress) break;
        }
        /* Surface a pump-internal trap as the loop's res. Without this,
         * a wasm crash inside the rewound _start would silently fall into
         * the "clean exit" branch. */
        if (child_ctx->pump_trap) {
            res = child_ctx->pump_trap;
            child_ctx->pump_trap = NULL;
        }

        /* Check if exec happened */
        if (!child_ctx->exec_pending) {
            if (res) {
                ydebug("child: trap: %s\n", res);
                child_ctx->proc->state = YOS_PROC_ZOMBIE;
                child_ctx->proc->exit_code = 139;
                child_ctx->proc->exited = 1;
            }
            break;
        }

        /* Handle exec - load new module */
        ydebug("child exec: loading %s\n", child_ctx->exec_path);

        /* Drop the pre-exec pthread_host BEFORE freeing the old
         * runtime — the host pins the old IM3Runtime as its master
         * and the old wasm-bytes pointer. Once m3_FreeRuntime fires
         * those become dangling, and a later pthread_create would
         * dereference them. Lazy-create happens further down when
         * yos_link_imports runs against the new module. */
        if (child_ctx->pthread_host) {
            yos_pthread_host_destroy(
                (yos_pthread_host *)child_ctx->pthread_host);
            child_ctx->pthread_host = NULL;
        }

        /* The guest-facing allocator (impl/alloc.c) keeps all its
         * state INSIDE ctx->memory now — no host-side registry to
         * dangle when m3_FreeRuntime frees the linear memory. Safe
         * to release the old runtime here. */
        m3_FreeRuntime(rt);
        m3_FreeEnvironment(env);

        /* The old wasm linear memory just went away — every wasm
         * offset cached host-side (env table, anything else keyed by
         * pointer-into-memory) is now stale. Reset before any guest
         * code runs that could read those caches. */
        {
            extern void yos_env_post_execve_reset(struct yos_exec_ctx *);
            extern void yos_pwd_post_execve_reset(struct yos_exec_ctx *);
            extern void yos_freebsd_userland_post_execve_reset(struct yos_exec_ctx *);
            extern void yos_iconv_ctx_free(struct yos_exec_ctx *);
            yos_env_post_execve_reset(child_ctx);
            yos_pwd_post_execve_reset(child_ctx);
            yos_freebsd_userland_post_execve_reset(child_ctx);
            /* exec replaces the image: any iconv handles the old image
             * opened are meaningless to the new one — release the host
             * iconv_t objects behind them. */
            yos_iconv_ctx_free(child_ctx);
        }

        child_ctx->argc = child_ctx->exec_argc;
        child_ctx->argv = child_ctx->exec_argv;
        /* Install the env that was passed to execve (if any). NULL
         * means "no override" — keep inherited envp so a forked child
         * that execs with envp=environ keeps the parent's environment. */
        if (child_ctx->exec_envp) {
            child_ctx->envc = child_ctx->exec_envc;
            child_ctx->envp = child_ctx->exec_envp;
        }

        env = m3_NewEnvironment();
        /* Match load_wasm_module / fork's 4 MiB budget. */
        rt = m3_NewRuntime(env, 4 * 1024 * 1024, NULL);
        if (!rt) {
            ydebug("child exec: failed to create runtime\n");
            child_ctx->proc->state = YOS_PROC_ZOMBIE;
            child_ctx->proc->exit_code = 127;
            child_ctx->proc->exited = 1;
            break;
        }

        FILE *f = fopen(child_ctx->exec_path, "rb");
        if (!f) {
            ydebug("child exec: cannot open %s\n", child_ctx->exec_path);
            m3_FreeRuntime(rt);
            m3_FreeEnvironment(env);
            rt = NULL;
            env = NULL;
            child_ctx->proc->state = YOS_PROC_ZOMBIE;
            child_ctx->proc->exit_code = 127;
            child_ctx->proc->exited = 1;
            break;
        }
        fseek(f, 0, SEEK_END);
        size_t wasm_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t *wasm_bytes = malloc(wasm_size);
        if (!wasm_bytes || fread(wasm_bytes, 1, wasm_size, f) != wasm_size) {
            fclose(f);
            free(wasm_bytes);
            m3_FreeRuntime(rt);
            m3_FreeEnvironment(env);
            rt = NULL;
            env = NULL;
            child_ctx->proc->state = YOS_PROC_ZOMBIE;
            child_ctx->proc->exit_code = 127;
            child_ctx->proc->exited = 1;
            break;
        }
        fclose(f);

        res = m3_ParseModule(env, &mod, wasm_bytes, wasm_size);
        if (res) {
            ydebug("child exec: parse error: %s\n", res);
            free(wasm_bytes);
            m3_FreeRuntime(rt);
            m3_FreeEnvironment(env);
            rt = NULL;
            env = NULL;
            child_ctx->proc->state = YOS_PROC_ZOMBIE;
            child_ctx->proc->exit_code = 127;
            child_ctx->proc->exited = 1;
            break;
        }

        res = m3_LoadModule(rt, mod);
        if (res) {
            ydebug("child exec: load error: %s\n", res);
            free(wasm_bytes);
            m3_FreeRuntime(rt);
            m3_FreeEnvironment(env);
            rt = NULL;
            env = NULL;
            child_ctx->proc->state = YOS_PROC_ZOMBIE;
            child_ctx->proc->exit_code = 127;
            child_ctx->proc->exited = 1;
            break;
        }

        child_ctx->runtime = rt;
        child_ctx->module = mod;
        child_ctx->wasm_bytes = wasm_bytes;
        child_ctx->wasm_bytes_size = wasm_size;
        rt->userdata = child_ctx;
        yos_link_imports(mod, child_ctx);

        extern M3Result ResizeMemory(IM3Runtime, uint32_t);
        /* Use the just-loaded module's declared max (capped at 4096 pages
         * = 256 MiB for nvim's sake). Avoids over-committing memory the
         * execed binary doesn't need — fork after exec then snapshots a
         * smaller arena. YOS_WASM_PAGES overrides the default so
         * sandbox-constrained hosts (tvOS/iOS app bundles, where every
         * forked wasm process eats its share of a ≤1 GiB total budget)
         * can shrink it. */
        {
            uint32_t pages = 4096;
            const char *env_pages = getenv("YOS_WASM_PAGES");
            if (env_pages && *env_pages) {
                char *e = NULL;
                unsigned long v = strtoul(env_pages, &e, 10);
                if (e && *e == '\0' && v > 0 && v <= 65536u) {
                    pages = (uint32_t)v;
                }
            }
            if (mod->memoryInfo.maxPages > 0 &&
                mod->memoryInfo.maxPages < pages)
                pages = mod->memoryInfo.maxPages;
            if (mod->memoryInfo.initPages > pages)
                pages = mod->memoryInfo.initPages;
            ResizeMemory(rt, pages);
        }

        uint32_t mem_size;
        child_ctx->memory = m3_GetMemory(rt, &mem_size, 0);
        child_ctx->memory_size = mem_size;
        /* Heap starts at __heap_base — see comment in main.c:load_wasm_module
         * for why a hardcoded constant corrupts .data. Falls back to
         * 0x50000 if the wasm doesn't export __heap_base. */
        child_ctx->heap_end = 0x50000;
        {
            IM3Global g = m3_FindGlobal(mod, "__heap_base");
            if (g) {
                M3TaggedValue tv = { 0 };
                M3Result gr = m3_GetGlobal(g, &tv);
                if (!gr && tv.type == c_m3Type_i32) {
                    uint32_t hb = (tv.value.i32 + 15) & ~15u;
                    if (hb > child_ctx->heap_end) child_ctx->heap_end = hb;
                }
            }
        }
        /* Fresh module → fresh address space; let mmap2 re-anchor
         * itself at memory_size/2 by clearing the watermark. */
        child_ctx->mmap_top = 0;
        child_ctx->free_count = 0;
        /* Allocator state lives IN the (just-replaced) linear memory.
         * Reset the wasm-offset bookmarks so the new image's first
         * malloc lazy-inits a fresh heap in the new memory. */
        child_ctx->alloc_lo = 0;
        child_ctx->alloc_hi = 0;
        child_ctx->alloc_free_head = 0;
        /* Stale setjmp slots from the pre-execve image refer to
         * jmp_buf addresses in the OLD module's stack. After execve
         * they are dead but sj_alloc_slot still treats them as
         * occupied — the next setjmp gets pushed to a high slot index
         * whose asyncify_buf offset (mem_size - (4+i)*64K) lands in
         * a region the new module already uses. Reset them here so
         * the freshly loaded module starts with a clean slot pool. */
        memset(child_ctx->sj_slots, 0, sizeof(child_ctx->sj_slots));
        child_ctx->setjmp_pending = 0;
        child_ctx->setjmp_pending_slot = -1;
        child_ctx->longjmp_pending = 0;
        child_ctx->longjmp_target  = 0;
        child_ctx->longjmp_value   = 0;
        child_ctx->sj_discard_ptr  = 0;
        child_ctx->asyncify_ptr    = 0;
        /* (pthread_host was already destroyed earlier, BEFORE the
         * m3_FreeRuntime that invalidated its master pointer.) */

        uint32_t *thread_ptr = (uint32_t *)(child_ctx->memory + 0);
        *thread_ptr = 0x100;
        memset(child_ctx->memory + 0x100, 0, 256);

        /* execve(2) replaces the process image — update comm and exe
         * on the yos_proc so /proc/<pid>/{stat,comm,exe} reflect the
         * new program. Same reasoning as the matching update in
         * main.c's top-level exec_pending loop; without this, a
         * forked-and-exec'd child keeps the parent's COMMAND in ps
         * output. */
        if (child_ctx->proc) {
            const char *slash = strrchr(child_ctx->exec_path, '/');
            const char *base  = slash ? slash + 1 : child_ctx->exec_path;
            strncpy(child_ctx->proc->comm, base,
                    sizeof(child_ctx->proc->comm) - 1);
            child_ctx->proc->comm[sizeof(child_ctx->proc->comm) - 1] = '\0';
            strncpy(child_ctx->proc->exe, child_ctx->exec_path,
                    sizeof(child_ctx->proc->exe) - 1);
            child_ctx->proc->exe[sizeof(child_ctx->proc->exe) - 1] = '\0';
            yos_ytrace_set_comm(child_ctx->proc->comm);
        }

        /* POSIX execve: every fd with FD_CLOEXEC set must be closed.
         * Without this, a shell that opens many fds for ZLE / job
         * control passes them all through to the exec'd child, which
         * then sees /dev/null at fd 9 instead of fd 3 — and worse,
         * tools that closefrom(3) miss them because yos's fd table
         * is virtual. Query each host fd's FD_CLOEXEC bit and drop
         * the close-on-exec ones from the child's fd_map. */
        for (int i = 3; i < YOS_FD_MAX; i++) {
            int hfd = child_ctx->fd_map[i];
            if (hfd < 0) continue;
            int flags = fcntl(hfd, F_GETFD);
            if (flags >= 0 && (flags & FD_CLOEXEC)) {
                close(hfd);
                child_ctx->fd_map[i] = -1;
            }
        }

        child_ctx->exec_pending = 0;
        child_ctx->exec_argv = NULL;
        child_ctx->exec_argc = 0;
        child_ctx->exec_envp = NULL;
        child_ctx->exec_envc = 0;

        ydebug("child exec: loaded %s, argc=%d\n", child_ctx->exec_path, child_ctx->argc);
    }

    /* Child finished - mark as zombie ONLY if still alive. The
     * SIGKILL path (deliver_to_proc) already marks the proc ZOMBIE
     * and the parent's waitpid may have already reaped it AND set
     * the slot back to FREE before pthread_cancel finishes here.
     * Re-marking a FREE slot as ZOMBIE resurrects a stale (pid,
     * ppid) tuple in the proc table — subsequent waitpid(-1,
     * WNOHANG) "finds" the same kid again as a phantom zombie. */
    pthread_mutex_lock(&child_ctx->rt->proc_lock);
    if (child_ctx->proc->state == YOS_PROC_READY ||
        child_ctx->proc->state == YOS_PROC_RUNNING) {
        child_ctx->proc->state = YOS_PROC_ZOMBIE;
        child_ctx->proc->exit_code = 0;
        child_ctx->proc->exited = 1;
    }
    pthread_cond_broadcast(&child_ctx->proc->wait_cond);
    pthread_mutex_unlock(&child_ctx->rt->proc_lock);

    /* Runtime-wide "something exited" event for main.c's shutdown
     * wait. Covers every break-out-of-loop path above (trap, exec
     * load failure, etc.) since they all fall through to here. */
    pthread_mutex_lock(&child_ctx->rt->proc_lock);
    pthread_cond_broadcast(&child_ctx->rt->any_exit_cond);
    pthread_mutex_unlock(&child_ctx->rt->proc_lock);

    ydebug("child pid=%d exited (post-m3_CallV cleanup)\n", child_ctx->proc->pid);

    m3_FreeRuntime(rt);
    m3_FreeEnvironment(env);
    free(child_ctx);

    return NULL;
}

/* Snapshot wasm linear memory using mincore() to skip the untouched
 * (lazy zero) bulk.
 *
 * The wasm linear arena is one big anonymous mmap (via wasm3's
 * realloc → glibc's large-alloc path). Only the pages the guest has
 * actually touched are backed by real RAM; the rest are mapped to
 * the kernel's shared zero page. A naive `memcpy(dst, src, 256MiB)`
 * READS every single page — forcing a fault per untouched 4 KiB
 * slot (~2 µs each, ~130 ms for 256 MiB) AND a corresponding write
 * fault on dst. Doubled by the child's restore = ~520 ms / fork.
 *
 * mincore() tells us, for each page in [src, src+size), whether it
 * is currently resident (bit 0 of vec[i]). We memcpy only the runs
 * of resident pages and record them so the child can replay just
 * those memcpys. Untouched pages stay lazy-zero on both sides.
 *
 * Portable across Linux / FreeBSD / macOS / iOS / tvOS — bit 0 has
 * the "in core" meaning on all of them. Linux 5.2+ requires the
 * memory to be ours (anonymous private), which it always is here.
 *
 * Returns 0 on success, -1 on region-table overflow OR mincore
 * failure. On overflow the snapshot is still byte-correct (every
 * resident page was copied) but the live-region list is incomplete,
 * so the caller MUST fall back to a full-size memcpy at restore. */
/* The host kernel's page size — what mincore() reports residency at.
 * Linux x86_64 + macOS x86_64: 4 KiB. macOS arm64 (Apple Silicon) and
 * iOS/tvOS arm64: 16 KiB. We probe at first use with sysconf rather
 * than hard-coding, because a mismatch makes mincore's vec[] reads
 * randomized — every fourth slot is "set" on a 4 K walk over 16 K
 * pages — and the live-region copy then misses three quarters of the
 * actually-resident pages (including the asyncify state buffer that
 * the child's rewind reads back). Symptom: every child wasm process
 * traps at `child: trap: [trap] out of bounds memory access` on
 * rewind. */
static uintptr_t yos_page_size(void)
{
    static uintptr_t cached = 0;
    if (!cached) {
        long ps = sysconf(_SC_PAGESIZE);
        cached = (ps > 0) ? (uintptr_t)ps : 4096u;
    }
    return cached;
}
#define YOS_PAGE_SIZE (yos_page_size())
static int snapshot_wasm_memory(uint8_t *dst, const uint8_t *src,
                                size_t size,
                                struct yos_fork_region *out_regions,
                                uint32_t out_regions_max,
                                uint32_t *out_count)
{
    *out_count = 0;
    int overflow = 0;
    uint64_t total_live = 0;

    /* mincore() requires a page-aligned start address. wasm3 places
     * an M3MemoryHeader right before the linear-memory pointer it
     * returns from m3_GetMemory, so `src` is base + sizeof(header) —
     * not page-aligned. Handle the unaligned head as one always-
     * copied partial page; mincore the aligned middle; handle any
     * unaligned tail the same way. The partial pages are tiny
     * (≤ 4 KiB) so unconditional copy is fine and keeps them
     * always-recorded (mincore can't tell us anything useful about
     * a page that straddles the header). */
    uintptr_t s = (uintptr_t)src;
    uintptr_t aligned_start = (s + YOS_PAGE_SIZE - 1) & ~(uintptr_t)(YOS_PAGE_SIZE - 1);
    size_t head_len = (aligned_start > s) ? (aligned_start - s) : 0;
    if (head_len > size) head_len = size;
    if (head_len > 0) {
        memcpy(dst, src, head_len);
        total_live += head_len;
        if (*out_count < out_regions_max) {
            out_regions[*out_count].off = 0;
            out_regions[*out_count].len = (uint32_t)head_len;
            (*out_count)++;
        } else {
            overflow = 1;
        }
    }

    size_t remaining = size - head_len;
    size_t middle_size = remaining & ~(size_t)(YOS_PAGE_SIZE - 1);
    size_t tail_len = remaining - middle_size;

    if (middle_size > 0) {
        const uint8_t *src_mid = src + head_len;
        size_t npages = middle_size / YOS_PAGE_SIZE;
        unsigned char *vec = malloc(npages);
        if (!vec) return -1;
        if (mincore((void *)src_mid, middle_size, vec) < 0) {
            ydebug("snapshot: mincore(addr=%p size=%zu) failed: %s\n",
                   (void *)src_mid, middle_size, strerror(errno));
            free(vec);
            return -1;
        }
        size_t i = 0;
        while (i < npages) {
            if (!(vec[i] & 1u)) { i++; continue; }
            size_t start = i;
            while (i < npages && (vec[i] & 1u)) i++;
            size_t end = i;
            size_t off_in_mid = start * YOS_PAGE_SIZE;
            size_t len        = (end - start) * YOS_PAGE_SIZE;
            size_t off        = head_len + off_in_mid;
            memcpy(dst + off, src + off, len);
            total_live += len;
            if (*out_count < out_regions_max) {
                out_regions[*out_count].off = (uint32_t)off;
                out_regions[*out_count].len = (uint32_t)len;
                (*out_count)++;
            } else {
                overflow = 1;
            }
        }
        free(vec);
    }

    if (tail_len > 0) {
        size_t off = head_len + middle_size;
        memcpy(dst + off, src + off, tail_len);
        total_live += tail_len;
        if (*out_count < out_regions_max) {
            out_regions[*out_count].off = (uint32_t)off;
            out_regions[*out_count].len = (uint32_t)tail_len;
            (*out_count)++;
        } else {
            overflow = 1;
        }
    }

    ydebug("snapshot: %u live runs, %llu live bytes of %zu (%.1f%%)\n",
           *out_count, (unsigned long long)total_live, size,
           size ? 100.0 * (double)total_live / (double)size : 0.0);
    return overflow ? -1 : 0;
}

/* yos_fork_pump - called after WASM execution returns */
void yos_fork_pump(struct yos_exec_ctx *ctx)
{
    while (ctx->fork_pending) {
        ctx->fork_pending = 0;

        IM3Runtime wrt = (IM3Runtime)ctx->runtime;
        IM3Module mod = (IM3Module)ctx->module;

        /* Stop unwind - buffer now contains saved call stack */
        call_asyncify(wrt, "asyncify_stop_unwind", -1);
        ydebug("unwind stopped, copying state for child\n");

        /* Copy memory AFTER unwind */
        uint32_t mem_size;
        uint8_t *mem = m3_GetMemory(wrt, &mem_size, 0);
        /* Check if asyncify save overflowed our buffer. The save area
         * lives at [asyncify_ptr+8, asyncify_ptr+ASYNCIFY_BUF_SIZE).
         * Overflow here writes past the buffer into whatever sits below
         * (often the sj_slot region, the wasm stack, or the data section
         * including __stack_chk_guard) and shows up later as random
         * canary smashes. */
        {
            uint32_t *hdr = (uint32_t *)(mem + ctx->asyncify_ptr);
            uint32_t used = hdr[0] - (ctx->asyncify_ptr + 8);
            uint32_t cap  = hdr[1] - (ctx->asyncify_ptr + 8);
            if (hdr[0] > hdr[1] || used > cap) {
                fprintf(stderr,
                        "yos: fork asyncify save OVERFLOWED — used=%u cap=%u "
                        "(buffer at %u, ASYNCIFY_BUF_SIZE=%d). Increase "
                        "ASYNCIFY_BUF_SIZE.\n",
                        used, cap, ctx->asyncify_ptr, ASYNCIFY_BUF_SIZE);
            } else {
                ydebug("fork asyncify save used=%u/%u bytes\n", used, cap);
            }
        }
        /* Anonymous mmap gives lazily-faulted zero pages — only
         * touched pages cost physical RAM. Replaces malloc+memset
         * which would commit 256 MiB up front on every fork. */
        uint8_t *mem_copy = mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem_copy == MAP_FAILED) {
            struct yos_proc *child = yos_proc_find(ctx->rt, ctx->fork_return);
            if (child) child->state = YOS_PROC_FREE;
            return;
        }

        /* Prepare thread argument up front so snapshot_wasm_memory
         * can fill its live-region map directly into the struct. */
        fork_thread_arg_t *fork_thread_arg = malloc(sizeof(fork_thread_arg_t));
        if (!fork_thread_arg) {
            munmap(mem_copy, mem_size);
            struct yos_proc *child = yos_proc_find(ctx->rt, ctx->fork_return);
            if (child) child->state = YOS_PROC_FREE;
            return;
        }
        if (snapshot_wasm_memory(mem_copy, mem, mem_size,
                                  fork_thread_arg->live_regions,
                                  YOS_FORK_MAX_REGIONS,
                                  &fork_thread_arg->live_region_count) < 0) {
            /* /proc/self/maps unavailable OR region table overflowed —
             * either way the child can't trust the region list, so
             * mark it empty and fall back to a full-size memcpy in
             * fork_thread_func. The snapshot itself is still valid:
             * snapshot_wasm_memory wrote the live bytes via memcpy
             * before signalling the overflow. */
            ydebug("fork pump: region overflow OR /proc/self/maps "
                   "unavailable — child will full-memcpy\n");
            fork_thread_arg->live_region_count = 0;
        }

        /* Save WASM globals */
        uint32_t num_globals = mod->numGlobals;
        int64_t *wasm_globals_copy = malloc(num_globals * sizeof(int64_t));
        if (!wasm_globals_copy) {
            munmap(mem_copy, mem_size);
            free(fork_thread_arg);
            struct yos_proc *child = yos_proc_find(ctx->rt, ctx->fork_return);
            if (child) child->state = YOS_PROC_FREE;
            return;
        }
        for (uint32_t i = 0; i < num_globals; i++) {
            wasm_globals_copy[i] = mod->globals[i].intValue;
        }

        /* Find child process */
        struct yos_proc *child_proc = yos_proc_find(ctx->rt, ctx->fork_return);
        if (!child_proc) {
            munmap(mem_copy, mem_size);
            free(fork_thread_arg);
            free(wasm_globals_copy);
            return;
        }

        fork_thread_arg->rt = ctx->rt;
        fork_thread_arg->proc = child_proc;
        fork_thread_arg->memory_snapshot = mem_copy;
        fork_thread_arg->memory_size = mem_size;
        fork_thread_arg->asyncify_ptr    = ctx->asyncify_ptr;
        /* TODO(setjmp-refactor): forward ctx->sj_slots[] to the child. */
        fork_thread_arg->sj_discard_ptr  = ctx->sj_discard_ptr;
        fork_thread_arg->wasm_globals = wasm_globals_copy;
        fork_thread_arg->wasm_globals_count = num_globals;
        fork_thread_arg->heap_end = ctx->heap_end;
        fork_thread_arg->alloc_lo        = ctx->alloc_lo;
        fork_thread_arg->alloc_hi        = ctx->alloc_hi;
        fork_thread_arg->mmap_top        = ctx->mmap_top;
        fork_thread_arg->free_count      = ctx->free_count;
        memcpy(fork_thread_arg->free_list, ctx->free_list,
               sizeof(fork_thread_arg->free_list));
        fork_thread_arg->alloc_free_head = ctx->alloc_free_head;
        fork_thread_arg->argc = ctx->argc;
        fork_thread_arg->argv = ctx->argv;
        fork_thread_arg->envc = ctx->envc;
        fork_thread_arg->envp = ctx->envp;
        /* Give the child its OWN copy of wasm_bytes. Sharing the
         * parent's buffer caused intermittent SIGSEGV in
         * m3_FindFunction (functions[i].import.moduleUtf8 deref) on
         * a child's later fork(): module->functions ended up NULL
         * because wasm3's parse/load kept pointers into the shared
         * wasm bytecode buffer and concurrent sibling forks raced on
         * its allocator path. Private buffers per fork isolate
         * wasm3's module state. */
        fork_thread_arg->wasm_bytes_size = ctx->wasm_bytes_size;
        fork_thread_arg->wasm_bytes = malloc(ctx->wasm_bytes_size);
        if (!fork_thread_arg->wasm_bytes) {
            munmap(mem_copy, mem_size);
            free(wasm_globals_copy);
            free(fork_thread_arg);
            struct yos_proc *child = yos_proc_find(ctx->rt, ctx->fork_return);
            if (child) child->state = YOS_PROC_FREE;
            return;
        }
        memcpy(fork_thread_arg->wasm_bytes, ctx->wasm_bytes,
               ctx->wasm_bytes_size);
        /* F_DUPFD each parent host fd RIGHT NOW, in the parent
         * thread, before the child thread runs and before the
         * parent's wasm code resumes (asyncify_start_rewind, below).
         *
         * Why: the snapshot used to store host fd NUMBERS and let
         * the child thread call F_DUPFD on each one later. Those
         * numbers are unstable references — between snapshot and the
         * child thread's dup loop, the parent's rewound wasm code
         * runs (e.g. zsh's post-fork close(forkpipe[1]); read; …)
         * and closes/reuses host fd numbers. The child's later
         * F_DUPFD then duplicates whatever the SLOT now points to,
         * not what the parent originally referenced — so e.g. the
         * pipe write end becomes a dup of host stdout, and zsh's
         * 8-byte fork-pipe errno write surfaces on the user's
         * terminal as "\xff\xff\xff\xff\xff\xff\xff\xff" + truncated
         * "ad file descriptor" (see test_ssh_no_args_help.py).
         *
         * Doing the dup synchronously here pins each kernel object
         * before the parent can race ahead. POSIX fork preserves
         * FD_CLOEXEC; F_DUPFD strips it, so re-set via
         * F_DUPFD_CLOEXEC when the source had it. */
        for (int i = 0; i < YOS_FD_MAX; i++) {
            int phfd = ctx->fd_map[i];
            if (phfd < 0) {
                fork_thread_arg->parent_fd_map[i] = -1;
                continue;
            }
            int flags = fcntl(phfd, F_GETFD);
            int dupcmd = (flags >= 0 && (flags & FD_CLOEXEC))
                           ? F_DUPFD_CLOEXEC : F_DUPFD;
            int chfd = fcntl(phfd, dupcmd, 0);
            fork_thread_arg->parent_fd_map[i] = (chfd >= 0) ? chfd : -1;
        }
        memcpy(fork_thread_arg->parent_cwd, ctx->cwd,
               sizeof(fork_thread_arg->parent_cwd));
        memcpy(fork_thread_arg->parent_sig_handlers, ctx->sig_handlers,
               sizeof(fork_thread_arg->parent_sig_handlers));
        fork_thread_arg->parent_sig_ignore_mask = ctx->sig_ignore_mask;
        fork_thread_arg->parent_sig_mask    = ctx->sig_mask;
        fork_thread_arg->parent_sig_pending = ctx->sig_pending;
        memcpy(fork_thread_arg->parent_locale_name, ctx->locale_name,
               sizeof(fork_thread_arg->parent_locale_name));
        /* Dup each live parent FILE* so the child gets an
         * independent host FILE* on each inherited slot. Without
         * this, child fclose closes the underlying host file and
         * subsequent parent fputs/fwrite/fclose ENOENT/EBADF/
         * crashes on the freed FILE*. */
        for (int i = 0; i < 256; i++) {
            FILE *parent_fp = (FILE *)ctx->file_slots[i];
            fork_thread_arg->parent_file_dup_fps[i]  = NULL;
            fork_thread_arg->parent_file_dup_wfds[i] = -1;
            fork_thread_arg->parent_file_modes[i][0] = '\0';
            if (!parent_fp) continue;
            int phfd = fileno(parent_fp);
            if (phfd < 0) continue;
            int chfd = fcntl(phfd, F_DUPFD_CLOEXEC, 0);
            if (chfd < 0) chfd = fcntl(phfd, F_DUPFD, 0);
            if (chfd < 0) continue;
            const char *mode = ctx->file_modes[i][0]
                               ? ctx->file_modes[i] : "r+";
            FILE *cfp = fdopen(chfd, mode);
            if (!cfp) { close(chfd); continue; }
            fork_thread_arg->parent_file_dup_fps[i]  = cfp;
            fork_thread_arg->parent_file_dup_wfds[i] = ctx->file_wfds[i];
            size_t n = strlen(mode);
            if (n >= sizeof(fork_thread_arg->parent_file_modes[i]))
                n = sizeof(fork_thread_arg->parent_file_modes[i]) - 1;
            memcpy(fork_thread_arg->parent_file_modes[i], mode, n);
            fork_thread_arg->parent_file_modes[i][n] = '\0';
        }
        /* env_store: copy the parent's index table so the child sees
         * the same env entries at fork time. The string bodies live
         * in wasm linear memory and ride along with the memory
         * snapshot, so the offsets remain valid. */
        memcpy(&fork_thread_arg->parent_env_store, &ctx->env_store,
               sizeof(fork_thread_arg->parent_env_store));
        fork_thread_arg->parent_umask = ctx->umask;
        fork_thread_arg->forkpty_pending    = ctx->forkpty_pending;
        fork_thread_arg->forkpty_master_wfd = ctx->forkpty_master_wfd;
        fork_thread_arg->forkpty_slave_wfd  = ctx->forkpty_slave_wfd;

        /* Spawn child thread detached so the parent resumes concurrently.
         * Child lifetime is tracked via yos_proc state (RUNNING/ZOMBIE);
         * waitpid reaps via the process table, not via pthread_join.
         * NOTE: this currently LEAKS the child's m3 runtime + linear
         * memory (~256 MiB VSZ / fork) — we can't safely m3_FreeRuntime
         * from the dying child's yos_exit (wasm3 interpreter is on the
         * stack, Runtime_Release asserts numActiveCodePages==0) AND
         * deferring it to the parent's waitpid races with the child's
         * pthread_exit tail (TLS dtors etc. corrupt the heap). Until a
         * dedicated reaper thread with a wait-for-fully-terminated
         * primitive exists, accept the leak. */
        child_proc->state = YOS_PROC_RUNNING;
        pthread_t t;
        int r = pthread_create(&t, NULL, fork_thread_func, fork_thread_arg);
        if (r != 0) {
            release_parent_dups(fork_thread_arg);
            munmap(mem_copy, fork_thread_arg->memory_size);
            free(wasm_globals_copy);
            free(fork_thread_arg);
            child_proc->state = YOS_PROC_FREE;
            return;
        }
        pthread_detach(t);

        ydebug("forked child pid=%d (parent thread tid=%d), resuming parent\n", child_proc->pid, (int)(uintptr_t)pthread_self());

        /* Resume parent - start rewind */
        call_asyncify(wrt, "asyncify_start_rewind", ctx->asyncify_ptr);

        /* Call _start again - this will rewind to fork point */
        IM3Function start;
        m3_FindFunction(&start, wrt, "_start");
        m3_CallV(start);
    }
}

/* Wait options from linux/wait.h */
#define WNOHANG    0x00000001

/* Find a zombie child to reap. Returns NULL if none found.
 * pid: -1 = any child, >0 = specific pid */
static struct yos_proc *find_zombie_child(struct yos_runtime *rt, int32_t ppid, int32_t pid)
{
    for (int i = 0; i < YOS_MAX_PROCS; i++) {
        struct yos_proc *p = &rt->procs[i];
        if (p->state == YOS_PROC_ZOMBIE && p->ppid == ppid) {
            if (pid == -1 || p->pid == pid)
                return p;
        }
    }
    return NULL;
}

/* Check if there are any children (zombie or running) */
static int has_children(struct yos_runtime *rt, int32_t ppid, int32_t pid)
{
    for (int i = 0; i < YOS_MAX_PROCS; i++) {
        struct yos_proc *p = &rt->procs[i];
        if (p->state != YOS_PROC_FREE && p->ppid == ppid) {
            if (pid == -1 || p->pid == pid)
                return 1;
        }
    }
    return 0;
}

int32_t yos_waitpid(struct yos_exec_ctx *ctx, int32_t pid, uint32_t stat_addr, int32_t options)
{
    ydebug("waitpid(%d, 0x%x, %d)\n", pid, stat_addr, options);

    int32_t my_pid = ctx->proc->pid;
    struct yos_runtime *rt = ctx->rt;

    pthread_mutex_lock(&rt->proc_lock);

    /* Check if child exists */
    if (!has_children(rt, my_pid, pid)) {
        pthread_mutex_unlock(&rt->proc_lock);
        /* POSIX waitpid returns -1 + errno=ECHILD, NOT -ECHILD as the
         * return value. zsh's `wait_for_processes` polls with WNOHANG
         * and only stops the reap loop on rc==0 or rc==-1+ECHILD;
         * returning -10 directly looks like "reaped pid 4294967286" to
         * the guest and trips the `wait failed: %e` warning. */
        extern int yos_remap_errno_h2g(int);
        if (ctx->memory && ctx->errno_off)
            *(int *)(ctx->memory + ctx->errno_off) = yos_remap_errno_h2g(ECHILD);
        return -1;
    }

    /* Find zombie child */
    struct yos_proc *child = find_zombie_child(rt, my_pid, pid);

    if (!child && (options & WNOHANG)) {
        pthread_mutex_unlock(&rt->proc_lock);
        return 0;  /* No zombie, non-blocking */
    }

    /* Wait for child to become zombie */
    while (!child) {
        pthread_mutex_unlock(&rt->proc_lock);
        /* TODO: proper blocking wait on child's wait_cond */
        usleep(1000);
        pthread_mutex_lock(&rt->proc_lock);
        child = find_zombie_child(rt, my_pid, pid);
    }

    /* Reap the zombie */
    int32_t child_pid = child->pid;
    int32_t exit_code = child->exit_code;
    int32_t term_sig  = child->term_sig;
    child->state = YOS_PROC_FREE;

    pthread_mutex_unlock(&rt->proc_lock);

    /* POSIX-shape status word:
     *   - if WIFSIGNALED: low 7 bits = signum, exit-status byte = 0
     *   - if WIFEXITED  : low 7 bits = 0,      exit-status byte = code
     * yos's deliver_to_proc / signal_pump record term_sig when a
     * SIG_DFL-terminate signal kills the proc; otherwise the proc
     * exited cleanly via _exit(code). */
    if (stat_addr && stat_addr < ctx->memory_size - 4) {
        int32_t status = term_sig ? (term_sig & 0x7f)
                                  : ((exit_code & 0xff) << 8);
        *(int32_t *)(ctx->memory + stat_addr) = status;
    }

    ydebug("waitpid = %d (exit_code=%d term_sig=%d)\n",
           child_pid, exit_code, term_sig);
    return child_pid;
}

/* Check WASM magic bytes */
static int check_wasm_magic(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t magic[4];
    int ok = (fread(magic, 1, 4, f) == 4 &&
              magic[0] == 0x00 && magic[1] == 0x61 &&
              magic[2] == 0x73 && magic[3] == 0x6d);
    fclose(f);
    return ok;
}

/* Parse shebang line. Returns 0 on success, -1 if not a shebang.
 * interp and arg are output buffers (PATH_MAX size). */
static int parse_shebang(const char *path, char *interp, char *arg)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    /* Must start with #! */
    if (line[0] != '#' || line[1] != '!') return -1;

    /* Skip #! and leading whitespace */
    char *p = line + 2;
    while (*p == ' ' || *p == '\t') p++;

    /* Extract interpreter path */
    char *start = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;

    size_t len = p - start;
    if (len == 0 || len >= PATH_MAX) return -1;
    memcpy(interp, start, len);
    interp[len] = '\0';

    /* Skip whitespace before optional arg */
    while (*p == ' ' || *p == '\t') p++;

    /* Extract optional argument (single arg only, like kernel) */
    arg[0] = '\0';
    if (*p && *p != '\n' && *p != '\r') {
        start = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        /* Trim trailing whitespace */
        while (p > start && (p[-1] == ' ' || p[-1] == '\t')) p--;
        len = p - start;
        if (len > 0 && len < PATH_MAX) {
            memcpy(arg, start, len);
            arg[len] = '\0';
        }
    }

    return 0;
}

/*
 * Host executable execution from inside a yos guest is gated by
 * YOS_ALLOW_HOST_EXEC. Default = undefined = host execs blocked: only wasm
 * binaries (or scripts with a wasm interpreter via shebang) load. When
 * defined at compile time (e.g. -DYOS_ALLOW_HOST_EXEC=1), execve falls
 * through to a real host execv() that replaces this yos process with the
 * target binary — matching POSIX execve semantics, but breaking the wasm
 * sandbox. Off by default; only flip on for a yos build that explicitly
 * acts as a wasm-↔-native bridge.
 */
static int32_t host_execve_fallback(const char *fn, struct yos_exec_ctx *ctx,
                                    uint32_t argv_ptr)
{
#ifdef YOS_ALLOW_HOST_EXEC
    /* Validate the argv pointer-array and every guest string it points
     * to before strdup'ing into host argv. argv_ptr==0 is legal (zero-
     * arg exec); empty terminator at offset 0 still needs in-range
     * check on the first slot. Without this a hostile guest can hand
     * us an OOB argv or an unterminated string and we walk past wasm
     * memory before execv. */
    if (argv_ptr != 0 &&
        (uint64_t)argv_ptr + 4ULL > (uint64_t)ctx->memory_size)
        return -EFAULT;
    uint32_t *wasm_argv = argv_ptr ? (uint32_t *)(ctx->memory + argv_ptr) : NULL;
    int argc = 0;
    if (wasm_argv) {
        while (argc < 1024) {
            uint64_t slot_end = (uint64_t)argv_ptr + (uint64_t)(argc + 1) * 4ULL;
            if (slot_end > (uint64_t)ctx->memory_size) return -EFAULT;
            if (wasm_argv[argc] == 0) break;
            if (!wstr_check(ctx, wasm_argv[argc])) return -EFAULT;
            argc++;
        }
    }
    char **host_argv = malloc((argc + 1) * sizeof(char *));
    if (!host_argv) return -ENOMEM;
    for (int i = 0; i < argc; ++i)
        host_argv[i] = strdup((const char *)(ctx->memory + wasm_argv[i]));
    host_argv[argc] = NULL;

    ydebug("execve: YOS_ALLOW_HOST_EXEC: replacing yos with host %s\n", fn);
    execv(fn, host_argv);
    /* execv returned → it failed; report errno to the wasm caller. */
    int err = errno;
    for (int i = 0; i < argc; ++i) free(host_argv[i]);
    free(host_argv);
    return -err;
#else
    (void)fn; (void)ctx; (void)argv_ptr;
    return -ENOEXEC;
#endif
}

/* Forward decl: defined further down in this file. yos_execve's
 * shebang-interp PATH lookup calls into it. */
static int execvp_path_search(struct yos_exec_ctx *, const char *, char *);

int32_t yos_execve(struct yos_exec_ctx *ctx, uint32_t filename, uint32_t argv_ptr, uint32_t envp)
{
    extern const char *yos_path_resolve(struct yos_exec_ctx *, const char *);
    /* Validate the guest filename string is in-range AND has a NUL
     * terminator within memory_size. Without this a guest can pass a
     * bogus offset or an unterminated string and the host's strncpy /
     * path_resolve / access walks past wasm memory. */
    if (filename == 0 || filename >= ctx->memory_size)
        return yos_errno_neg(ctx, EFAULT);
    {
        const char *p = (const char *)(ctx->memory + filename);
        const char *end = (const char *)(ctx->memory + ctx->memory_size);
        const char *q;
        for (q = p; q < end; ++q) if (*q == 0) break;
        if (q == end) return yos_errno_neg(ctx, EFAULT);
    }
    const char *raw_fn = (const char *)(ctx->memory + filename);
    /* yos_path_resolve returns a TLS pointer — copy because we use
     * it across many subsequent calls (access, open, etc. below). */
    const char *res = yos_path_resolve(ctx, raw_fn);
    static _Thread_local char fn_buf[PATH_MAX];
    strncpy(fn_buf, res, sizeof fn_buf - 1);
    fn_buf[sizeof fn_buf - 1] = 0;
    const char *fn = fn_buf;
    ydebug("execve(%s, ...) [raw=%s]\n", fn, raw_fn);

    /* Check file exists and is readable. POSIX contract: execve returns
     * -1 with errno set; never returns 0 on failure. Use yos_errno_neg
     * to write the FreeBSD-remapped errno into the wasm-side slot AND
     * return -1, so the guest's `execve(...) == -1` test fires correctly
     * — without this, zsh's interactive command-not-found path treats a
     * negative-errno return ("-2") as a non-standard error code, exits 1
     * instead of 127, and never prints "command not found: foo". */
    if (access(fn, R_OK) != 0) {
        int saved = errno;
        ydebug("execve: %s: %s\n", fn, strerror(saved));
        /* Remember this attempt so yos_exit can synthesise a
         * "command not found"-style diagnostic if the guest's own
         * print path is wedged (asyncify-fork-child silence regression). */
        strncpy(ctx->last_failed_exec_path, fn,
                sizeof(ctx->last_failed_exec_path) - 1);
        ctx->last_failed_exec_path[sizeof(ctx->last_failed_exec_path) - 1] = 0;
        ctx->last_failed_exec_errno = saved;
        ctx->stderr_written_since_exec = 0;
        return yos_errno_neg(ctx, saved);
    }

    char exec_path[PATH_MAX];
    char shebang_interp[PATH_MAX];
    char shebang_arg[PATH_MAX];
    int is_script = 0;
    int extra_args = 0;  /* args to prepend from shebang */

    /* Check file format */
    if (check_wasm_magic(fn)) {
        /* Direct WASM execution */
        strcpy(exec_path, fn);
    } else if (parse_shebang(fn, shebang_interp, shebang_arg) == 0) {
        /* Script with shebang. The interpreter must end up being a
         * wasm binary; the shebang itself can point at a literal host
         * path OR a name resolvable via PATH. We try in order:
         *   1. The interpreter path verbatim (e.g. /libexec/zsh
         *      assuming the host fs has /libexec/zsh).
         *   2. If that's not wasm OR doesn't exist, walk the guest's
         *      $PATH using basename(interpreter). Lets scripts ship
         *      with `#!/libexec/zsh` even though the actual umbrella
         *      libexec dir lives at /nix/store/<hash>-yos-all/libexec/
         *      — yos.sh exports PATH=<umbrella-libexec> and the
         *      walked path finds zsh there. */
        ydebug("execve: shebang interp=%s arg=%s\n", shebang_interp, shebang_arg);

        const char *interp_use = shebang_interp;
        char resolved_interp[PATH_MAX];
        if (!check_wasm_magic(shebang_interp)) {
            const char *slash = strrchr(shebang_interp, '/');
            const char *base = slash ? slash + 1 : shebang_interp;
            if (execvp_path_search(ctx, base, resolved_interp) &&
                check_wasm_magic(resolved_interp)) {
                ydebug("execve: shebang interp %s → PATH resolved %s\n",
                       shebang_interp, resolved_interp);
                interp_use = resolved_interp;
            } else {
                ydebug("execve: interpreter %s is not WASM (PATH lookup also failed)\n",
                       shebang_interp);
                return host_execve_fallback(fn, ctx, argv_ptr);
            }
        }
        strcpy(exec_path, interp_use);
        strncpy(shebang_interp, interp_use, sizeof shebang_interp - 1);
        shebang_interp[sizeof shebang_interp - 1] = 0;
        is_script = 1;
        extra_args = shebang_arg[0] ? 2 : 1;  /* interp [arg] script */
    } else {
        /* Not wasm, no shebang — typically a HOST binary path leaked in
         * from the inherited environment: nvim's :terminal execs
         * $SHELL=/bin/zsh, which passed the os_can_exe() pre-check
         * against the REAL host fs, and then lands here as an ELF. The
         * browser engine resolves exec targets by BASENAME in its tool
         * map; mirror that — walk the guest $PATH for the basename and
         * exec the wasm sibling (…/libexec/zsh) when there is one.
         * argv[0] is left untouched, so a shell exec'd as "/bin/sh"
         * still sees basename sh and enters sh-emulation. Only when the
         * basename resolves to nothing wasm do we fall through to the
         * host-exec fallback (ENOEXEC unless YOS_ALLOW_HOST_EXEC). */
        const char *slash = strrchr(fn, '/');
        char resolved_alias[PATH_MAX];
        if (slash && slash[1] &&
            execvp_path_search(ctx, slash + 1, resolved_alias) &&
            check_wasm_magic(resolved_alias)) {
            ydebug("execve: host-format %s → PATH-resolved wasm %s\n",
                   fn, resolved_alias);
            strcpy(exec_path, resolved_alias);
        } else {
            ydebug("execve: %s: unknown format\n", fn);
            return host_execve_fallback(fn, ctx, argv_ptr);
        }
    }

    /* Count original argv entries.
     *
     * Each iteration reads `wasm_argv[orig_argc]` — a 4-byte slot at
     * argv_ptr + 4*orig_argc. Validate each slot's range before the
     * read so a bogus argv_ptr or huge argc can't make us scan past
     * wasm memory. Likewise validate each string offset and confirm
     * it points at a NUL-terminated string in-range.
     *
     * If argv_ptr is 0 the guest passed NULL — POSIX says "undefined
     * behaviour" but every real libc treats this as EFAULT. */
    if (argv_ptr == 0) return yos_errno_neg(ctx, EFAULT);
    int orig_argc = 0;
    for (;;) {
        uint64_t slot_off = (uint64_t)argv_ptr + (uint64_t)orig_argc * 4ULL;
        if (slot_off + 4 > (uint64_t)ctx->memory_size)
            return yos_errno_neg(ctx, EFAULT);
        uint32_t entry = *(uint32_t *)(ctx->memory + (uint32_t)slot_off);
        if (entry == 0) break;
        if (orig_argc > 1024) return yos_errno_neg(ctx, E2BIG);
        /* Validate the string this slot points at. */
        if (entry >= ctx->memory_size) return yos_errno_neg(ctx, EFAULT);
        {
            const char *p = (const char *)(ctx->memory + entry);
            const char *end = (const char *)(ctx->memory + ctx->memory_size);
            const char *q; for (q = p; q < end; ++q) if (*q == 0) break;
            if (q == end) return yos_errno_neg(ctx, EFAULT);
        }
        orig_argc++;
    }
    uint32_t *wasm_argv = (uint32_t *)(ctx->memory + argv_ptr);

    /* For scripts: argv[0] is replaced, script path inserted after interp */
    int new_argc = is_script ? (extra_args + orig_argc) : orig_argc;

    /* Allocate host argv */
    char **host_argv = malloc((new_argc + 1) * sizeof(char *));
    if (!host_argv) return yos_errno_neg(ctx, ENOMEM);

    int ai = 0;

    if (is_script) {
        /* argv[0] = interpreter basename or original argv[0] behavior */
        host_argv[ai++] = strdup(shebang_interp);
        if (shebang_arg[0]) {
            host_argv[ai++] = strdup(shebang_arg);
        }
        /* Insert script path */
        host_argv[ai++] = strdup(fn);
        /* Copy remaining original args (skip argv[0]) — already validated above. */
        for (int i = 1; i < orig_argc; i++) {
            const char *src = (const char *)(ctx->memory + wasm_argv[i]);
            host_argv[ai++] = strdup(src);
        }
    } else {
        for (int i = 0; i < orig_argc; i++) {
            const char *src = (const char *)(ctx->memory + wasm_argv[i]);
            host_argv[ai++] = strdup(src);
        }
    }
    host_argv[ai] = NULL;

    /* Debug */
    for (int i = 0; i < ai; i++) {
        ydebug("execve: argv[%d] = %s\n", i, host_argv[i]);
    }

    /* Capture envp from wasm-side caller. NULL/0 means "no env override" —
     * fall back to ctx->envp at exec-load time. POSIX execve takes a
     * full env replacement (it does NOT merge), so we copy whatever
     * the caller passed verbatim. */
    char **host_envp = NULL;
    int env_count = 0;
    if (envp != 0) {
        /* Same range-walk pattern as argv: validate each slot AND each
         * env-string before the read. */
        if (envp >= ctx->memory_size) {
            for (int i = 0; i < ai; i++) free(host_argv[i]);
            free(host_argv);
            return yos_errno_neg(ctx, EFAULT);
        }
        for (;;) {
            uint64_t slot_off = (uint64_t)envp + (uint64_t)env_count * 4ULL;
            if (slot_off + 4 > (uint64_t)ctx->memory_size) {
                for (int i = 0; i < ai; i++) free(host_argv[i]);
                free(host_argv);
                return yos_errno_neg(ctx, EFAULT);
            }
            uint32_t entry = *(uint32_t *)(ctx->memory + (uint32_t)slot_off);
            if (entry == 0) break;
            if (env_count > 4096) {
                for (int i = 0; i < ai; i++) free(host_argv[i]);
                free(host_argv);
                return yos_errno_neg(ctx, E2BIG);
            }
            if (entry >= ctx->memory_size) {
                for (int i = 0; i < ai; i++) free(host_argv[i]);
                free(host_argv);
                return yos_errno_neg(ctx, EFAULT);
            }
            {
                const char *p = (const char *)(ctx->memory + entry);
                const char *end = (const char *)(ctx->memory + ctx->memory_size);
                const char *q; for (q = p; q < end; ++q) if (*q == 0) break;
                if (q == end) {
                    for (int i = 0; i < ai; i++) free(host_argv[i]);
                    free(host_argv);
                    return yos_errno_neg(ctx, EFAULT);
                }
            }
            env_count++;
        }
        uint32_t *wasm_envp = (uint32_t *)(ctx->memory + envp);
        host_envp = malloc((env_count + 1) * sizeof(char *));
        if (!host_envp) {
            for (int i = 0; i < ai; i++) free(host_argv[i]);
            free(host_argv);
            return yos_errno_neg(ctx, ENOMEM);
        }
        for (int i = 0; i < env_count; i++) {
            const char *src = (const char *)(ctx->memory + wasm_envp[i]);
            host_envp[i] = strdup(src);
        }
        host_envp[env_count] = NULL;
    }

    /* POSIX execve: signal mask is preserved; pending signals are
     * cleared; handlers reset to SIG_DFL unless they were SIG_IGN
     * (those carry across). The wasm function-table indices recorded
     * in sig_handlers are no longer valid against the new module's
     * table — clear every custom handler index. SIG_IGN now lives
     * in its own bitmask (ctx->sig_ignore_mask) so we just leave that
     * bitmask alone; the cleared sig_handlers + retained ignore_mask
     * together preserve the POSIX-required SIG_IGN-carry behaviour. */
    for (int i = 0; i < 32; i++) ctx->sig_handlers[i] = 0u;
    ctx->sig_pending = 0;
    /* ctx->sig_mask intentionally preserved. */

    /* Store exec info */
    strcpy(ctx->exec_path, exec_path);
    ctx->exec_pending = 1;
    ctx->exec_argc = ai;
    ctx->exec_argv = host_argv;
    ctx->exec_envc = env_count;
    ctx->exec_envp = host_envp;

    ydebug("execve: exec_pending set, path=%s argc=%d\n", exec_path, ai);

    /* POSIX exec closes every fd whose FD_CLOEXEC flag is set. Real
     * host-execve does this implicitly; yos's pseudo-exec leaves the
     * shared host fd table intact, so we have to walk fd_map and close
     * the marked entries ourselves. Without this, libuv's spawn signal
     * pipe (O_CLOEXEC) stays open after exec, the post-execvp failure
     * path in the child writes errno into it, and the parent reads
     * that errno and treats the spawn as failed. */
    for (int i = 0; i < YOS_FD_MAX; i++) {
        int hfd = ctx->fd_map[i];
        if (hfd < 0) continue;
        int flags = fcntl(hfd, F_GETFD);
        if (flags < 0) continue;
        if (flags & FD_CLOEXEC) {
            close(hfd);
            ctx->fd_map[i] = -1;
        }
    }

    /* Return 0 (success) - syscall handler will trap to stop WASM execution */
    return 0;
}

/* execv(path, argv) — POSIX wrapper around execve with the caller's
 * existing env. We pass envp=0 to yos_execve which means "no env
 * replacement" (the reloaded module inherits ctx->envp). */
int32_t yos_execv(struct yos_exec_ctx *ctx, uint32_t path, uint32_t argv_ptr)
{
    return yos_execve(ctx, path, argv_ptr, 0);
}

/* execvp(file, argv) — searches PATH if `file` has no slash. We don't
 * implement PATH search yet; if the caller passes an absolute or
 * relative path with a slash (which is what libuv / nvim does for the
 * self-respawn path), it works. Bare file names return -ENOENT. */
/* PATH-search helper for execvp / execvpe. POSIX says:
 *   - If `file` contains a '/', execve(file) directly.
 *   - Else walk $PATH (colon-separated); for each prefix `p` try
 *     execve("p/file"). Empty prefix means cwd.
 *   - If $PATH is unset, use a system default (we use "" → cwd).
 *
 * Writes the resolved path into `out` (caller-owned, must be
 * PATH_MAX). Returns 1 on hit (out filled), 0 if every candidate
 * stat'd to ENOENT (caller surfaces ENOENT), -1 on error.
 *
 * We resolve via host stat (against the GUEST's $PATH from
 * ctx->envp). The path we hand to yos_execve is absolute, so
 * even though we don't have per-process cwd today, the exec
 * arg works regardless of host cwd state. */
static int execvp_path_search(struct yos_exec_ctx *ctx,
                              const char *file, char *out)
{
    /* Pull PATH from the guest env. ctx->envp is the parent's env
     * array as host char**; each entry is a host string the guest
     * set via setenv. Easier than the wasm-side env walk. */
    const char *path = NULL;
    if (ctx->envp) {
        for (int i = 0; ctx->envp[i]; i++) {
            if (strncmp(ctx->envp[i], "PATH=", 5) == 0) {
                path = ctx->envp[i] + 5;
                break;
            }
        }
    }
    if (!path || !*path) path = "";  /* empty PATH → cwd only */

    const char *p = path;
    while (*p || p == path) {
        const char *colon = strchr(p, ':');
        size_t plen = colon ? (size_t)(colon - p) : strlen(p);
        size_t flen = strlen(file);
        if (plen + 1 + flen + 1 > 4096) {
            if (!colon) break;
            p = colon + 1;
            continue;
        }
        if (plen == 0) {
            /* empty component → cwd */
            memcpy(out, file, flen + 1);
        } else {
            memcpy(out, p, plen);
            out[plen] = '/';
            memcpy(out + plen + 1, file, flen + 1);
        }
        struct stat st;
        if (stat(out, &st) == 0 && (st.st_mode & S_IFREG)) {
            ydebug("execvp_path: %s → %s\n", file, out);
            return 1;
        }
        if (!colon) break;
        p = colon + 1;
    }
    return 0;
}

int32_t yos_execvp(struct yos_exec_ctx *ctx, uint32_t file, uint32_t argv_ptr)
{
    /* Validate file is an in-range, NUL-terminated guest string before
     * strchr/execvp_path_search walk it. argv pointer-array + each
     * string it points to are validated downstream by yos_execve. */
    const char *fn = wstr_check(ctx, file);
    if (!fn) return -EFAULT;
    if (strchr(fn, '/')) {
        return yos_execve(ctx, file, argv_ptr, 0);
    }
    char resolved[4096];
    if (!execvp_path_search(ctx, fn, resolved)) {
        ydebug("execvp(%s): no match in PATH\n", fn);
        return -ENOENT;
    }
    /* Stash the resolved absolute path in the guest's wasm memory
     * so yos_execve sees it via the same (wasm-offset → host-string)
     * convention as everything else. Pin it to the parent's stack-
     * top scratch — we're about to fork+exec anyway so the parent's
     * memory state is leaving. ctx->heap_end is a stable cursor; the
     * 4 KiB headroom is plenty for a PATH-resolved entry. */
    uint32_t off = ctx->heap_end + 1024;
    if ((uint64_t)off + 4096ULL > (uint64_t)ctx->memory_size) return -ENOMEM;
    size_t n = strlen(resolved);
    memcpy(ctx->memory + off, resolved, n + 1);
    return yos_execve(ctx, off, argv_ptr, 0);
}

/* execvpe(file, argv, envp) — same as execvp but with explicit envp. */
int32_t yos_execvpe(struct yos_exec_ctx *ctx, uint32_t file,
                    uint32_t argv_ptr, uint32_t envp)
{
    const char *fn = wstr_check(ctx, file);
    if (!fn) return -EFAULT;
    if (strchr(fn, '/')) {
        return yos_execve(ctx, file, argv_ptr, envp);
    }
    char resolved[4096];
    if (!execvp_path_search(ctx, fn, resolved)) {
        ydebug("execvpe(%s): no match in PATH\n", fn);
        return -ENOENT;
    }
    uint32_t off = ctx->heap_end + 1024;
    if ((uint64_t)off + 4096ULL > (uint64_t)ctx->memory_size) return -ENOMEM;
    size_t n = strlen(resolved);
    memcpy(ctx->memory + off, resolved, n + 1);
    return yos_execve(ctx, off, argv_ptr, envp);
}

/* deliver_to_proc: send `sig` to one guest proc via pthread_kill on its
 * thread, returning kill(2)-style status (0 on success or -errno).
 * sig==0 is the existence probe — no signal sent. */
static int deliver_to_proc(struct yos_proc *p, int sig)
{
    if (p->state == YOS_PROC_FREE || p->state == YOS_PROC_ZOMBIE)
        return -ESRCH;
    if (sig == 0) return 0;
    if (!p->thread) return -ESRCH;
    /* Some signals are *process-wide* on Linux/FreeBSD — pthread_kill
     * for SIGSTOP/SIGCONT/SIGTSTP/SIGTTIN/SIGTTOU stops or resumes the
     * entire host process (including yos and the parent guest), not
     * just the targeted child thread. zsh sends these for job control
     * (SIGSTOP/SIGCONT around `$(...)` evaluation, etc.) and the
     * intent is purely guest-internal — drop them. The guest's exec
     * model already provides start/stop semantics via pthread
     * lifetime; we don't want host-process suspension. SIGTERM/SIGKILL
     * pass through (real teardown). */
    switch (sig) {
        case 17: /* SIGSTOP   on FreeBSD — same as Linux */
        case 18: /* SIGTSTP   */
        case 19: /* SIGCONT (FreeBSD) — Linux's SIGSTOP is 19, ambiguous;
                  *           filtering both numbers is safest. */
        case 21: /* SIGTTIN  */
        case 22: /* SIGTTOU  */
            return 0;
        /* SIGCHLD (FreeBSD 20) is NOT filtered: delivery goes through
         * the per-ctx sig_pending channel below (never a raw host
         * pthread_kill of 20, which on Linux would be SIGTSTP), the
         * pump ignores it when no handler is installed (not in
         * default_action_is_terminate), and a registered handler is
         * exactly what the sender wants to run — nvim's libuv SIGCHLD
         * watcher reaps its forkpty :terminal child through it. The
         * sigsuspend-synthesised SIGCHLD path is unaffected. */
    }
    /* SIGKILL: route through the same per-ctx pending-signal channel
     * as every other signal. yos_signal_pump in the target's host
     * thread sees SIGKILL pending, marks the proc as ZOMBIE with
     * term_sig=9, and pthread_exit()s cleanly.
     *
     * History: used to call pthread_cancel(p->thread) here, but
     * pthread_cancel terminates the host thread at the NEXT
     * cancellation point — which is typically inside libc/wasm3
     * internals holding partially-modified state. The cancelled
     * thread's wasm3 runtime got leaked, and worse, follow-up
     * fork()s in other ctxs sometimes crashed in m3Error with a
     * garbage IM3Runtime pointer (last_bridge=fork in the crash
     * dump). Cooperative pthread_exit via signal_pump avoids the
     * pthread_cancel hazard entirely.
     *
     * Side-channel pthread_kill below wakes any blocked syscall
     * (read/usleep/etc.) with EINTR so the target's next yos_*
     * bridge call enters signal_pump and notices the SIGKILL bit. */
    if (sig == 9 /* SIGKILL — same number on Linux & FreeBSD */) {
        if (p->ctx_handle) {
            struct yos_exec_ctx *target = (struct yos_exec_ctx *)p->ctx_handle;
            __atomic_or_fetch(&target->sig_pending, 1u << 9, __ATOMIC_RELEASE);
        }
        /* Best-effort wake-up. If the thread hasn't been recorded
         * yet (race: pthread_create just returned), skip — the
         * pending bit is already set and the target will see it on
         * its first bridge entry. */
        if (p->thread) (void)pthread_kill(p->thread, SIGUSR2);
        return 0;
    }
    /* Set the target's per-process pending bit atomically. This is the
     * authoritative yos-side signal delivery: the target's
     * yos_signal_pump will fire the registered handler at the next
     * yield point (provided the signal isn't blocked in sig_mask).
     *
     * The pthread_kill below is ONLY a side-channel wake-up so a
     * target blocked in read()/usleep()/nanosleep() returns EINTR and
     * runs its pump. We use SIGUSR2 (not the original sig number) for
     * the wake-up because:
     *   1. If we sent the actual signum, host_signal_dispatcher would
     *      run in the target's thread and set the GLOBAL
     *      g_host_pending_signals bit. The first guest to call
     *      signal_pump after that (often the kill CALLER, since it
     *      writes prompt output right after kill returns) would steal
     *      that global bit and process the signal AS IF IT WERE FOR
     *      ITSELF — `kill <bg_job>` terminated the shell instead of
     *      the job. Issue: telnet-mode kill %1 hung the session.
     *   2. SIGUSR2 has a registered no-op handler (host_sigusr2_wake
     *      in main.c) whose entire purpose is to interrupt the
     *      target's blocking syscall without contaminating the
     *      global pending bitmask.
     * Sig 0 (existence probe) was handled above; we don't reach here. */
    if (p->ctx_handle && sig > 0 && sig < 32) {
        struct yos_exec_ctx *target = (struct yos_exec_ctx *)p->ctx_handle;
        __atomic_or_fetch(&target->sig_pending, 1u << sig, __ATOMIC_RELEASE);
    }
    int rc = pthread_kill(p->thread, SIGUSR2);
    return rc == 0 ? 0 : -rc;
}

int32_t yos_kill(struct yos_exec_ctx *ctx, int32_t pid, int32_t sig)
{
    ydebug("kill(%d, %d)\n", pid, sig);
    struct yos_runtime *rt = ctx->rt;
    int32_t my_pgid = ctx->proc ? ctx->proc->pgid : 0;
    extern int yos_remap_errno_h2g(int);

    /* POSIX kill returns -1 + errno on failure, NOT -<errno> as the
     * return value. zsh's wait loop is
     *   while (kill(pid, 0) >= 0 || errno != ESRCH) sigsuspend(...);
     * — returning -ESRCH (=-3) directly looks like rc<0, but errno
     * stays whatever it was before, so the loop never breaks. */
    int rc;

    /* pid > 0: deliver to that guest proc. */
    if (pid > 0) {
        struct yos_proc *t = yos_proc_find(rt, pid);
        if (!t) {
            if (ctx->memory && ctx->errno_off)
                *(int *)(ctx->memory + ctx->errno_off) = yos_remap_errno_h2g(ESRCH);
            return -1;
        }
        rc = deliver_to_proc(t, sig);
        if (rc < 0) {
            if (ctx->memory && ctx->errno_off)
                *(int *)(ctx->memory + ctx->errno_off) = yos_remap_errno_h2g(-rc);
            return -1;
        }
        /* POSIX self-kill synchronous delivery. If the calling thread
         * is the target, the signal must be delivered before kill()
         * returns (unless it's blocked). yos's default model defers
         * delivery to the next yield point inside the wasm runtime;
         * for self-kill that "next point" is whatever syscall the
         * guest happens to call next — which could be never if the
         * guest sits in pure compute or checks a flag set by the
         * handler. Pump here so kill(getpid(), sig) actually invokes
         * the handler before returning. */
        if (t == ctx->proc) {
            extern void yos_signal_pump(struct yos_exec_ctx *);
            yos_signal_pump(ctx);
        }
        return rc;
    }

    /* pid == 0: deliver to every proc in caller's pgrp. */
    /* pid == -1: deliver to every proc except init (and the caller). */
    /* pid <  -1: deliver to every proc whose pgid == -pid. */
    int32_t target_pgid = (pid == 0) ? my_pgid : (pid < -1 ? -pid : 0);
    int found = 0, last_err = 0;
    pthread_mutex_lock(&rt->proc_lock);
    for (int i = 0; i < YOS_MAX_PROCS; i++) {
        struct yos_proc *p = &rt->procs[i];
        if (p->state == YOS_PROC_FREE || p->state == YOS_PROC_ZOMBIE) continue;
        int match = 0;
        if (pid == 0)         match = (p->pgid == my_pgid);
        else if (pid == -1)   match = (p->pid != 1);  /* skip init */
        else                  match = (p->pgid == target_pgid);
        if (!match) continue;
        found++;
        int rc = (sig == 0) ? 0 : pthread_kill(p->thread, sig);
        if (rc != 0) last_err = -rc;
    }
    pthread_mutex_unlock(&rt->proc_lock);
    if (!found) {
        if (ctx->memory && ctx->errno_off)
            *(int *)(ctx->memory + ctx->errno_off) = yos_remap_errno_h2g(ESRCH);
        return -1;
    }
    if (last_err) {
        if (ctx->memory && ctx->errno_off)
            *(int *)(ctx->memory + ctx->errno_off) = yos_remap_errno_h2g(-last_err);
        return -1;
    }
    return 0;
}

/* yos_proc_kill_by_pid — deliver `sig` to guest `pid` without a calling
 * ctx. Used by the yctl daemon (a host-only pthread that has no
 * struct yos_exec_ctx of its own) to drive proc.kill from RPC. Returns
 * 0 on success, -errno on failure (ESRCH, etc.). */
int yos_proc_kill_by_pid(struct yos_runtime *rt, int32_t pid, int32_t sig)
{
    struct yos_proc *p = yos_proc_find(rt, pid);
    if (!p) return -ESRCH;
    return deliver_to_proc(p, sig);
}

/* wait(int *status) — equivalent to waitpid(-1, status, 0). POSIX
 * blocks until ANY child changes state. yos_waitpid handles the
 * blocking + status writeback already. Was stubbed -ENOSYS via
 * hooks.yaml; promoted to passthrough by removing the entry, so
 * codegen's m3w_wait wrapper looks up this symbol. */
int32_t yos_wait(struct yos_exec_ctx *ctx, uint32_t stat_addr)
{
    return yos_waitpid(ctx, -1, stat_addr, 0);
}

/* wait3(status, options, rusage) — equivalent to waitpid(-1, status,
 * options) with rusage data. zsh's `wait_for_processes` uses wait3
 * with WNOHANG to drain zombies after a SIGCHLD. The bridge previously
 * stubbed this to ENOSYS; that made zsh report `wait failed: <errno>`
 * after every fork+wait. We pass through to yos_waitpid and zero the
 * rusage struct (yos doesn't track per-proc CPU yet — bridge.py's
 * struct_convert pipeline could fill this in later). */
int32_t yos_wait3(struct yos_exec_ctx *ctx, uint32_t stat_addr, int32_t options, uint32_t ru)
{
    if (ru && ctx->memory && ru + 72 <= ctx->memory_size) {
        /* struct rusage is 72 bytes on FreeBSD i386 — zero it. */
        memset(ctx->memory + ru, 0, 72);
    }
    return yos_waitpid(ctx, -1, stat_addr, options);
}

int32_t yos_wait4(struct yos_exec_ctx *ctx, int32_t pid, uint32_t stat_addr, int32_t options, uint32_t ru)
{
    ydebug("wait4(%d, 0x%x, %d, 0x%x)\n", pid, stat_addr, options, ru);
    (void)ru; /* TODO: rusage conversion */

    int32_t my_pid = ctx->proc->pid;
    struct yos_runtime *rt = ctx->rt;

    pthread_mutex_lock(&rt->proc_lock);

    /* Check if child exists */
    if (!has_children(rt, my_pid, pid)) {
        pthread_mutex_unlock(&rt->proc_lock);
        ydebug("wait4 = -1 errno=ECHILD\n");
        /* See yos_waitpid for the rationale on -1+errno vs -ECHILD. */
        extern int yos_remap_errno_h2g(int);
        if (ctx->memory && ctx->errno_off)
            *(int *)(ctx->memory + ctx->errno_off) = yos_remap_errno_h2g(ECHILD);
        return -1;
    }

    /* Find zombie child */
    struct yos_proc *child = find_zombie_child(rt, my_pid, pid);

    if (!child && (options & WNOHANG)) {
        pthread_mutex_unlock(&rt->proc_lock);
        ydebug("wait4 = 0 (WNOHANG)\n");
        return 0;
    }

    /* Wait for child to become zombie */
    while (!child) {
        pthread_mutex_unlock(&rt->proc_lock);
        usleep(1000);
        pthread_mutex_lock(&rt->proc_lock);
        child = find_zombie_child(rt, my_pid, pid);
    }

    /* Reap the zombie */
    int32_t child_pid = child->pid;
    int32_t exit_code = child->exit_code;
    int32_t term_sig  = child->term_sig;
    child->state = YOS_PROC_FREE;

    pthread_mutex_unlock(&rt->proc_lock);

    /* POSIX-shape status word — see yos_waitpid for the same logic. */
    if (stat_addr && stat_addr < ctx->memory_size - 4) {
        int32_t status = term_sig ? (term_sig & 0x7f)
                                  : ((exit_code & 0xff) << 8);
        *(int32_t *)(ctx->memory + stat_addr) = status;
    }

    ydebug("wait4 = %d (exit_code=%d term_sig=%d)\n",
           child_pid, exit_code, term_sig);
    return child_pid;
}

/* clone() — the SINGLE process-creation primitive. Both fork and
 * pthread_create eventually route here. Branches on CLONE_VM:
 *   CLONE_VM set   → thread path (sibling wasm3 runtime, shared memory)
 *   CLONE_VM clear → fork path (asyncify snapshot + restore in fresh runtime)
 *
 * i386 SYS_clone arg order: (flags, child_stack, parent_tid, child_tid, tls).
 *
 * fn / arg are not separate syscall args on i386 — musl's __clone
 * pushes them onto the supplied stack before the syscall. We read them
 * back from wasm linear memory at child_stack-8 (fn) and child_stack-4
 * (arg), matching the standard x86 ABI musl already implements.
 *
 * The wasm32 musl __clone shim (see src/musl/arch/wasm32/thread/...)
 * is responsible for doing those pushes; without that, fn/arg arrive
 * as garbage and we must reject. */
int32_t yos_proc_clone(struct yos_exec_ctx *ctx,
                       uint32_t flags,
                       uint32_t child_stack,
                       uint32_t ptid_addr,
                       uint32_t ctid_addr,
                       uint32_t tls)
{
    /* CLONE_* constants come from <sched.h> via <pthread.h> in types.h. */
    ydebug("clone(flags=0x%x, stack=0x%x, ptid=0x%x, ctid=0x%x, tls=0x%x)\n",
           flags, child_stack, ptid_addr, ctid_addr, tls);

    /* Fork path: no shared memory. Delegate to the existing fork
     * machinery (asyncify-based copy). vfork is fork + parent-blocks
     * which we already have separately. */
    if (!(flags & CLONE_VM)) {
        /* Fall back to fork() for now. CLONE_PARENT/CLONE_PTRACE/etc.
         * are accepted-and-ignored here; the proc table fields land
         * the same way as a plain fork. */
        return yos_fork(ctx);
    }

    /* Thread path. CLONE_VM without CLONE_THREAD is unusual (would mean
     * "share memory but be a separate process"); we treat it as a
     * thread anyway since the substrate (sibling wasm3 runtime) doesn't
     * model that distinction. */
    if (!ctx->proc || !ctx->rt) return -EINVAL;

    /* Read fn and arg from where musl's wasm32 __clone shim wrote them:
     * the i386 ABI says they live just below child_stack (which is the
     * top-of-stack passed in). Bounds-check before touching memory. */
    if (child_stack < 8 || child_stack > ctx->memory_size)
        return -EFAULT;
    uint32_t *sp = (uint32_t *)(ctx->memory + child_stack - 8);
    uint32_t fn  = sp[0];
    uint32_t arg = sp[1];
    if (fn == 0) {
        /* Either the shim hasn't pushed yet or the caller is doing
         * raw clone() with an undocumented stack convention. */
        ydebug("clone: fn at 0x%x is 0 — wasm32 __clone shim not pushing fn/arg?\n",
               child_stack - 8);
        return -EINVAL;
    }

    /* Allocate the new yos_proc. Thread variant: tgid inherited from
     * caller's tgid so getpid() agrees across threads of one task,
     * pid is the unique tid. */
    struct yos_proc *child = yos_proc_alloc(ctx->rt, ctx->proc->pid);
    if (!child) return -EAGAIN;
    if (flags & CLONE_THREAD) {
        child->tgid = ctx->proc->tgid;
    }
    child->pgid = ctx->proc->pgid;
    child->sid  = ctx->proc->sid;
    if (flags & CLONE_CHILD_CLEARTID) child->tid_address = ctid_addr;

    /* Spawn through the same internal substrate the L1 import uses.
     * yos_clone_thread comes from impl/proc/pthread.h (included at
     * the top); no local extern decl needed. */
    uint32_t spawned_tid = 0;
    int rc = yos_clone_thread((yos_pthread_host *)ctx->pthread_host,
                              fn, arg,
                              (flags & CLONE_CHILD_CLEARTID) ? ctid_addr : 0,
                              (flags & CLONE_SETTLS) ? tls : 0,
                              child_stack,
                              ctx->memory,
                              &spawned_tid);
    if (rc != 0) {
        child->state = YOS_PROC_FREE;
        return rc;
    }

    /* CLONE_PARENT_SETTID: parent gets the new tid via *ptid_addr. */
    if ((flags & CLONE_PARENT_SETTID) && ptid_addr) {
        if (ptid_addr + 4 <= ctx->memory_size)
            *(uint32_t *)(ctx->memory + ptid_addr) = (uint32_t)child->pid;
    }
    /* CLONE_CHILD_SETTID: kernel writes tid into the child-side ctid
     * address up front (the matching CLEARTID + futex-wake on exit
     * brings it back to 0). musl's pthread_create relies on the
     * write happening before clone() returns, so anyone futex_wait'ing
     * on the address sees the non-zero tid value and parks correctly. */
    #ifndef CLONE_CHILD_SETTID
    #  define CLONE_CHILD_SETTID 0x01000000
    #endif
    if ((flags & CLONE_CHILD_SETTID) && ctid_addr) {
        if (ctid_addr + 4 <= ctx->memory_size) {
            *(uint32_t *)(ctx->memory + ctid_addr) = (uint32_t)child->pid;
            ydebug("clone: CHILD_SETTID wrote %d to ctid=0x%x\n",
                   child->pid, ctid_addr);
        }
    }

    ydebug("clone -> pid=%d (host slot=%u)\n", child->pid, spawned_tid);
    (void)spawned_tid;
    return child->pid;
}

/* ============================================================================
 * Vfork Implementation
 *
 * vfork is like fork but:
 * 1. Parent blocks until child calls exec or _exit
 * 2. Child shares parent's memory (in real vfork - we copy anyway in wasm)
 *
 * Flow:
 * 1. Create child like fork
 * 2. Set child->vfork_parent_pid = parent pid
 * 3. Parent blocks on parent->vfork_cond
 * 4. Child runs, calls exec or _exit
 * 5. exec/_exit signals parent->vfork_cond
 * 6. Parent unblocks, continues
 * ============================================================================ */

int32_t yos_vfork(struct yos_exec_ctx *ctx)
{
    if (!ctx->proc || !ctx->rt) {
        ydebug("vfork: invalid context\n");
        return -EINVAL;
    }

    IM3Runtime wrt = (IM3Runtime)ctx->runtime;
    IM3Module mod = (IM3Module)ctx->module;

    if (!wrt || !mod) {
        ydebug("vfork: no wasm runtime\n");
        return -ENOMEM;
    }

    int state = get_asyncify_state(wrt);

    /* If asyncify not available, vfork cannot work */
    if (state < 0) {
        ydebug("vfork: asyncify not available\n");
        return -ENOSYS;
    }

    /* REWIND path: returning from vfork after rewind */
    if (state == ASYNCIFY_REWINDING) {
        call_asyncify(wrt, "asyncify_stop_rewind", -1);
        ydebug("vfork rewind complete, returning %d\n", ctx->fork_return);
        return ctx->fork_return;
    }

    /* FIRST CALL path: trigger unwind */
    ydebug("vfork called by pid=%d\n", ctx->proc->pid);

    /* Get memory for asyncify buffer */
    uint32_t mem_size;
    uint8_t *mem = m3_GetMemory(wrt, &mem_size, 0);
    if (!mem || mem_size == 0) {
        return -ENOMEM;
    }

    /* Allocate asyncify buffer */
    if (ctx->asyncify_ptr == 0) {
        ctx->asyncify_ptr = mem_size - ASYNCIFY_BUF_SIZE;
    }

    /* Reset asyncify buffer header */
    uint32_t *buf = (uint32_t *)(mem + ctx->asyncify_ptr);
    buf[0] = ctx->asyncify_ptr + 8;
    buf[1] = ctx->asyncify_ptr + ASYNCIFY_BUF_SIZE;

    /* Allocate child process slot */
    struct yos_proc *child_proc = yos_proc_alloc(ctx->rt, ctx->proc->pid);
    if (!child_proc) {
        return -EAGAIN;
    }
    child_proc->pgid = ctx->proc->pgid;
    child_proc->sid = ctx->proc->sid;
    /* fork(2) inherits comm/exe/cwd from the parent. Without this
     * copy the child appears in /proc/<pid>/stat with an empty
     * COMMAND column until something execve's into it — surprising
     * for short-lived helpers and breaks `ps` output for any forked-
     * but-not-yet-exec'd process. execve replaces these on the
     * exec path. */
    memcpy(child_proc->comm, ctx->proc->comm, sizeof(child_proc->comm));
    memcpy(child_proc->exe,  ctx->proc->exe,  sizeof(child_proc->exe));
    memcpy(child_proc->cwd,  ctx->proc->cwd,  sizeof(child_proc->cwd));

    /* vfork specific: child stores parent pid, parent will block */
    child_proc->vfork_parent_pid = ctx->proc->pid;
    ctx->proc->vfork_child_done = 0;

    /* Store child pid as return value for parent */
    ctx->fork_return = child_proc->pid;
    ctx->fork_pending = 1;

    ydebug("vfork: starting unwind, child pid=%d, parent will block\n", child_proc->pid);
    call_asyncify(wrt, "asyncify_start_unwind", ctx->asyncify_ptr);

    return child_proc->pid;
}

/* vfork_pump - like fork_pump but parent blocks until child signals */
void yos_vfork_pump(struct yos_exec_ctx *ctx)
{
    while (ctx->fork_pending) {
        ctx->fork_pending = 0;

        IM3Runtime wrt = (IM3Runtime)ctx->runtime;
        IM3Module mod = (IM3Module)ctx->module;

        call_asyncify(wrt, "asyncify_stop_unwind", -1);

        /* Copy memory */
        uint32_t mem_size;
        uint8_t *mem = m3_GetMemory(wrt, &mem_size, 0);
        /* Anonymous mmap: zero-filled lazy pages, no upfront commit. */
        uint8_t *mem_copy = mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem_copy == MAP_FAILED) {
            struct yos_proc *child = yos_proc_find(ctx->rt, ctx->fork_return);
            if (child) child->state = YOS_PROC_FREE;
            return;
        }

        /* Allocate thread arg up front so snapshot can fill regions. */
        fork_thread_arg_t *fork_thread_arg = malloc(sizeof(fork_thread_arg_t));
        if (!fork_thread_arg) {
            munmap(mem_copy, mem_size);
            struct yos_proc *child = yos_proc_find(ctx->rt, ctx->fork_return);
            if (child) child->state = YOS_PROC_FREE;
            return;
        }
        if (snapshot_wasm_memory(mem_copy, mem, mem_size,
                                  fork_thread_arg->live_regions,
                                  YOS_FORK_MAX_REGIONS,
                                  &fork_thread_arg->live_region_count) < 0) {
            ydebug("vfork snapshot: region overflow / no /proc maps —"
                   " fall back to full memcpy\n");
            fork_thread_arg->live_region_count = 0;
        }

        /* Save globals */
        uint32_t num_globals = mod->numGlobals;
        int64_t *wasm_globals_copy = malloc(num_globals * sizeof(int64_t));
        if (!wasm_globals_copy) {
            munmap(mem_copy, mem_size);
            free(fork_thread_arg);
            struct yos_proc *child = yos_proc_find(ctx->rt, ctx->fork_return);
            if (child) child->state = YOS_PROC_FREE;
            return;
        }
        for (uint32_t i = 0; i < num_globals; i++) {
            wasm_globals_copy[i] = mod->globals[i].intValue;
        }

        struct yos_proc *child_proc = yos_proc_find(ctx->rt, ctx->fork_return);
        if (!child_proc) {
            munmap(mem_copy, mem_size);
            free(fork_thread_arg);
            free(wasm_globals_copy);
            return;
        }

        fork_thread_arg->rt = ctx->rt;
        fork_thread_arg->proc = child_proc;
        fork_thread_arg->memory_snapshot = mem_copy;
        fork_thread_arg->memory_size = mem_size;
        fork_thread_arg->asyncify_ptr    = ctx->asyncify_ptr;
        /* TODO(setjmp-refactor): forward ctx->sj_slots[] to the child. */
        fork_thread_arg->sj_discard_ptr  = ctx->sj_discard_ptr;
        fork_thread_arg->wasm_globals = wasm_globals_copy;
        fork_thread_arg->wasm_globals_count = num_globals;
        fork_thread_arg->heap_end = ctx->heap_end;
        fork_thread_arg->alloc_lo        = ctx->alloc_lo;
        fork_thread_arg->alloc_hi        = ctx->alloc_hi;
        fork_thread_arg->mmap_top        = ctx->mmap_top;
        fork_thread_arg->free_count      = ctx->free_count;
        memcpy(fork_thread_arg->free_list, ctx->free_list,
               sizeof(fork_thread_arg->free_list));
        fork_thread_arg->alloc_free_head = ctx->alloc_free_head;
        fork_thread_arg->argc = ctx->argc;
        fork_thread_arg->argv = ctx->argv;
        fork_thread_arg->envc = ctx->envc;
        fork_thread_arg->envp = ctx->envp;
        /* Private wasm_bytes per fork — see fork path for rationale. */
        fork_thread_arg->wasm_bytes_size = ctx->wasm_bytes_size;
        fork_thread_arg->wasm_bytes = malloc(ctx->wasm_bytes_size);
        if (!fork_thread_arg->wasm_bytes) {
            munmap(mem_copy, mem_size);
            free(wasm_globals_copy);
            free(fork_thread_arg);
            struct yos_proc *child = yos_proc_find(ctx->rt, ctx->fork_return);
            if (child) child->state = YOS_PROC_FREE;
            return;
        }
        memcpy(fork_thread_arg->wasm_bytes, ctx->wasm_bytes,
               ctx->wasm_bytes_size);
        /* Same stable-snapshot reasoning as in the fork path above —
         * dup parent's host fds NOW so the child sees what was open
         * at vfork time, not what's open whenever its dup loop runs. */
        for (int i = 0; i < YOS_FD_MAX; i++) {
            int phfd = ctx->fd_map[i];
            if (phfd < 0) {
                fork_thread_arg->parent_fd_map[i] = -1;
                continue;
            }
            int flags = fcntl(phfd, F_GETFD);
            int dupcmd = (flags >= 0 && (flags & FD_CLOEXEC))
                           ? F_DUPFD_CLOEXEC : F_DUPFD;
            int chfd = fcntl(phfd, dupcmd, 0);
            fork_thread_arg->parent_fd_map[i] = (chfd >= 0) ? chfd : -1;
        }
        memcpy(fork_thread_arg->parent_cwd, ctx->cwd,
               sizeof(fork_thread_arg->parent_cwd));
        memcpy(fork_thread_arg->parent_sig_handlers, ctx->sig_handlers,
               sizeof(fork_thread_arg->parent_sig_handlers));
        fork_thread_arg->parent_sig_ignore_mask = ctx->sig_ignore_mask;
        fork_thread_arg->parent_sig_mask    = ctx->sig_mask;
        fork_thread_arg->parent_sig_pending = ctx->sig_pending;
        memcpy(fork_thread_arg->parent_locale_name, ctx->locale_name,
               sizeof(fork_thread_arg->parent_locale_name));
        /* Dup each live parent FILE* so the child gets an
         * independent host FILE* on each inherited slot. Without
         * this, child fclose closes the underlying host file and
         * subsequent parent fputs/fwrite/fclose ENOENT/EBADF/
         * crashes on the freed FILE*. */
        for (int i = 0; i < 256; i++) {
            FILE *parent_fp = (FILE *)ctx->file_slots[i];
            fork_thread_arg->parent_file_dup_fps[i]  = NULL;
            fork_thread_arg->parent_file_dup_wfds[i] = -1;
            fork_thread_arg->parent_file_modes[i][0] = '\0';
            if (!parent_fp) continue;
            int phfd = fileno(parent_fp);
            if (phfd < 0) continue;
            int chfd = fcntl(phfd, F_DUPFD_CLOEXEC, 0);
            if (chfd < 0) chfd = fcntl(phfd, F_DUPFD, 0);
            if (chfd < 0) continue;
            const char *mode = ctx->file_modes[i][0]
                               ? ctx->file_modes[i] : "r+";
            FILE *cfp = fdopen(chfd, mode);
            if (!cfp) { close(chfd); continue; }
            fork_thread_arg->parent_file_dup_fps[i]  = cfp;
            fork_thread_arg->parent_file_dup_wfds[i] = ctx->file_wfds[i];
            size_t n = strlen(mode);
            if (n >= sizeof(fork_thread_arg->parent_file_modes[i]))
                n = sizeof(fork_thread_arg->parent_file_modes[i]) - 1;
            memcpy(fork_thread_arg->parent_file_modes[i], mode, n);
            fork_thread_arg->parent_file_modes[i][n] = '\0';
        }
        memcpy(&fork_thread_arg->parent_env_store, &ctx->env_store,
               sizeof(fork_thread_arg->parent_env_store));
        fork_thread_arg->parent_umask = ctx->umask;
        fork_thread_arg->forkpty_pending    = ctx->forkpty_pending;
        fork_thread_arg->forkpty_master_wfd = ctx->forkpty_master_wfd;
        fork_thread_arg->forkpty_slave_wfd  = ctx->forkpty_slave_wfd;

        /* Spawn child thread */
        child_proc->state = YOS_PROC_RUNNING;
        pthread_t t;
        int r = pthread_create(&t, NULL, fork_thread_func, fork_thread_arg);
        if (r != 0) {
            /* Same fd-leak fix as the fork path above — release the
             * dups stashed for a child thread that never ran. */
            release_parent_dups(fork_thread_arg);
            munmap(mem_copy, fork_thread_arg->memory_size);
            free(wasm_globals_copy);
            free(fork_thread_arg);
            child_proc->state = YOS_PROC_FREE;
            return;
        }

        /* vfork: parent blocks until child calls exec or _exit */
        ydebug("vfork: parent pid=%d blocking until child done\n", ctx->proc->pid);
        pthread_mutex_lock(&ctx->proc->lock);
        while (!ctx->proc->vfork_child_done) {
            pthread_cond_wait(&ctx->proc->vfork_cond, &ctx->proc->lock);
        }
        ctx->proc->vfork_child_done = 0;
        pthread_mutex_unlock(&ctx->proc->lock);
        ydebug("vfork: parent pid=%d unblocked\n", ctx->proc->pid);

        pthread_join(t, NULL);

        /* Resume parent */
        call_asyncify(wrt, "asyncify_start_rewind", ctx->asyncify_ptr);

        IM3Function start;
        m3_FindFunction(&start, wrt, "_start");
        m3_CallV(start);
    }
}

int32_t yos_tkill(struct yos_exec_ctx *ctx, int32_t tid, int32_t sig)
{
    ydebug("tkill(%d, %d)\n", tid, sig);
    /* yos models one thread per guest process — guest tid == guest pid. */
    if (tid <= 0) return -ESRCH;
    struct yos_proc *t = yos_proc_find(ctx->rt, tid);
    if (!t) return -ESRCH;
    return deliver_to_proc(t, sig);
}

int32_t yos_proc_set_thread_area(struct yos_exec_ctx *ctx)
{
    (void)ctx;
    ydebug("set_thread_area() = 0 (stub)\n");
    return 0; /* single-threaded wasm - pretend success */
}

int32_t yos_proc_get_thread_area(struct yos_exec_ctx *ctx)
{
    (void)ctx;
    ydebug("get_thread_area() = 0 (stub)\n");
    return 0; /* single-threaded wasm - pretend success */
}

int32_t yos_proc_exit_group(struct yos_exec_ctx *ctx, int32_t error_code)
{
    ydebug("exit_group(%d)\n", error_code);
    return yos_exit(ctx, error_code);
}

int32_t yos_proc_set_tid_address(struct yos_exec_ctx *ctx, uint32_t tidptr)
{
    ydebug("set_tid_address(0x%x)\n", tidptr);
    /* Late-bound CLONE_CHILD_CLEARTID: kernel will write 0 here and
     * futex-wake on thread exit. yos_exit consults this field. */
    if (ctx->proc) ctx->proc->tid_address = tidptr;
    return ctx->proc ? ctx->proc->pid : 1;
}

int32_t yos_tgkill(struct yos_exec_ctx *ctx, int32_t tgid, int32_t tid, int32_t sig)
{
    ydebug("tgkill(%d, %d, %d)\n", tgid, tid, sig);
    if (tid <= 0) return -ESRCH;
    struct yos_proc *t = yos_proc_find(ctx->rt, tid);
    if (!t) return -ESRCH;
    /* tgid > 0 sanity-check: must match the proc's tgid (= its pid in
     * our one-thread-per-proc model). */
    if (tgid > 0 && tgid != t->pid) return -ESRCH;
    return deliver_to_proc(t, sig);
}

int32_t yos_waitid(struct yos_exec_ctx *ctx, int32_t which, int32_t pid, uint32_t infop, int32_t options, uint32_t ru)
{
    (void)ru; /* TODO: rusage */
    ydebug("waitid(%d, %d, 0x%x, %d)\n", which, pid, infop, options);
    siginfo_t info;
    int ret = waitid((idtype_t)which, (id_t)pid, &info, options);
    if (ret < 0) {
        ydebug("waitid = %d (errno=%d)\n", -errno, errno);
        return yos_errno_neg(ctx, errno);
    }
    /* Copy siginfo to wasm memory - simplified, just copy pid and status */
    if (infop && infop < ctx->memory_size - 128) {
        int32_t *si = (int32_t *)(ctx->memory + infop);
        si[0] = info.si_signo;
        si[1] = info.si_errno;
        si[2] = info.si_code;
        si[3] = info.si_pid;
        si[4] = info.si_uid;
        si[5] = info.si_status;
    }
    ydebug("waitid = 0\n");
    return 0;
}

/* Identity getters: return guest values from the per-runtime proc
 * table. Forwarding to host getpid/etc. would leak the host TID
 * (a 6-digit number) into the guest namespace. */
int32_t yos_getpid(struct yos_exec_ctx *ctx)
{
    /* Linux: getpid() returns the thread-group id, not the per-thread
     * tid. For non-thread procs (init, fork, vfork) tgid == pid so
     * this is the same number; for clone(CLONE_THREAD) children, tgid
     * is the parent's tgid and pid is unique. */
    return ctx->proc ? ctx->proc->tgid : 1;
}

int32_t yos_getppid(struct yos_exec_ctx *ctx)
{
    return ctx->proc ? ctx->proc->ppid : 0;
}

int32_t yos_getsid(struct yos_exec_ctx *ctx, int32_t pid)
{
    if (pid == 0) return ctx->proc ? ctx->proc->sid : 1;
    struct yos_proc *p = yos_proc_find(ctx->rt, pid);
    return p ? p->sid : -ESRCH;
}

int32_t yos_getpgid(struct yos_exec_ctx *ctx, int32_t pid)
{
    if (pid == 0) return ctx->proc ? ctx->proc->pgid : 1;
    struct yos_proc *p = yos_proc_find(ctx->rt, pid);
    return p ? p->pgid : -ESRCH;
}

int32_t yos_getpgrp(struct yos_exec_ctx *ctx)
{
    return ctx->proc ? ctx->proc->pgid : 1;
}

int32_t yos_gettid(struct yos_exec_ctx *ctx)
{
    /* One thread per guest proc — gettid() == getpid(). */
    return ctx->proc ? ctx->proc->pid : 1;
}

/* setpgid(pid=0, pgid=0): caller becomes pgrp leader of its own pgrp.
 * setpgid(pid=0, pgid=N): move caller to pgrp N (must already exist
 * in the same session, or N==caller's own pid).
 * setpgid(pid=P, pgid=N): same but for proc P (must be caller or child;
 * we don't enforce that here — userspace rarely abuses this).
 *
 * The reference is POSIX setpgid(2). Restrictions we *don't* enforce:
 *   - "session leader can't change pgid" (sid==pid for leaders)
 *   - "target must not have execve()'d since fork"
 * These are not load-bearing for the shells we run; if they become so
 * we'll add them. */
int32_t yos_setpgid(struct yos_exec_ctx *ctx, int32_t pid, int32_t pgid)
{
    ydebug("setpgid(%d, %d)\n", pid, pgid);
    if (!ctx->proc) return -ESRCH;
    struct yos_proc *target = (pid == 0)
        ? ctx->proc
        : yos_proc_find(ctx->rt, pid);
    if (!target) return -ESRCH;
    int32_t new_pgid = (pgid == 0) ? target->pid : pgid;
    if (new_pgid <= 0) return -EINVAL;
    target->pgid = new_pgid;
    return 0;
}

/* setsid: caller becomes leader of a new session and pgrp.
 * Fails if caller is already a pgrp leader (sid==pid AND someone else
 * shares the pgrp). Simpler check: forbid if pgid==pid (we're already
 * leading some pgrp). */
int32_t yos_setsid(struct yos_exec_ctx *ctx)
{
    ydebug("setsid()\n");
    if (!ctx->proc) return -ESRCH;
    /* Conservative: deny if already a pgrp leader (POSIX EPERM). */
    if (ctx->proc->pgid == ctx->proc->pid &&
        ctx->proc->sid == ctx->proc->pid) {
        /* Already a session leader: the call is idempotent in spirit;
         * Linux returns EPERM, but for simple init/shell flows we can
         * just succeed-and-return-current-sid. */
        return ctx->proc->sid;
    }
    ctx->proc->sid  = ctx->proc->pid;
    ctx->proc->pgid = ctx->proc->pid;
    /* New session has no controlling tty; kernel would also clear that.
     * We leave rt->fg_pgid alone — anything that opens /dev/tty later
     * will be the new owner if it does TIOCSCTTY. */
    return ctx->proc->sid;
}

int32_t yos_proc_arch_prctl(struct yos_exec_ctx *ctx)
{
    (void)ctx;
    ydebug("arch_prctl() = 0 (stub, not relevant for wasm32)\n");
    return 0; /* arch_prctl is x86-64 specific, not relevant for wasm32 */
}

/* ============================================================================
 * Time syscalls with 32-bit struct conversion
 * ============================================================================ */

/* 32-bit timespec (i386 uses 32-bit time_t) */
struct old_timespec32 {
    int32_t tv_sec;
    int32_t tv_nsec;
};

int32_t yos_proc_clock_gettime(struct yos_exec_ctx *ctx, int32_t clockid, uint32_t tp_addr)
{
    ydebug("clock_gettime(%d, 0x%x)\n", clockid, tp_addr);

    if (tp_addr == 0 || tp_addr >= ctx->memory_size - sizeof(struct old_timespec32)) {
        return yos_errno_neg(ctx, EFAULT);
    }

    struct timespec ts;
    int ret = clock_gettime(clockid, &ts);
    if (ret < 0) {
        ydebug("clock_gettime = %d\n", -errno);
        return yos_errno_neg(ctx, errno);
    }

    /* Convert host64 timespec to wasm32 old_timespec32 */
    struct old_timespec32 *tp = (struct old_timespec32 *)(ctx->memory + tp_addr);
    tp->tv_sec = (int32_t)ts.tv_sec;
    tp->tv_nsec = (int32_t)ts.tv_nsec;

    ydebug("clock_gettime = 0 (sec=%d, nsec=%d)\n", tp->tv_sec, tp->tv_nsec);
    return 0;
}

int32_t yos_proc_clock_getres(struct yos_exec_ctx *ctx, int32_t clockid, uint32_t res_addr)
{
    ydebug("clock_getres(%d, 0x%x)\n", clockid, res_addr);

    struct timespec ts;
    int ret = clock_getres(clockid, res_addr ? &ts : NULL);
    if (ret < 0) {
        return yos_errno_neg(ctx, errno);
    }

    if (res_addr && res_addr < ctx->memory_size - sizeof(struct old_timespec32)) {
        struct old_timespec32 *res = (struct old_timespec32 *)(ctx->memory + res_addr);
        res->tv_sec = (int32_t)ts.tv_sec;
        res->tv_nsec = (int32_t)ts.tv_nsec;
    }

    return 0;
}

int32_t yos_proc_nanosleep(struct yos_exec_ctx *ctx, uint32_t rqtp_addr, uint32_t rmtp_addr)
{
    ydebug("nanosleep(0x%x, 0x%x)\n", rqtp_addr, rmtp_addr);

    if (rqtp_addr == 0 || rqtp_addr >= ctx->memory_size - sizeof(struct old_timespec32)) {
        return yos_errno_neg(ctx, EFAULT);
    }

    /* Convert wasm32 old_timespec32 to host64 timespec */
    struct old_timespec32 *rqtp = (struct old_timespec32 *)(ctx->memory + rqtp_addr);
    struct timespec req = {
        .tv_sec = rqtp->tv_sec,
        .tv_nsec = rqtp->tv_nsec
    };
    struct timespec rem;

    int ret = nanosleep(&req, &rem);
    if (ret < 0) {
        /* On EINTR, copy remaining time */
        if (errno == EINTR && rmtp_addr && rmtp_addr < ctx->memory_size - sizeof(struct old_timespec32)) {
            struct old_timespec32 *rmtp = (struct old_timespec32 *)(ctx->memory + rmtp_addr);
            rmtp->tv_sec = (int32_t)rem.tv_sec;
            rmtp->tv_nsec = (int32_t)rem.tv_nsec;
        }
        return yos_errno_neg(ctx, errno);
    }

    return 0;
}
