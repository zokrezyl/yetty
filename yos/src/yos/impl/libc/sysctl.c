/* impl/sysctl.c — minimal FreeBSD sysctl shim.
 *
 * The full FreeBSD sysctl namespace is huge and host-dependent. We
 * implement only the cases we know guests actually need; everything
 * else returns -ENOSYS. Today's set:
 *
 *   CTL_KERN.KERN_PROC.KERN_PROC_PATHNAME(-1)
 *     → path of the current "process". libuv's uv_exepath() uses
 *       this on FreeBSD-shaped builds. We return the wasm file
 *       path the host yos was invoked with (ctx->rt->argv[0]).
 *
 * Listed in hooks.yaml runtime_owned so bridge.py emits no body;
 * main.c calls yos_sysctl_link() to bind us.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

#include "wasm3.h"
#include "m3_env.h"
#include "yos/types.h"
#include <yos/ytrace/ytrace.h>

/* Wrap-safe range check: offset + len fits in [0, memory_size]. uint32+
 * uint32 addition wraps on overflow; 64-bit promotion catches that. */
static inline int range_ok(struct yos_exec_ctx *ctx, uint32_t off, uint64_t len)
{
    return (uint64_t)off + len <= (uint64_t)ctx->memory_size;
}

/* FreeBSD sysctl MIB constants (from sys/sysctl.h, sys/proc.h). */
#define CTL_KERN              1
#define KERN_PROC            14
#define KERN_PROC_ALL         0  /* everything */
#define KERN_PROC_PID         1  /* by process id */
#define KERN_PROC_ARGS        7  /* get/set arguments/proctitle */
#define KERN_PROC_PROC        8  /* only processes (no threads) */
#define KERN_PROC_PATHNAME   12

/* struct kinfo_proc, FreeBSD-i386 (wasm32 ABI). Total 768 bytes.
 * Layout extracted via tools/struct-offsets.py kinfo_proc — re-run
 * that if a future sysroot bump changes the FreeBSD definition. The
 * codegen pipeline doesn't see this struct (no libc fn signature
 * mentions it), so we hand-bake the offsets here. */
#define FBI_KP_SIZE              768
#define FBI_KP_OFF_STRUCTSIZE      0  /* int      — must equal 768 */
#define FBI_KP_OFF_PID            40  /* pid_t */
#define FBI_KP_OFF_PPID           44
#define FBI_KP_OFF_PGID           48
#define FBI_KP_OFF_TPGID          52
#define FBI_KP_OFF_SID            56
#define FBI_KP_OFF_TSID           60
#define FBI_KP_OFF_JOBC           64  /* short */
#define FBI_KP_OFF_UID           136  /* uid_t */
#define FBI_KP_OFF_RUID          140
#define FBI_KP_OFF_RUNTIME       272  /* u_int64_t — usec */
#define FBI_KP_OFF_START_SEC     280  /* struct timeval { time_t sec; suseconds_t usec; } */
#define FBI_KP_OFF_START_USEC    284
#define FBI_KP_OFF_FLAG          296  /* long */
#define FBI_KP_OFF_STAT          308  /* char */
#define FBI_KP_OFF_NICE          309  /* signed char */
#define FBI_KP_OFF_TDNAME        314  /* char[17] */
#define FBI_KP_OFF_COMM          367  /* char[20] */
#define FBI_KP_OFF_NUMTHREADS    516  /* int */
#define FBI_KP_OFF_TID           520  /* lwpid_t */

/* FreeBSD ki_stat values. */
#define FBI_SRUN    2
#define FBI_SZOMB   5

static int fbi_ki_stat(yos_proc_state_t s)
{
    /* Map yos's coarser states to FreeBSD's. ps(1) only cares about
     * SRUN/SSLEEP/SZOMB for the STAT column; we have no sleeping
     * state of our own, so READY/RUNNING both render as 'R'. */
    return (s == YOS_PROC_ZOMBIE) ? FBI_SZOMB : FBI_SRUN;
}

/* Fill one kinfo_proc record at `dst` (must be 768 bytes valid). */
static void fbi_fill_kinfo_proc(uint8_t *dst, const struct yos_proc *p)
{
    memset(dst, 0, FBI_KP_SIZE);
    *(int32_t  *)(dst + FBI_KP_OFF_STRUCTSIZE) = FBI_KP_SIZE;
    *(int32_t  *)(dst + FBI_KP_OFF_PID)        = p->pid;
    *(int32_t  *)(dst + FBI_KP_OFF_PPID)       = p->ppid;
    *(int32_t  *)(dst + FBI_KP_OFF_PGID)       = p->pgid;
    *(int32_t  *)(dst + FBI_KP_OFF_SID)        = p->sid;
    *(int32_t  *)(dst + FBI_KP_OFF_TID)        = p->pid;
    *(int32_t  *)(dst + FBI_KP_OFF_NUMTHREADS) = 1;
    *(uint32_t *)(dst + FBI_KP_OFF_UID)        = 0;  /* uid model TBD */
    *(uint32_t *)(dst + FBI_KP_OFF_RUID)       = 0;
    dst[FBI_KP_OFF_STAT] = (uint8_t)fbi_ki_stat(p->state);
    dst[FBI_KP_OFF_NICE] = 0;
    /* ki_comm[COMMLEN+1] = char[20]. p->comm is char[16], NUL-padded. */
    size_t n = strnlen(p->comm, sizeof p->comm);
    if (n > 19) n = 19;
    memcpy(dst + FBI_KP_OFF_COMM, p->comm, n);
    /* ki_tdname[TDNAMLEN+1] = char[17]. Mirror comm — yos has no
     * separate thread name yet. */
    size_t tn = (n > 16) ? 16 : n;
    memcpy(dst + FBI_KP_OFF_TDNAME, p->comm, tn);
}

/* Stream kinfo_proc records into the wasm-side buffer for the procs
 * matching `selector`. selector_arg is the pid for KERN_PROC_PID, 0
 * otherwise. Handles the FreeBSD size-query (oldp NULL or buflen too
 * small): writes required size to *oldlenp + returns ENOMEM.
 *
 * Iterates ctx->rt->procs[] under proc_lock so the snapshot is
 * consistent — no race with concurrent fork/exit. The full pass runs
 * in one bridge call (no incremental refill across getdents-style
 * resumes), which removes the dirent-resume drift the old /proc
 * impl had. */
static int32_t do_kern_proc_select(struct yos_exec_ctx *ctx,
                                   int selector, int selector_arg,
                                   uint32_t old_off, uint32_t oldlenp_off)
{
    if (!ctx || !ctx->rt) return -EINVAL;

    uint32_t buflen = 0;
    if (oldlenp_off) {
        if (!range_ok(ctx, oldlenp_off, 4)) return -EFAULT;
        buflen = *(uint32_t *)(ctx->memory + oldlenp_off);
    }

    pthread_mutex_lock(&ctx->rt->proc_lock);

    /* First pass: count matching records. */
    size_t count = 0;
    for (int i = 0; i < YOS_MAX_PROCS; i++) {
        const struct yos_proc *p = &ctx->rt->procs[i];
        if (p->state == YOS_PROC_FREE) continue;
        if (selector == KERN_PROC_PID && p->pid != selector_arg) continue;
        count++;
    }
    size_t need = count * FBI_KP_SIZE;

    if (old_off == 0 || buflen < need) {
        pthread_mutex_unlock(&ctx->rt->proc_lock);
        if (oldlenp_off)
            *(uint32_t *)(ctx->memory + oldlenp_off) = (uint32_t)need;
        if (old_off == 0) return 0;
        return -ENOMEM;
    }
    if (!range_ok(ctx, old_off, need)) {
        pthread_mutex_unlock(&ctx->rt->proc_lock);
        return -EFAULT;
    }

    /* Second pass: emit. */
    uint8_t *out = ctx->memory + old_off;
    for (int i = 0; i < YOS_MAX_PROCS; i++) {
        const struct yos_proc *p = &ctx->rt->procs[i];
        if (p->state == YOS_PROC_FREE) continue;
        if (selector == KERN_PROC_PID && p->pid != selector_arg) continue;
        fbi_fill_kinfo_proc(out, p);
        out += FBI_KP_SIZE;
    }
    pthread_mutex_unlock(&ctx->rt->proc_lock);

    if (oldlenp_off)
        *(uint32_t *)(ctx->memory + oldlenp_off) = (uint32_t)need;
    return 0;
}

/* KERN_PROC_ARGS — return the NUL-separated argv buffer for the given
 * PID. Caller can also write it (proctitle update); we accept-and-drop
 * because yos doesn't expose proctitle yet. */
static int32_t do_kern_proc_args(struct yos_exec_ctx *ctx, int pid,
                                 uint32_t old_off, uint32_t oldlenp_off,
                                 uint32_t newp_off, uint32_t newlen)
{
    (void)newp_off; (void)newlen;  /* TODO: implement proctitle write */
    if (!ctx || !ctx->rt) return -EINVAL;

    pthread_mutex_lock(&ctx->rt->proc_lock);
    const struct yos_proc *p = NULL;
    for (int i = 0; i < YOS_MAX_PROCS; i++) {
        if (ctx->rt->procs[i].state != YOS_PROC_FREE &&
            ctx->rt->procs[i].pid == pid) {
            p = &ctx->rt->procs[i];
            break;
        }
    }
    if (!p || !p->cmdline) {
        pthread_mutex_unlock(&ctx->rt->proc_lock);
        return -ESRCH;
    }

    /* Compute the NUL-separated buffer size. */
    size_t need = 0;
    for (int i = 0; i < p->cmdline_argc; i++)
        need += strlen(p->cmdline[i]) + 1;

    uint32_t buflen = 0;
    if (oldlenp_off) {
        if (!range_ok(ctx, oldlenp_off, 4)) {
            pthread_mutex_unlock(&ctx->rt->proc_lock);
            return -EFAULT;
        }
        buflen = *(uint32_t *)(ctx->memory + oldlenp_off);
    }

    if (old_off == 0 || buflen < need) {
        pthread_mutex_unlock(&ctx->rt->proc_lock);
        if (oldlenp_off)
            *(uint32_t *)(ctx->memory + oldlenp_off) = (uint32_t)need;
        if (old_off == 0) return 0;
        return -ENOMEM;
    }
    if (!range_ok(ctx, old_off, need)) {
        pthread_mutex_unlock(&ctx->rt->proc_lock);
        return -EFAULT;
    }

    uint8_t *out = ctx->memory + old_off;
    for (int i = 0; i < p->cmdline_argc; i++) {
        size_t n = strlen(p->cmdline[i]) + 1;
        memcpy(out, p->cmdline[i], n);
        out += n;
    }
    pthread_mutex_unlock(&ctx->rt->proc_lock);

    if (oldlenp_off)
        *(uint32_t *)(ctx->memory + oldlenp_off) = (uint32_t)need;
    return 0;
}

static int32_t do_kern_proc_pathname(struct yos_exec_ctx *ctx,
                                     uint32_t old_off, uint32_t oldlenp_off)
{
    /* Return the CURRENT process's executable, not yos's argv[0].
     *
     * `ctx->rt->argv` is the yos commandline shifted by 1 — i.e. the
     * very first guest's argv. It is NEVER updated across execve(),
     * so every fork+exec'd guest sees the original zsh/init path
     * here. libuv's uv_exepath() consults this via
     * sysctl(KERN_PROC_PATHNAME) and assumes "this is my own
     * executable"; uv_spawn then re-execs that path to launch a
     * helper (nvim's UI client, the :terminal shell, etc.). Result
     * before the fix: nvim's grandchild ended up exec'ing zsh with
     * argv = [nvim, --embed] and zsh complained "no such option:
     * embed".
     *
     * The per-proc `exe` field is set on every execve (impl/proc.c
     * child loop, main.c initial loop) and inherited on fork
     * (impl/proc.c yos_fork). Reading it here gives every guest
     * its own correct self-path. */
    const char *path = NULL;
    if (ctx->proc && ctx->proc->exe[0]) path = ctx->proc->exe;
    else if (ctx->rt && ctx->rt->argv && ctx->rt->argv[0])
        path = ctx->rt->argv[0];
    if (!path) return -ENOENT;
    size_t need = strlen(path) + 1;

    /* Read caller's old buffer length (oldlenp is a uint32_t* in
     * wasm32). NULL oldlenp => caller wants the size only via the
     * call's normal return path; FreeBSD documents ENOMEM in that
     * scenario. We treat NULL as "no buffer" too. */
    uint32_t buflen = 0;
    if (oldlenp_off) {
        if (!range_ok(ctx, oldlenp_off, 4)) return -EFAULT;
        buflen = *(uint32_t *)(ctx->memory + oldlenp_off);
    }

    if (old_off == 0) {
        /* Caller is asking for the size only. */
        if (oldlenp_off)
            *(uint32_t *)(ctx->memory + oldlenp_off) = (uint32_t)need;
        return 0;
    }

    if (buflen < need) {
        if (oldlenp_off)
            *(uint32_t *)(ctx->memory + oldlenp_off) = (uint32_t)need;
        return -ENOMEM;
    }
    if (!range_ok(ctx, old_off, need)) return -EFAULT;
    memcpy(ctx->memory + old_off, path, need);
    if (oldlenp_off)
        *(uint32_t *)(ctx->memory + oldlenp_off) = (uint32_t)need;
    return 0;
}

/* sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
 *        const void *newp, size_t newlen) */
static m3ApiRawFunction(m3_yos_sysctl)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, name_off);
    m3ApiGetArg(uint32_t, namelen);
    m3ApiGetArg(uint32_t, oldp_off);
    m3ApiGetArg(uint32_t, oldlenp_off);
    m3ApiGetArg(uint32_t, newp_off);
    m3ApiGetArg(uint32_t, newlen);
    (void)newp_off; (void)newlen;

    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    uint32_t mem_size = 0;
    ctx->memory = m3_GetMemory(runtime, &mem_size, 0);
    ctx->memory_size = mem_size;

    /* 64-bit math so `4 * namelen` and the sum can't wrap uint32. */
    if (namelen < 2 ||
        (uint64_t)name_off + 4ULL * (uint64_t)namelen > (uint64_t)mem_size)
        m3ApiReturn(-EINVAL);
    int32_t *mib = (int32_t *)(ctx->memory + name_off);

    if (mib[0] == CTL_KERN && mib[1] == KERN_PROC && namelen >= 3) {
        switch (mib[2]) {
        case KERN_PROC_PATHNAME:
            ydebug("sysctl(KERN_PROC_PATHNAME)\n");
            m3ApiReturn(do_kern_proc_pathname(ctx, oldp_off, oldlenp_off));
        case KERN_PROC_ALL:
        case KERN_PROC_PROC: {
            int arg = (namelen >= 4) ? mib[3] : 0;
            (void)arg;  /* PROC_ALL/PROC ignore the arg slot */
            ydebug("sysctl(KERN_PROC_%s)\n",
                   mib[2] == KERN_PROC_ALL ? "ALL" : "PROC");
            m3ApiReturn(do_kern_proc_select(ctx, mib[2], 0,
                                            oldp_off, oldlenp_off));
        }
        case KERN_PROC_PID: {
            if (namelen < 4) m3ApiReturn(-EINVAL);
            ydebug("sysctl(KERN_PROC_PID, %d)\n", mib[3]);
            m3ApiReturn(do_kern_proc_select(ctx, KERN_PROC_PID, mib[3],
                                            oldp_off, oldlenp_off));
        }
        case KERN_PROC_ARGS: {
            if (namelen < 4) m3ApiReturn(-EINVAL);
            ydebug("sysctl(KERN_PROC_ARGS, %d)\n", mib[3]);
            m3ApiReturn(do_kern_proc_args(ctx, mib[3], oldp_off,
                                          oldlenp_off, newp_off, newlen));
        }
        default:
            break;
        }
    }

    ydebug("sysctl: unhandled mib[%u] = {%d, %d, %d, ...}\n",
           namelen, mib[0], namelen > 1 ? mib[1] : 0,
           namelen > 2 ? mib[2] : 0);
    m3ApiReturn(-ENOSYS);
}

/* sysctlbyname(const char *name, …) — same idea, name lookup form. */
static m3ApiRawFunction(m3_yos_sysctlbyname)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, name_off);
    m3ApiGetArg(uint32_t, oldp_off);
    m3ApiGetArg(uint32_t, oldlenp_off);
    m3ApiGetArg(uint32_t, newp_off);
    m3ApiGetArg(uint32_t, newlen);
    (void)oldp_off; (void)oldlenp_off; (void)newp_off; (void)newlen;

    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    uint32_t mem_size = 0;
    ctx->memory = m3_GetMemory(runtime, &mem_size, 0);
    ctx->memory_size = mem_size;

    const char *name = (name_off && name_off < mem_size)
                     ? (const char *)(ctx->memory + name_off) : "?";
    ydebug("sysctlbyname(\"%s\") -> ENOSYS\n", name);
    m3ApiReturn(-ENOSYS);
}

void yos_sysctl_link(IM3Module mod)
{
    m3_LinkRawFunction(mod, "env", "sysctl",       "i(iiiiii)", m3_yos_sysctl);
    m3_LinkRawFunction(mod, "env", "sysctlbyname", "i(iiiii)",  m3_yos_sysctlbyname);
}
