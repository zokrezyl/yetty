/*
 * yctl host daemon — see include/yos/yctl/yctl.h for the API + wire
 * format. msgpack-RPC v2 over AF_UNIX SOCK_STREAM; one detached host
 * pthread accepts connections and serves requests one at a time.
 *
 * Concurrency: every read of rt->procs[] is guarded by rt->proc_lock,
 * every read of a proc's per-proc fields by proc->lock. Cross-thread
 * atomic-only fields (sig_pending, …) use atomic loads. Snapshot
 * everything into stack-locals under the lock, drop the lock, then
 * encode msgpack into a buffer and write to the socket. No socket
 * I/O ever happens while a lock is held.
 */

#include "yos/yctl/yctl.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <msgpack.h>

#include "yos/types.h"
#include "impl/proc/proc.h"
#include "yos/ytrace/ytrace.h"
#include "yos/yperf/yperf.h"

/* Forward decl from ytrace.h's runtime control surface. */
extern void ytrace_set_all_enabled(bool enabled);

#define YCTL_VERSION_STRING "yctl 0.1"
#define YCTL_MAX_REQ_BYTES  (1u << 20)   /* 1 MiB — generous; requests
                                           are normally a few bytes */

struct yctl_server {
    struct yos_runtime *rt;
    int listen_fd;
    char sock_path[108];     /* sun_path max */
};

/* ── msgpack helpers ──────────────────────────────────────────────── */

static void pk_str(msgpack_packer *pk, const char *s)
{
    size_t n = strlen(s);
    msgpack_pack_str(pk, n);
    msgpack_pack_str_body(pk, s, n);
}

static void pk_kv_str(msgpack_packer *pk, const char *k, const char *v)
{
    pk_str(pk, k);
    pk_str(pk, v);
}

static void pk_kv_int(msgpack_packer *pk, const char *k, int64_t v)
{
    pk_str(pk, k);
    msgpack_pack_int64(pk, v);
}

static void pk_kv_u64(msgpack_packer *pk, const char *k, uint64_t v)
{
    pk_str(pk, k);
    msgpack_pack_uint64(pk, v);
}

/* Build a [1, msgid, error, result] response envelope.
 * If `err` is non-NULL the response carries the error string and a nil
 * result; otherwise the caller has already packed the `result` value
 * into `pk` *between* msgpack_pack_array(4) + msgid + error_marker and
 * the close. We use a two-buffer pattern instead — the caller packs
 * the result body into `body_sbuf`, we then prepend the envelope. */
static void pk_envelope_ok(msgpack_packer *pk, uint64_t msgid,
                            const char *result_blob, size_t result_len)
{
    msgpack_pack_array(pk, 4);
    msgpack_pack_uint8(pk, 1);            /* response type */
    msgpack_pack_uint64(pk, msgid);
    msgpack_pack_nil(pk);                 /* error: nil */
    /* result body — already packed by caller */
    msgpack_pack_str_body(pk, result_blob, result_len);
}

static void pack_err_response(msgpack_packer *pk, uint64_t msgid,
                              const char *errstr)
{
    msgpack_pack_array(pk, 4);
    msgpack_pack_uint8(pk, 1);
    msgpack_pack_uint64(pk, msgid);
    pk_str(pk, errstr);
    msgpack_pack_nil(pk);
}

/* Pull an int64 out of a msgpack object that's known to be an integer. */
static int obj_as_i64(const msgpack_object *o, int64_t *out)
{
    if (o->type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
        *out = (int64_t)o->via.u64; return 0;
    }
    if (o->type == MSGPACK_OBJECT_NEGATIVE_INTEGER) {
        *out = o->via.i64; return 0;
    }
    return -1;
}

static int obj_as_bool(const msgpack_object *o, bool *out)
{
    if (o->type == MSGPACK_OBJECT_BOOLEAN) {
        *out = o->via.boolean; return 0;
    }
    return -1;
}

static int obj_eq_str(const msgpack_object *o, const char *s)
{
    if (o->type != MSGPACK_OBJECT_STR) return 0;
    size_t n = strlen(s);
    return o->via.str.size == n && memcmp(o->via.str.ptr, s, n) == 0;
}

static int obj_str_dup(const msgpack_object *o, char *buf, size_t cap)
{
    if (o->type != MSGPACK_OBJECT_STR) return -1;
    size_t n = o->via.str.size;
    if (n + 1 > cap) n = cap - 1;
    memcpy(buf, o->via.str.ptr, n);
    buf[n] = 0;
    return 0;
}

/* ── snapshot structs (filled under locks, then released for encoding) ── */

struct proc_summary {
    int32_t pid;
    int32_t ppid;
    int32_t state;
    char    comm[16];
};

struct proc_detail {
    int32_t pid, ppid, pgid, sid, tgid;
    int32_t state;
    int32_t exit_code;
    char    comm[16];
    char    exe[256];        /* truncated copy — PATH_MAX is generous */
    char    cwd[256];
    /* cmdline: up to 32 args, each up to 256 chars */
    int     cmdline_argc;
    char    cmdline[32][256];
};

struct mem_info {
    uint32_t memory_size;
    uint32_t heap_end;
    uint32_t mmap_top;
    int      free_count;
    struct yos_free_region free_list[YOS_MAX_FREE_REGIONS];
};

struct fd_pair { int wfd; int hfd; };

struct fd_info {
    int      count;
    struct fd_pair pairs[YOS_FD_MAX];
};

struct sig_info {
    uint32_t mask;
    uint32_t pending;
    uint32_t handlers[32];
};

/* ── method implementations ──────────────────────────────────────── */

/* version — no lock needed. */
static void m_version(struct yctl_server *s, msgpack_packer *pk,
                      uint64_t msgid, const msgpack_object_array *params)
{
    (void)s; (void)params;
    msgpack_pack_array(pk, 4);
    msgpack_pack_uint8(pk, 1);
    msgpack_pack_uint64(pk, msgid);
    msgpack_pack_nil(pk);
    pk_str(pk, YCTL_VERSION_STRING);
}

/* proc.list — snapshot every non-FREE slot under proc_lock. */
static void m_proc_list(struct yctl_server *s, msgpack_packer *pk,
                        uint64_t msgid, const msgpack_object_array *params)
{
    (void)params;
    struct proc_summary out[YOS_MAX_PROCS];
    int n = 0;
    pthread_mutex_lock(&s->rt->proc_lock);
    for (int i = 0; i < YOS_MAX_PROCS; i++) {
        struct yos_proc *p = &s->rt->procs[i];
        if (p->state == YOS_PROC_FREE) continue;
        out[n].pid   = p->pid;
        out[n].ppid  = p->ppid;
        out[n].state = (int32_t)p->state;
        memcpy(out[n].comm, p->comm, sizeof out[n].comm);
        out[n].comm[sizeof(out[n].comm) - 1] = 0;
        n++;
    }
    pthread_mutex_unlock(&s->rt->proc_lock);

    msgpack_pack_array(pk, 4);
    msgpack_pack_uint8(pk, 1);
    msgpack_pack_uint64(pk, msgid);
    msgpack_pack_nil(pk);
    msgpack_pack_array(pk, n);
    for (int i = 0; i < n; i++) {
        msgpack_pack_map(pk, 4);
        pk_kv_int(pk, "pid",   out[i].pid);
        pk_kv_int(pk, "ppid",  out[i].ppid);
        pk_kv_int(pk, "state", out[i].state);
        pk_kv_str(pk, "comm",  out[i].comm);
    }
}

/* Snapshot per-proc detail under proc->lock. */
static int snapshot_detail(struct yos_runtime *rt, int32_t pid,
                           struct proc_detail *d)
{
    pthread_mutex_lock(&rt->proc_lock);
    struct yos_proc *p = NULL;
    for (int i = 0; i < YOS_MAX_PROCS; i++) {
        if (rt->procs[i].state != YOS_PROC_FREE && rt->procs[i].pid == pid) {
            p = &rt->procs[i]; break;
        }
    }
    if (!p) {
        pthread_mutex_unlock(&rt->proc_lock);
        return -1;
    }
    /* Hold proc_lock while copying out fields. We don't take proc->lock
     * here because the proc table guarantees the slot is stable as long
     * as we hold proc_lock — the alloc/free transitions go through
     * proc_lock. Per-proc field mutations (comm, cmdline, state) can
     * race with us in principle but they're write-once-after-fork (or
     * rewritten on exec under the proc_lock that yos_execve takes
     * around set_proc_info), and we're reading a snapshot anyway. */
    d->pid       = p->pid;
    d->ppid      = p->ppid;
    d->pgid      = p->pgid;
    d->sid       = p->sid;
    d->tgid      = p->tgid;
    d->state     = (int32_t)p->state;
    d->exit_code = p->exit_code;
    memcpy(d->comm, p->comm, sizeof d->comm);
    d->comm[sizeof d->comm - 1] = 0;
    strncpy(d->exe, p->exe, sizeof d->exe - 1);
    d->exe[sizeof d->exe - 1] = 0;
    strncpy(d->cwd, p->cwd, sizeof d->cwd - 1);
    d->cwd[sizeof d->cwd - 1] = 0;
    int n = p->cmdline_argc;
    if (n > (int)(sizeof d->cmdline / sizeof d->cmdline[0]))
        n = (int)(sizeof d->cmdline / sizeof d->cmdline[0]);
    d->cmdline_argc = n;
    for (int i = 0; i < n; i++) {
        const char *a = (p->cmdline && p->cmdline[i]) ? p->cmdline[i] : "";
        strncpy(d->cmdline[i], a, sizeof d->cmdline[i] - 1);
        d->cmdline[i][sizeof d->cmdline[i] - 1] = 0;
    }
    pthread_mutex_unlock(&rt->proc_lock);
    return 0;
}

static void m_proc_get(struct yctl_server *s, msgpack_packer *pk,
                       uint64_t msgid, const msgpack_object_array *params)
{
    int64_t pid;
    if (params->size < 1 || obj_as_i64(&params->ptr[0], &pid) != 0) {
        pack_err_response(pk, msgid, "proc.get: missing/invalid pid");
        return;
    }
    struct proc_detail d;
    if (snapshot_detail(s->rt, (int32_t)pid, &d) != 0) {
        pack_err_response(pk, msgid, "proc.get: no such pid");
        return;
    }

    msgpack_pack_array(pk, 4);
    msgpack_pack_uint8(pk, 1);
    msgpack_pack_uint64(pk, msgid);
    msgpack_pack_nil(pk);
    msgpack_pack_map(pk, 10);
    pk_kv_int(pk, "pid",       d.pid);
    pk_kv_int(pk, "ppid",      d.ppid);
    pk_kv_int(pk, "pgid",      d.pgid);
    pk_kv_int(pk, "sid",       d.sid);
    pk_kv_int(pk, "tgid",      d.tgid);
    pk_kv_int(pk, "state",     d.state);
    pk_kv_int(pk, "exit_code", d.exit_code);
    pk_kv_str(pk, "comm",      d.comm);
    pk_kv_str(pk, "exe",       d.exe);
    pk_str(pk, "cmdline");
    msgpack_pack_array(pk, d.cmdline_argc);
    for (int i = 0; i < d.cmdline_argc; i++) pk_str(pk, d.cmdline[i]);
}

/* Snapshot ctx-side memory state. Read ctx through proc->ctx_handle
 * under proc_lock — the ctx is freed only after its proc exits, and
 * we hold proc_lock so the proc can't transition FREE under us. */
static int snapshot_mem(struct yos_runtime *rt, int32_t pid, struct mem_info *m)
{
    pthread_mutex_lock(&rt->proc_lock);
    struct yos_proc *p = NULL;
    for (int i = 0; i < YOS_MAX_PROCS; i++) {
        if (rt->procs[i].state != YOS_PROC_FREE && rt->procs[i].pid == pid) {
            p = &rt->procs[i]; break;
        }
    }
    if (!p || !p->ctx_handle) {
        pthread_mutex_unlock(&rt->proc_lock);
        return -1;
    }
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)p->ctx_handle;
    /* mem_lock guards free_list/free_count on the ctx. */
    pthread_mutex_lock(&ctx->mem_lock);
    m->memory_size = ctx->memory_size;
    m->heap_end    = ctx->heap_end;
    m->mmap_top    = ctx->mmap_top;
    m->free_count  = ctx->free_count;
    int n = ctx->free_count;
    if (n > YOS_MAX_FREE_REGIONS) n = YOS_MAX_FREE_REGIONS;
    for (int i = 0; i < n; i++) m->free_list[i] = ctx->free_list[i];
    pthread_mutex_unlock(&ctx->mem_lock);
    pthread_mutex_unlock(&rt->proc_lock);
    return 0;
}

static void m_mem_regions(struct yctl_server *s, msgpack_packer *pk,
                          uint64_t msgid, const msgpack_object_array *params)
{
    int64_t pid;
    if (params->size < 1 || obj_as_i64(&params->ptr[0], &pid) != 0) {
        pack_err_response(pk, msgid, "mem.regions: missing/invalid pid");
        return;
    }
    struct mem_info m;
    if (snapshot_mem(s->rt, (int32_t)pid, &m) != 0) {
        pack_err_response(pk, msgid, "mem.regions: no such pid or no ctx");
        return;
    }

    msgpack_pack_array(pk, 4);
    msgpack_pack_uint8(pk, 1);
    msgpack_pack_uint64(pk, msgid);
    msgpack_pack_nil(pk);
    msgpack_pack_map(pk, 4);
    pk_kv_u64(pk, "memory_size", m.memory_size);
    pk_kv_u64(pk, "heap_end",    m.heap_end);
    pk_kv_u64(pk, "mmap_top",    m.mmap_top);
    pk_str(pk, "free");
    int n = m.free_count;
    if (n > YOS_MAX_FREE_REGIONS) n = YOS_MAX_FREE_REGIONS;
    msgpack_pack_array(pk, n);
    for (int i = 0; i < n; i++) {
        msgpack_pack_map(pk, 2);
        pk_kv_u64(pk, "addr", m.free_list[i].addr);
        pk_kv_u64(pk, "len",  m.free_list[i].len);
    }
}

/* fd.table — no per-ctx lock on fd_map; the slot is owned by the
 * proc's own thread. Cross-thread reads of the slot are racy in
 * principle (the owning thread could be opening/closing fds), but
 * each int slot is naturally atomic on every platform we target. We
 * read the snapshot under proc_lock so the ctx pointer itself is
 * stable. */
static int snapshot_fds(struct yos_runtime *rt, int32_t pid, struct fd_info *f)
{
    pthread_mutex_lock(&rt->proc_lock);
    struct yos_proc *p = NULL;
    for (int i = 0; i < YOS_MAX_PROCS; i++) {
        if (rt->procs[i].state != YOS_PROC_FREE && rt->procs[i].pid == pid) {
            p = &rt->procs[i]; break;
        }
    }
    if (!p || !p->ctx_handle) {
        pthread_mutex_unlock(&rt->proc_lock);
        return -1;
    }
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)p->ctx_handle;
    f->count = 0;
    for (int wfd = 0; wfd < YOS_FD_MAX; wfd++) {
        int hfd = __atomic_load_n(&ctx->fd_map[wfd], __ATOMIC_ACQUIRE);
        if (hfd < 0) continue;
        f->pairs[f->count].wfd = wfd;
        f->pairs[f->count].hfd = hfd;
        f->count++;
    }
    pthread_mutex_unlock(&rt->proc_lock);
    return 0;
}

static void m_fd_table(struct yctl_server *s, msgpack_packer *pk,
                       uint64_t msgid, const msgpack_object_array *params)
{
    int64_t pid;
    if (params->size < 1 || obj_as_i64(&params->ptr[0], &pid) != 0) {
        pack_err_response(pk, msgid, "fd.table: missing/invalid pid");
        return;
    }
    struct fd_info f;
    if (snapshot_fds(s->rt, (int32_t)pid, &f) != 0) {
        pack_err_response(pk, msgid, "fd.table: no such pid or no ctx");
        return;
    }
    msgpack_pack_array(pk, 4);
    msgpack_pack_uint8(pk, 1);
    msgpack_pack_uint64(pk, msgid);
    msgpack_pack_nil(pk);
    msgpack_pack_array(pk, f.count);
    for (int i = 0; i < f.count; i++) {
        msgpack_pack_map(pk, 2);
        pk_kv_int(pk, "wfd", f.pairs[i].wfd);
        pk_kv_int(pk, "hfd", f.pairs[i].hfd);
    }
}

static int snapshot_sig(struct yos_runtime *rt, int32_t pid, struct sig_info *si)
{
    pthread_mutex_lock(&rt->proc_lock);
    struct yos_proc *p = NULL;
    for (int i = 0; i < YOS_MAX_PROCS; i++) {
        if (rt->procs[i].state != YOS_PROC_FREE && rt->procs[i].pid == pid) {
            p = &rt->procs[i]; break;
        }
    }
    if (!p || !p->ctx_handle) {
        pthread_mutex_unlock(&rt->proc_lock);
        return -1;
    }
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)p->ctx_handle;
    /* sig_pending is documented as cross-thread atomic. sig_mask and
     * sig_handlers[] mutate under the owning thread's sigaction/
     * sigprocmask paths, which are infrequent — a relaxed-load snapshot
     * is fine for an introspection view. */
    si->mask    = __atomic_load_n(&ctx->sig_mask,    __ATOMIC_ACQUIRE);
    si->pending = __atomic_load_n(&ctx->sig_pending, __ATOMIC_ACQUIRE);
    for (int i = 0; i < 32; i++)
        si->handlers[i] = __atomic_load_n(&ctx->sig_handlers[i], __ATOMIC_RELAXED);
    pthread_mutex_unlock(&rt->proc_lock);
    return 0;
}

static void m_sig_state(struct yctl_server *s, msgpack_packer *pk,
                        uint64_t msgid, const msgpack_object_array *params)
{
    int64_t pid;
    if (params->size < 1 || obj_as_i64(&params->ptr[0], &pid) != 0) {
        pack_err_response(pk, msgid, "sig.state: missing/invalid pid");
        return;
    }
    struct sig_info si;
    if (snapshot_sig(s->rt, (int32_t)pid, &si) != 0) {
        pack_err_response(pk, msgid, "sig.state: no such pid or no ctx");
        return;
    }
    msgpack_pack_array(pk, 4);
    msgpack_pack_uint8(pk, 1);
    msgpack_pack_uint64(pk, msgid);
    msgpack_pack_nil(pk);
    msgpack_pack_map(pk, 3);
    pk_kv_u64(pk, "mask",    si.mask);
    pk_kv_u64(pk, "pending", si.pending);
    pk_str(pk, "handlers");
    msgpack_pack_array(pk, 32);
    for (int i = 0; i < 32; i++) msgpack_pack_uint32(pk, si.handlers[i]);
}

static void m_proc_kill(struct yctl_server *s, msgpack_packer *pk,
                        uint64_t msgid, const msgpack_object_array *params)
{
    int64_t pid, sig;
    if (params->size < 2
        || obj_as_i64(&params->ptr[0], &pid) != 0
        || obj_as_i64(&params->ptr[1], &sig) != 0) {
        pack_err_response(pk, msgid, "proc.kill: need [pid, sig]");
        return;
    }
    int rc = yos_proc_kill_by_pid(s->rt, (int32_t)pid, (int32_t)sig);
    if (rc < 0) {
        char buf[64];
        snprintf(buf, sizeof buf, "proc.kill: errno=%d", -rc);
        pack_err_response(pk, msgid, buf);
        return;
    }
    msgpack_pack_array(pk, 4);
    msgpack_pack_uint8(pk, 1);
    msgpack_pack_uint64(pk, msgid);
    msgpack_pack_nil(pk);
    msgpack_pack_nil(pk);
}

static void m_trace_set(struct yctl_server *s, msgpack_packer *pk,
                        uint64_t msgid, const msgpack_object_array *params)
{
    (void)s;
    bool on;
    if (params->size < 1 || obj_as_bool(&params->ptr[0], &on) != 0) {
        pack_err_response(pk, msgid, "trace.set: need [bool]");
        return;
    }
    ytrace_set_all_enabled(on);
    msgpack_pack_array(pk, 4);
    msgpack_pack_uint8(pk, 1);
    msgpack_pack_uint64(pk, msgid);
    msgpack_pack_nil(pk);
    msgpack_pack_nil(pk);
}

static void m_perf_set(struct yctl_server *s, msgpack_packer *pk,
                       uint64_t msgid, const msgpack_object_array *params)
{
    (void)s;
    if (params->size < 1 || params->ptr[0].type != MSGPACK_OBJECT_STR) {
        pack_err_response(pk, msgid, "perf.set: need [\"on\"|\"off\"|\"stop\"]");
        return;
    }
    if (obj_eq_str(&params->ptr[0], "on")) {
        yperf_set_enabled(true);
    } else if (obj_eq_str(&params->ptr[0], "off")) {
        yperf_set_enabled(false);
    } else if (obj_eq_str(&params->ptr[0], "stop")) {
        /* Equivalent of setenv("YPERF","stop") — dump + reset. */
        yperf_dump_and_reset();
    } else {
        pack_err_response(pk, msgid, "perf.set: bad mode");
        return;
    }
    msgpack_pack_array(pk, 4);
    msgpack_pack_uint8(pk, 1);
    msgpack_pack_uint64(pk, msgid);
    msgpack_pack_nil(pk);
    msgpack_pack_nil(pk);
}

/* ── dispatch ────────────────────────────────────────────────────── */

static const struct {
    const char *name;
    void (*fn)(struct yctl_server *, msgpack_packer *,
               uint64_t, const msgpack_object_array *);
} g_methods[] = {
    { "version",     m_version     },
    { "proc.list",   m_proc_list   },
    { "proc.get",    m_proc_get    },
    { "mem.regions", m_mem_regions },
    { "fd.table",    m_fd_table    },
    { "sig.state",   m_sig_state   },
    { "proc.kill",   m_proc_kill   },
    { "trace.set",   m_trace_set   },
    { "perf.set",    m_perf_set    },
};

static void dispatch(struct yctl_server *s, msgpack_packer *pk,
                     const msgpack_object *req)
{
    if (req->type != MSGPACK_OBJECT_ARRAY || req->via.array.size < 4) {
        pack_err_response(pk, 0, "bad request shape");
        return;
    }
    const msgpack_object_array *a = &req->via.array;
    int64_t type, msgid;
    if (obj_as_i64(&a->ptr[0], &type) != 0
        || obj_as_i64(&a->ptr[1], &msgid) != 0
        || type != 0
        || a->ptr[2].type != MSGPACK_OBJECT_STR
        || a->ptr[3].type != MSGPACK_OBJECT_ARRAY) {
        pack_err_response(pk, 0, "bad request envelope");
        return;
    }
    const msgpack_object_str *method = &a->ptr[2].via.str;
    for (size_t i = 0; i < sizeof g_methods / sizeof g_methods[0]; i++) {
        size_t nl = strlen(g_methods[i].name);
        if (method->size == nl && memcmp(method->ptr, g_methods[i].name, nl) == 0) {
            g_methods[i].fn(s, pk, (uint64_t)msgid, &a->ptr[3].via.array);
            return;
        }
    }
    pack_err_response(pk, (uint64_t)msgid, "unknown method");
}

/* ── connection loop ─────────────────────────────────────────────── */

static int write_all(int fd, const char *buf, size_t n)
{
    while (n > 0) {
        ssize_t k = write(fd, buf, n);
        if (k < 0) { if (errno == EINTR) continue; return -1; }
        buf += k; n -= (size_t)k;
    }
    return 0;
}

static void handle_client(struct yctl_server *s, int cfd)
{
    msgpack_unpacker unp;
    if (!msgpack_unpacker_init(&unp, 8192)) {
        ywarn("yctl: msgpack_unpacker_init failed\n");
        return;
    }

    for (;;) {
        if (msgpack_unpacker_buffer_capacity(&unp) < 1024) {
            if (!msgpack_unpacker_reserve_buffer(&unp, 4096)) break;
        }
        char *buf = msgpack_unpacker_buffer(&unp);
        size_t cap = msgpack_unpacker_buffer_capacity(&unp);
        ssize_t n = read(cfd, buf, cap);
        if (n == 0) break;                 /* client closed */
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (msgpack_unpacker_message_size(&unp) + (size_t)n > YCTL_MAX_REQ_BYTES) {
            ywarn("yctl: request too large; dropping connection\n");
            break;
        }
        msgpack_unpacker_buffer_consumed(&unp, (size_t)n);

        msgpack_unpacked req;
        msgpack_unpacked_init(&req);
        for (;;) {
            msgpack_unpack_return ur = msgpack_unpacker_next(&unp, &req);
            if (ur == MSGPACK_UNPACK_CONTINUE) break;
            if (ur != MSGPACK_UNPACK_SUCCESS) {
                ywarn("yctl: msgpack parse error %d\n", (int)ur);
                msgpack_unpacked_destroy(&req);
                goto done;
            }
            msgpack_sbuffer out_sbuf;
            msgpack_sbuffer_init(&out_sbuf);
            msgpack_packer pk;
            msgpack_packer_init(&pk, &out_sbuf, msgpack_sbuffer_write);
            dispatch(s, &pk, &req.data);
            if (write_all(cfd, out_sbuf.data, out_sbuf.size) != 0) {
                msgpack_sbuffer_destroy(&out_sbuf);
                msgpack_unpacked_destroy(&req);
                goto done;
            }
            msgpack_sbuffer_destroy(&out_sbuf);
        }
        msgpack_unpacked_destroy(&req);
    }
done:
    msgpack_unpacker_destroy(&unp);
}

static void *yctl_thread_main(void *arg)
{
    struct yctl_server *s = arg;

    /* Block all signals on this thread — yos's signal pump runs on
     * guest threads, and the accept loop should never absorb a signal. */
    sigset_t all;
    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, NULL);

    ywarn("yctl: listening on %s\n", s->sock_path);
    for (;;) {
        int cfd = accept(s->listen_fd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            ywarn("yctl: accept failed: %s\n", strerror(errno));
            break;
        }
        handle_client(s, cfd);
        close(cfd);
    }
    close(s->listen_fd);
    unlink(s->sock_path);
    free(s);
    return NULL;
}

/* The single instance pointer — yctl_start can only run once per
 * process; subsequent calls return -1 with errno=EEXIST. We hold the
 * pointer to keep the heap allocation alive for the thread; the
 * thread frees it on accept-loop exit (which happens only at shutdown
 * via close-on-exec / accept error). */
static _Atomic(struct yctl_server *) g_yctl = NULL;

int yctl_start(struct yos_runtime *rt, const char *sock_path)
{
    struct yctl_server *expected = NULL;
    struct yctl_server *s = calloc(1, sizeof *s);
    if (!s) return -1;

    s->rt = rt;
    snprintf(s->sock_path, sizeof s->sock_path, "%s", sock_path);

    s->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s->listen_fd < 0) { free(s); return -1; }

    /* Unlink any stale file from a previous run; ignore ENOENT. */
    (void)unlink(s->sock_path);

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, s->sock_path, sizeof sa.sun_path - 1);
    if (bind(s->listen_fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        close(s->listen_fd); free(s); return -1;
    }
    if (listen(s->listen_fd, 4) < 0) {
        close(s->listen_fd); unlink(s->sock_path); free(s); return -1;
    }

    if (!atomic_compare_exchange_strong(&g_yctl, &expected, s)) {
        close(s->listen_fd); unlink(s->sock_path); free(s);
        errno = EEXIST; return -1;
    }

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int rc = pthread_create(&tid, &attr, yctl_thread_main, s);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        atomic_store(&g_yctl, NULL);
        close(s->listen_fd); unlink(s->sock_path); free(s);
        errno = rc; return -1;
    }
    return 0;
}
