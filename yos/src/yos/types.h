#ifndef YOS_TYPES_H
#define YOS_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <limits.h>      /* PATH_MAX from POSIX libc, not Linux UAPI */
#include "yos_autoglobals.h"   /* generated: struct yos_autoglobals */

/* Asyncify states (from Binaryen's asyncify transformation) */
#define ASYNCIFY_NORMAL    0
#define ASYNCIFY_UNWINDING 1
#define ASYNCIFY_REWINDING 2

/* Asyncify buffer size (reused for each fork) */
#define ASYNCIFY_BUF_SIZE  16384

/* Max processes in table */
/* yos's proc-table slot count. Each forked guest, including the
 * always-alive supervisor chain (runsvdir → runsv → yos-tcpserver
 * → telnetd → its PTY child → the user shell) consumes one slot;
 * with the legacy cap of 64 a perf-stress chaos round of `-k 64`
 * concurrent kids tipped the table over and the next fork(2)
 * returned -1 mid-burst. 256 is enough headroom for a few hundred
 * concurrent guests and costs about 100 KiB of host memory total. */
#define YOS_MAX_PROCS      256

/* Max free regions for mmap reuse */
#define YOS_MAX_FREE_REGIONS 64

/* Max tracked LIVE mmap regions. Auxiliary bookkeeping so MAP_FIXED can
 * tell a live mapping apart from a hole and munmap can drop ownership.
 * Best-effort: on overflow a mapping simply isn't tracked (no corruption
 * — the free-list double-free guard is the hard invariant). */
#define YOS_MAX_LIVE_REGIONS 128

/* A memory region [addr, addr+len) in WASM linear memory. Used for both
 * the free list (reusable holes) and the live list (owned mappings). */
struct yos_free_region {
    uint32_t addr;
    uint32_t len;
};

/* Process states */
typedef enum {
    YOS_PROC_FREE = 0,
    YOS_PROC_READY,
    YOS_PROC_RUNNING,
    YOS_PROC_ZOMBIE,
} yos_proc_state_t;

/* Process identity (slot in process table) */
struct yos_proc {
    int32_t pid;
    int32_t ppid;
    int32_t pgid;
    int32_t sid;
    /* Thread-group id. For threads spawned via clone(CLONE_THREAD), this
     * is the tgid of the calling task — getpid() returns proc->tgid,
     * gettid() returns proc->pid, so all threads of a task agree on
     * getpid(). For everything else (init, fork, vfork) tgid == pid.
     * Default-initialised to pid in yos_proc_alloc. */
    int32_t tgid;
    /* CLONE_CHILD_CLEARTID address (wasm offset). On thread/proc exit
     * the worker atomically writes 0 here and futex-wakes any waiter,
     * which is what unblocks pthread_join. set_tid_address(2) updates
     * this late-bound; CLONE_CHILD_CLEARTID sets it at clone time. */
    uint32_t tid_address;
    yos_proc_state_t state;
    int32_t exit_code;
    /* If non-zero, this proc was terminated by signal `term_sig`
     * (FreeBSD signum). yos_waitpid packs term_sig into the low
     * 7 bits of the POSIX status word so guest code using
     * WIFSIGNALED / WTERMSIG observes the right semantics. */
    int32_t term_sig;

    pthread_mutex_t lock;
    pthread_cond_t wait_cond;
    int exited;

    /* vfork support: parent blocks until child exec/_exit */
    int32_t vfork_parent_pid;
    pthread_cond_t vfork_cond;
    int vfork_child_done;

    pthread_t thread;

    /* Back-pointer to the running ctx for cross-thread access (kill →
     * sig_pending, /proc/[pid] introspection from another guest, …).
     * void* because struct yos_exec_ctx is defined below this struct
     * and we don't want to reorder the file. Set by fork_thread_func
     * and the initial main.c thread when each first enters wasm.
     * NULL when the proc is zombie/freed.
     *
     * Cross-thread reads of fields THROUGH this pointer must use atomics
     * where applicable (sig_pending is the obvious one). */
    void *ctx_handle;

    /* Process info for /proc/[pid] (stored per-process, not per-ctx) */
    char comm[16];           /* command name (basename of exe) */
    char exe[PATH_MAX];      /* path to executable */
    char cwd[PATH_MAX];      /* current working directory */
    char **cmdline;          /* command line args (NULL-terminated) */
    int cmdline_argc;
    /* Set to 1 by yos_proc_post_exit_cleanup when this proc's original
     * parent died and we forcibly set ppid=1. POSIX init auto-reaps
     * orphans; yos's init (runsvdir, or whatever pid 1 happens to be)
     * does not. We auto-reap at the orphan's exit time to keep the
     * proc table clean — but ONLY when this flag is set, NOT just
     * because ppid==1 (a legitimate child of the root guest pid 1
     * still needs waitpid by its parent). */
    int was_orphaned;
};

struct yos_runtime;  /* forward decl */

/* Per-ctx environment store entry. Cap defined here too so impl/env.c
 * doesn't carry its own private constant. */
#define YOS_ENV_MAX 512
struct yos_env_entry {
    uint32_t name_off;   /* wasm-memory offset of the name copy */
    uint32_t value_off;  /* wasm-memory offset of the value copy */
    uint32_t name_len;
};

struct yos_exec_ctx {
    /* Links */
    struct yos_runtime *rt;
    struct yos_proc *proc;

    /* WASM state */
    uint8_t *memory;      /* wasm linear memory base */
    uint32_t memory_size; /* wasm linear memory size */
    uint32_t heap_end;    /* brk position — what brk(0) reports back */
    /* Separate watermark for anonymous mmap2 allocations. wasm_brk
     * (heap_end above) can shrink (musl gives memory back to the kernel
     * in the oldmalloc __bin_chunk path), but mmap allocations above
     * the new brk are still LIVE. If mmap2 also used heap_end, a brk
     * shrink would let a later mmap2 hand out memory that was already
     * given to a prior mmap2 — silent heap corruption. So keep mmap2
     * on its own monotonically-rising cursor. */
    uint32_t mmap_top;
    void *runtime;        /* IM3Runtime */
    void *module;         /* IM3Module */
    uint8_t *wasm_bytes;  /* raw wasm binary (kept for fork) */
    size_t wasm_bytes_size;

    /* Memory free list (reusable holes) + live list (owned mappings).
     * free_list feeds find_free_region; live_list lets munmap drop the
     * exact mapping and lets MAP_FIXED distinguish a live range from a
     * hole instead of blindly clobbering it. Both guarded by mem_lock. */
    struct yos_free_region free_list[YOS_MAX_FREE_REGIONS];
    int free_count;
    struct yos_free_region live_list[YOS_MAX_LIVE_REGIONS];
    int live_count;
    pthread_mutex_t mem_lock;

    /* Per-ctx host-side pthread implementation. MUST be per-ctx (not
     * per-runtime) because yos_pthread_host pins the master IM3Runtime
     * and the wasm-bytes pointer at first thread creation. With one
     * runtime-wide host, every guest's pthread_create would clone the
     * FIRST guest that ever spawned a thread — fork+exec of a different
     * wasm produces "threads run pid=1's code on pid=2's memory" and
     * mutex/condvar/rwlock all silently no-op. Lazy-init from main.c's
     * pthread_host_link path. */
    void *pthread_host;

    /* Command line */
    int argc;
    char **argv;
    /* Environment passed to wasm. envc is the number of strings; envp
     * is a NULL-terminated array of host-side strdup'd strings. NULL
     * envp means "no environment" (treated as empty for crt1 purposes). */
    int envc;
    char **envp;

    /* Fork state (asyncify-based) */
    uint32_t asyncify_ptr;
    int fork_pending;
    int32_t fork_return;
    int is_child;
    /* forkpty stash (impl/libc/syslog_extras.c m3_forkpty). The bridge
     * opens the pty pair BEFORE unwinding through yos_fork; both sides
     * re-execute the bridge on rewind and finish their half — the child
     * wires the slave onto fds 0/1/2 (login_tty recipe), the parent
     * keeps only the master. The guest fd NUMBERS are stashed here and
     * copied to the child ctx by the fork pump (host fds transfer via
     * the ordinary parent_fd_map dup path). */
    int forkpty_pending;
    int32_t forkpty_master_wfd;
    int32_t forkpty_slave_wfd;
    /* Set on the CHILD side of forkpty. On exit, the parent gets a
     * SIGCHLD: a pty child is a hand-rolled fork, so no EVFILT_PROC
     * filter watches it — the parent's sigaction (nvim's libuv SIGCHLD
     * watcher) is the only way it learns the exit and can waitpid.
     * Scoped to forkpty children so ordinary fork/wait flows (zsh,
     * tmux) keep their existing wait/sigsuspend-driven delivery. */
    int is_forkpty_child;

    /* Hand-bridged iconv(3) handles (impl/libc/iconv.c). The guest sees
     * a small handle (slot index + 1); the host iconv_t lives here. A
     * codegen passthrough CANNOT work for iconv: its char** in/out
     * arguments carry guest OFFSETS behind the outer pointer (host
     * iconv would dereference a wasm offset as a host address — SIGSEGV
     * in glibc, hit by nvim's :terminal input-encoding conversion), and
     * iconv_open's iconv_t return would truncate a host pointer into
     * the guest's 32-bit world. NOT inherited across fork (host
     * iconv_t is not shareable); freed via yos_iconv_ctx_free at exit. */
    void *iconv_slots[16];

    /* AUTO-isolated libc globals — bridge.py emits the per-ctx
     * field definitions for everything marked auto_save_restore in
     * the policy file. yos_autoglobals.h is generated; if you don't
     * see your global here at runtime, check whether it's listed.
     * The header path resolves through codegen's include dir. */
    struct yos_autoglobals autoglobals;

    /* Per-ctx libc-globals isolation (build-tools/libbridge/policies/libc.yaml).
     *
     * Each field below replaces a SHARED host-libc global that, left
     * unguarded, would let guest A's call corrupt guest B's view.
     * The bridges (impl/getopt.c, impl/tz.c, …) read+write these
     * ctx fields instead of touching the host global directly.
     *
     * Adding a new entry here = corresponding policy.yaml entry MUST
     * be promoted from `leaks` to `bridged_per_ctx` with `via:` =
     * the impl file. The extractor's --fail-on-leak catches gaps. */

    /* getopt state (optind/optarg/optopt/opterr). impl/getopt.c
     * implements the parser itself (FreeBSD-derived) and never calls
     * host getopt — eliminates the host-libc-global write entirely.
     * `optind = 1` is the POSIX initial value; opterr defaults to 1
     * (print error messages). */
    struct {
        int   optind;       /* next argv index to inspect; init 1 */
        int   opterr;       /* if non-zero, print errors; init 1   */
        int   optopt;       /* the unrecognized opt char            */
        uint32_t optarg_off;/* wasm-memory offset of current optarg */
    } getopt_state;

    /* Timezone state. tzset() with $TZ set mutates host tzname/
     * timezone/daylight. impl/tz.c swaps them in from this ctx slot
     * before any timezone-sensitive call (localtime/mktime/strftime),
     * swaps host's previous state out after.
     * Initialised lazily: `initialized=0` until first tzset/localtime. */
    struct {
        int   initialized;
        char  tzname0[64];      /* tzname[0] copy (e.g. "CET")     */
        char  tzname1[64];      /* tzname[1] copy (e.g. "CEST")    */
        long  timezone;         /* seconds west of UTC             */
        int   daylight;         /* 1 if DST observed in this zone  */
    } tz_state;

    /* Environment store. Backing table for getenv/setenv/unsetenv/
     * putenv/clearenv (impl/libc/env.c). Entries hold WASM-MEMORY
     * offsets (name_off / value_off) into strings yos_malloc'd in
     * the guest's mimalloc arena. Lives per-ctx so two guests' (or
     * a parent + forked-child's) setenv calls don't trample each
     * other. fork copies the parent's env_store into the child's
     * fresh ctx; the underlying string bytes ride along with the
     * wasm linear-memory snapshot, so the offsets stay valid.
     *
     * YOS_ENV_MAX caps the entry count. e[i].name_off==0 means a
     * deleted slot (unsetenv compacts so the live entries are
     * contiguous from 0..count-1). */
    struct {
        struct yos_env_entry e[YOS_ENV_MAX];
        int    count;
        int    initialised;
    } env_store;

    /* Per-ctx umask (file-creation mode mask). FreeBSD/POSIX umask
     * applies to open/creat/mkdir/mkfifo/mknod — the host kernel
     * does this at syscall time using the HOST PROCESS umask, which
     * is shared across all yos guests (they're host pthreads of one
     * process). Without per-ctx storage, child's umask(077) leaks
     * into parent. yos sets the host umask to 0 once at startup and
     * applies ctx->umask in software when calling host open/mkdir/
     * etc. (see impl/io/io.c).
     *
     * 022 is the POSIX-shell default and what bash/zsh inherit on
     * Linux; pick the same so guests started with no explicit umask
     * still see normal "rw-r--r--" file modes. */
    unsigned short umask;

    /* Per-ctx FILE* table (impl/io/file.c). Indices 1..YOS_FILE_MAX-1
     * are wasm-handle slots; indices 1/2/3 are reserved for
     * stdin/stdout/stderr and resolved separately. Per-ctx storage
     * prevents child fclose(handle) from invalidating parent's
     * still-live handle, and fork dups the underlying host FILE*
     * via fdopen(dup(fileno)) so each side has independent close
     * semantics. file_modes[i] holds the fopen mode string so fork
     * can reconstruct the FILE* with the right access flags. */
    void   *file_slots[256];    /* host FILE *; NULL when slot is free.
                                 * Must match YOS_FILE_MAX in file.c. */
    int32_t file_wfds [256];    /* wasm fd that wraps each FILE*'s
                                 * fileno (-1 if not allocated). */
    char    file_modes[256][8]; /* fopen mode strings — needed by fork
                                 * to call fdopen on the dup'd fd. */

    /* Per-ctx anchors for the static-buffer libc returns (strerror,
     * localtime, gmtime, ctime, getpwuid, getgrgid, ttyname, setlocale,
     * inet_ntoa-style, etc). impl/libc/pwd.c historically held these
     * as FILE-SCOPE statics — shared across every guest in the same
     * yos host process. Two concurrent guests calling, e.g., strerror
     * would race on the same anchor: first guest allocates a wasm
     * buffer at offset X in its own memory and caches X here, second
     * guest sees the cached X and tries to write into ITS memory at
     * offset X — which may or may not be a valid yos_malloc'd buffer.
     * Manifestations include strerror returning empty strings, ls
     * printing the same user/group repeatedly, and intermittent
     * SIGSEGV when a guest's mimalloc bookkeeping never claimed the
     * cached offset. Moving them on-ctx is the same fix shape as the
     * env_store and FILE* table fixes earlier. */
    struct {
        uint32_t pwd, grp, login, ufu, gfg;
        uint32_t tm, timestr;
        uint32_t errstr, gaistr, hstr, signam;
        uint32_t ttyname, ctermid, dirname, l64a, nl_langinfo;
        uint32_t setlocale, getwd, tempnam, getusershell, tmpnam, proto;
    } pwd_anchors;

    /* Lazy-allocated per-ctx tables. Each was a process-wide static
     * before. Pointer + capacity instead of inline array so ctxs that
     * never use the corresponding feature don't pay the memory cost. */
    void   *dir_slots;          /* yos_dir_slot[YOS_DIR_MAX] — opendir */
    void   *pty_head;           /* head of pty_entry linked list */
    int32_t pty_next_id;        /* next-assigned PTY id; start at 1 */
    void   *fifo_head;          /* head of fifo_entry linked list */
    void   *kq_proc_watches;    /* kqueue proc_watch[MAX_PROC_WATCH] */
    uintptr_t kq_proc_watch_counter; /* darwin kqueue ident sequence */
    void   *kq_tty_watchers;    /* darwin TTY watchers pointer array */

    /* ydev device handle tables (audio/camera/sensor/location).
     * Each is an array of pointers indexed by a guest-side handle
     * id. Were process-wide; concurrent guests would alias each
     * other's handles. */
    void   *ydev_cam;           /* ydev_camera_t   *[YDEV_BR_HMAX] */
    void   *ydev_ain;           /* ydev_audio_in_t *[YDEV_BR_HMAX] */
    void   *ydev_aout;          /* ydev_audio_out_t*[YDEV_BR_HMAX] */
    void   *ydev_sens;          /* ydev_sensor_t   *[YDEV_BR_HMAX] */
    void   *ydev_loc;           /* ydev_loc_t      *[YDEV_BR_HMAX] */

    /* FreeBSD userland helpers (impl/libc/freebsd_userland.c). */
    uint32_t progname_off;      /* getprogname()'s cached wasm offset */
    struct {                    /* fgetln stash — was "single ctx" static */
        uint32_t buf_off;       /* wasm offset of last-line buffer */
        uint32_t buf_cap;       /* capacity */
        uint32_t buf_len;       /* current line length */
        void    *host_fp;       /* FILE* this stash belongs to */
    } fgetln_stash;

    /* Fake-PTY termios (impl/libc/posix.c). Real PTYs aren't available
     * on sandboxed iOS/tvOS so yos synthesises termios state per-ctx.
     * Was a process-wide singleton — two guests' tcsetattr clobbered
     * each other. termios is ~44 bytes on FreeBSD wasm32; we store
     * it as opaque bytes to avoid needing the host's struct termios
     * definition here. */
    int      fake_pty_termios_init;
    uint8_t  fake_pty_termios[256];

    /* Resolver state — deferred. Adding requires bridging
     * gethostbyname/getaddrinfo via res_n* reentrant variants.
     * Until then env.gethostbyname is stubbed in the auto-bridge. */
    void *resolver_state;       /* future: struct __res_state * */

    /* Locale state — current `setlocale` argument per-ctx. impl/pwd.c
     * currently calls host setlocale directly (leak); to be fixed by
     * holding the locale string here and re-applying via uselocale
     * (per-thread, glibc) on each bridge entry. */
    char locale_name[64];       /* "" = host default */

    /* Per-guest bridged-library state.
     *
     * yos hosts arbitrary native libraries (libpython3.12, future:
     * libsqlite, libssl, ...) and exposes their C APIs to wasm guests
     * via env.* bridges (impl/libpython.c et al). Each guest gets its
     * own slice of every library's state — held here, owned by the
     * library bridge, opaque to the rest of yos. NULL until the guest
     * calls the library's init bridge.
     *
     * py_tstate: PyThreadState * of this guest's CPython subinterpreter.
     *   Subinterpreters give each guest its own sys.modules / builtins /
     *   (3.12+) GIL — without them two guests sharing one libpython
     *   would see each other's monkey-patches and global mutation.
     *   See impl/libpython.c. */
    void *py_tstate;

    /* PyObject handle table — same shape as ssl_handles[] /
     * lua_handles[]. Every PyObject * the bridge surface passes back
     * to the guest is wrapped here and the guest holds an i32
     * handle. Per-subinterpreter isolation guarantees that handles
     * across different guests cannot collide: each guest's
     * PyObject*'s are allocated inside its own PyInterpreterState
     * arena. See policies/python.yaml. */
    void   **py_handles;
    uint32_t py_handles_cap;

    /* openssl per-guest state. yos's host openssl is shared across every
     * guest (one libcrypto/libssl in the address space), but each guest's
     * SSL_CTX, SSL, EVP_MD_CTX, BIO, ... must be isolated — guest A's
     * SSL_CTX_set_verify_callback may not be observable from guest B.
     *
     * The bridge passes opaque host pointers to the guest as i32 handle
     * IDs through a per-ctx handle table. ssl_handles is a void* array
     * indexed 1..ssl_handles_cap-1 (slot 0 reserved so a valid handle
     * is never 0/NULL); first free slot is found by linear scan. Freed
     * slots are reused. Cleared on ctx teardown; the bridge's
     * yos_openssl_ctx_free walks the table and calls the appropriate
     * destructor for each live handle.
     *
     * No host openssl global mutates per-guest behaviour — algorithm
     * registries are init-once + immutable, the error queue lives in
     * ERR_get_error()'s per-thread storage (and yos's fork=pthread
     * model gives per-guest threads), RNG state is per-process and
     * shared (which is fine: the guest can't disable reseed). See
     * build-tools/libbridge/policies/openssl.yaml. */
    void   **ssl_handles;
    uint32_t ssl_handles_cap;

    /* liblua per-guest state. Each guest gets its own lua_State *
     * (from luaL_newstate at first env.luaL_newstate). lua's design
     * is the cleanest possible embedding case: there are NO mutable
     * file-scope globals in liblua, every API takes lua_State *L
     * explicitly, all per-instance state lives inside L. So the
     * bridge just resolves the i32 handle the guest holds back to
     * the host lua_State *.
     *
     * lua_handles[1] is the main state for this guest;
     * lua_handles[N>1] hold coroutine lua_State *'s created via
     * lua_newthread (those share globals/registry with the main
     * state but have their own stacks). Slot 0 reserved.
     *
     * See impl/libc/liblua.c and policies/lua.yaml. */
    void   **lua_handles;
    uint32_t lua_handles_cap;

    /* libarchive per-guest state. libarchive is the textbook reentrant
     * case (build-tools/libbridge analysis: ZERO writable globals; all
     * state lives in the caller-owned `struct archive *` /
     * `struct archive_entry *`). So the bridge just maps the i32 handle
     * the guest holds to the host pointer. Both archive and entry
     * pointers share this table; slot 0 reserved. See
     * impl/libc/libarchive.c.
     *
     * arc_handle_kinds is a parallel byte array (same cap, same index)
     * tagging each live slot as archive vs entry. Teardown needs it to
     * archive_read_free() only the leaked archive handles — entry
     * pointers are owned by their parent archive and must not be freed
     * directly. 0 = empty, see YOS_ARC_KIND_* in libarchive.c. */
    void   **arc_handles;
    uint8_t *arc_handle_kinds;
    uint32_t *arc_handle_owner;   /* for ENTRY slots: owning archive handle */
    uint32_t arc_handles_cap;

    /* "Did this ctx write to stderr (wfd=2) since the last failed exec?"
     * Used by yos_exit to detect a forked child that died after exec
     * failure without printing — under asyncify-fork, zsh's zwarning code
     * path doesn't reach env.fputc in the child (the *parent* path prints
     * fine). Yos synthesises the diagnostic itself in that case so the
     * user sees something instead of dead silence. Bumped by yos_write/
     * yos_fputc/yos_fwrite/yos_fputs/yos_vfprintf when fd/handle resolves
     * to wfd 2; reset by yos_execve on each attempt. last_failed_exec_*
     * remember the most recent ENOENT-class execve so we can format it. */
    int stderr_written_since_exec;
    char last_failed_exec_path[256];
    int last_failed_exec_errno;

    /* setjmp/longjmp state (asyncify-based, see m3_setjmp/m3_longjmp).
     *
     * Each LIVE setjmp has its own slot in sj_slots[] keyed by the user's
     * jmp_buf address. Per-slot we keep:
     *   - asyncify_buf:    wasm-side buffer asyncify_start_unwind writes to
     *   - save_data:       host-side snapshot of that buffer (rewind
     *                      consumes the buffer as it replays, so each
     *                      longjmp has to restore it from this copy)
     *
     * Why per-slot, not a single global: an inner setjmp would overwrite
     * an outer setjmp's saved state. Test 5 in tests/ut/yos/test_setjmp.c
     * (outer-longjmp skips inner) is the regression that pinned this.
     *
     * sj_discard_ptr is shared scratch — longjmp unwinds INTO it just to
     * leave the wasm call stack, then the pump throws it away.
     *
     * setjmp_pending is set on setjmp's first call to flag the unwind→
     * rewind round-trip the main pump must drive (so setjmp returns 0
     * with proper stack state captured). longjmp_pending + longjmp_value
     * + longjmp_target drive the rewind from the matching slot's
     * asyncify_buf; m3_setjmp returns longjmp_value when its own
     * jmp_buf_ptr equals longjmp_target. */
    struct yos_sj_slot {
        uint32_t jmp_buf_ptr;        /* user's jmp_buf addr; 0 = unused */
        uint32_t asyncify_buf;       /* wasm asyncify save buffer addr */
        void    *save_data;          /* host snapshot of that buffer */
        size_t   save_size;
    } sj_slots[64];
    /* Index of the slot whose first-call unwind is in flight (-1 = none).
     * set by m3_setjmp before asyncify_start_unwind, consumed by the pump. */
    int      setjmp_pending_slot;
    uint32_t sj_discard_ptr;
    int      setjmp_pending;        /* legacy: == (setjmp_pending_slot>=0) */
    int      longjmp_pending;
    int32_t  longjmp_value;
    /* Trap result captured from inside yos_setjmp_pump's _start invocation
     * so the caller can detect that the wasm trapped during a rewind step
     * (the pump otherwise discards the m3_CallV return). NULL if no trap. */
    const char *pump_trap;
    /* User's jmp_buf the active longjmp is targeting. m3_setjmp matches
     * its own arg against this so an inner setjmp doesn't falsely consume
     * an outer setjmp's longjmp. 0 means "no longjmp in flight". */
    uint32_t longjmp_target;

    /* Exec state */
    int exec_pending;
    char exec_path[PATH_MAX];
    int exec_argc;
    char **exec_argv;  /* heap-allocated, freed after exec */
    int exec_envc;
    char **exec_envp;  /* heap-allocated, NULL means "inherit ctx->envp" */

    /* Virtual file table for procfs etc. (allocated on first use) */
    void *procfs_fds;

    /* Per-runtime fd table. fd_map[wasm_fd] = host_fd; -1 means slot
     * unused (wasm fd not open). Every fd-allocating syscall (open /
     * socket / pipe / dup2 / accept4 / epoll_create1 / …) returns a
     * wasm fd allocated from this table; every fd-consuming syscall
     * translates wasm→host before invoking the kernel. Fork dups each
     * used host fd so parent and child have independent host fds for
     * the same wasm fd numbers, which is the only way to support
     * fork-as-thread without one runtime's close/dup2 trampling the
     * other's fds. wfd 0/1/2 default to host 0/1/2 (inherited stdio);
     * other slots are -1 until allocated. Virtual fds (procfs etc.)
     * use wfd ≥ YOS_VFS_FD_BASE and skip this table. */
#define YOS_FD_MAX 256
    int fd_map[YOS_FD_MAX];
    /* Per-fd absolute path. Populated by path-taking opens (open/openat/
     * creat/opendir). NULL means "we don't know the path" (sockets,
     * pipes, fcntl-dup, accept, etc.). Used by yos_fchdir to update
     * ctx->cwd without asking the host kernel — yos deliberately does
     * not consult /proc or any kernel-specific fd→path facility. The
     * string is yos_malloc'd from the host heap; freed on close /
     * release; strdup'd on fork. */
    char *fd_paths[YOS_FD_MAX];
    /* yos_fd_table_init runs at every load_wasm_module call (initial
     * load + every execve). After the first run, parent's dup2/redirect
     * setup must NOT be wiped by a second init. fd_table_inited stays
     * set across execve so the second call is a no-op. */
    int fd_table_inited;

    /* Current working directory (tracked for virtual paths) */
    char cwd[PATH_MAX];

    /* Per-runtime POSIX timer_t handle table. Host timer_t is `void *`
     * (8 bytes); wasm32 stores the timer id in 4 bytes — we hand the
     * wasm side a small int32 index and look up the real `void *` here
     * before each timer_settime/gettime/delete. Kept per-ctx so a
     * forked child gets its own table; a global one would let one
     * process delete or read timers another owns. 0 means slot free. */
#define YOS_TIMER_MAX 64
    void *timer_ids[YOS_TIMER_MAX];
    pthread_mutex_t timer_lock;
    int             timer_lock_init;

    /* Wasm offset of the int-sized slot that backs the FreeBSD `errno`
     * macro (#define errno (*__error())). yos___error(ctx) returns this
     * offset; bridges write here on error paths so client code's
     * `errno` reads the right value. Per-ctx for now (single-thread
     * correct); pthread workers need their own slot — TODO. */
    uint32_t errno_off;

    /* Guest-facing allocator state (impl/alloc.c).
     *
     * Pure free-list allocator inside [alloc_lo, alloc_hi) of the
     * guest's wasm linear memory. State (free-list head + block
     * headers) lives IN ctx->memory itself, so on execve the new
     * runtime's fresh linear memory starts with a clean allocator
     * automatically — no host-side global registry to dangle, no
     * mimalloc, no per-ctx cleanup needed. Lazy-inited on first
     * malloc by impl/alloc.c.
     *
     *   alloc_lo, alloc_hi        — wasm-offset bounds of the heap
     *                               region. Zero = not initialised.
     *   alloc_free_head           — wasm offset of the first free
     *                               block, or 0 if the list is empty.
     */
    uint32_t alloc_lo;
    uint32_t alloc_hi;
    uint32_t alloc_free_head;

    /* Per-process signal state (impl/sig.c).
     *
     *   sig_handlers[signum]  — wasm-side function-table index of the
     *                           registered handler (FreeBSD signums 1..31;
     *                           [0] unused). 0 means SIG_DFL (no handler).
     *                           Real handler indices are 1..table_size-1
     *                           in the wasm function table.
     *   sig_ignore_mask       — separate from sig_handlers[]: bit
     *                           (signo-1) set ⇔ signal is SIG_IGN.
     *                           Without this, the FreeBSD ABI's
     *                           `sa_handler = SIG_IGN = (void*)1` would
     *                           collide with wasm function-table index
     *                           1 (where clang's linker often places the
     *                           first user-defined function). Splitting
     *                           the SIG_IGN sentinel into its own bit
     *                           lets ordinary handler indices live at
     *                           any table position.
     *   sig_mask              — bitmask of blocked FreeBSD signals
     *                           (bit (signo-1)). Read/written by
     *                           sigprocmask, consulted by signal_pump
     *                           before invoking each handler.
     *   sig_pending           — bits set when a signal arrives while
     *                           that signal is blocked, OR when kill()
     *                           targets this process. Cleared by
     *                           signal_pump as each is delivered.
     *
     * Inheritance: fork copies all four from parent. Execve preserves
     * sig_mask + sig_ignore_mask, resets handlers (custom → SIG_DFL;
     * SIG_IGN preserved), and clears sig_pending. */
    uint32_t sig_handlers[32];
    uint32_t sig_ignore_mask;
    uint32_t sig_mask;
    uint32_t sig_pending;
};

/* Global runtime state */
struct yos_runtime {
    struct yos_proc procs[YOS_MAX_PROCS];
    pthread_mutex_t proc_lock;
    /* Broadcast every time ANY proc transitions to ZOMBIE — main.c
     * waits on it during shutdown so yos doesn't exit while a
     * forked child is still running. Per-proc wait_cond is for
     * waitpid() consumers (one cond var per child); this one is
     * a runtime-wide "something exited" event. */
    pthread_cond_t  any_exit_cond;
    int32_t next_pid;

    /* Foreground process-group of the controlling tty, virtualized in
     * the guest namespace. TIOCGPGRP/TIOCSPGRP on a tty fd read/write
     * this instead of the host kernel's value (which would be the host
     * shell's pgrp, an unrelated number from the guest's perspective).
     * Initialized to 1 = init proc, so ash's foreground-pgrp loop
     * (compares getpgrp()==tcgetpgrp()) succeeds at startup. */
    int32_t fg_pgid;

    int argc;
    char **argv;
    int envc;
    char **envp;

    /* VFS mount table */
    void *mount_table;

    /* Lazy-allocated host-side L1 pthread implementation. Holds the per-process
     * thread-slot table + TLS arena pool. yos_link_imports() asks for it so
     * every loaded module gets the yos_pthread_* imports bound. Treated as
     * `struct yos_pthread_host *` by code that includes "impl/pthread.h"; kept
     * void here to avoid pulling that header into every translation unit. */
    void *pthread_host;
};

#endif /* YOS_TYPES_H */
