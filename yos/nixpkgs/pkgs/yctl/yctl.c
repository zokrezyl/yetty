/*
 * yctl — client for the yos introspection + control daemon.
 *
 * Same source compiles twice:
 *   - host:  pkgs.msgpack-c via pkg-config (system glibc + libc)
 *   - wasm:  ../yctl/...  --sysroot=<sysroot> -lc -lyos_stubs +
 *            msgpack-c built for wasm32, see default.nix
 *
 * The default socket path matches yos's `--yctl-socket` default — see
 * the umbrella's bin/yos-shell wrapper for how this is set up in the
 * sandbox.  Override with `-s PATH`.
 *
 * Wire: msgpack-RPC v2 over AF_UNIX SOCK_STREAM. One request per
 * invocation, one response, exit. Output is plain text — column-aligned
 * for the list view, key:value blocks for the per-pid views.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <msgpack.h>

#define YCTL_DEFAULT_SOCK "/tmp/yos.sock"

static void usage(void)
{
    fprintf(stderr,
        "yctl - control/observability client for the yos runtime.\n"
        "usage: yctl [-s SOCK] <subcommand> [args...]\n"
        "\n"
        "  -s SOCK            unix socket path (default " YCTL_DEFAULT_SOCK ")\n"
        "\n"
        "Subcommands:\n"
        "  version            show daemon version\n"
        "  list               list every live guest process\n"
        "  get   <pid>        show full proc detail\n"
        "  mem   <pid>        show memory layout + free regions\n"
        "  fd    <pid>        show fd table (wasm fd -> host fd)\n"
        "  sig   <pid>        show signal mask/pending/handlers\n"
        "  kill  <pid> <sig>  send signal\n"
        "  trace on|off       toggle ytrace points\n"
        "  perf  on|off|stop  toggle yperf (stop = dump+reset)\n"
        );
}

/* ── socket plumbing ──────────────────────────────────────────────── */

static int connect_sock(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path, sizeof sa.sun_path - 1);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        fprintf(stderr, "yctl: connect %s: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static int write_all(int fd, const char *buf, size_t n)
{
    while (n > 0) {
        ssize_t k = write(fd, buf, n);
        if (k < 0) { if (errno == EINTR) continue; return -1; }
        buf += k; n -= (size_t)k;
    }
    return 0;
}

/* ── request builder ──────────────────────────────────────────────── */

/* Build [0, msgid, method, params] into sbuf. params_packer fills the
 * params array body — caller provides element count + a small function
 * that packs each value. */
typedef void (*params_fn)(msgpack_packer *pk, void *user);

static void build_request(msgpack_sbuffer *sbuf, uint64_t msgid,
                          const char *method, size_t n_params,
                          params_fn fn, void *user)
{
    msgpack_packer pk;
    msgpack_packer_init(&pk, sbuf, msgpack_sbuffer_write);
    msgpack_pack_array(&pk, 4);
    msgpack_pack_uint8(&pk, 0);
    msgpack_pack_uint64(&pk, msgid);
    size_t mlen = strlen(method);
    msgpack_pack_str(&pk, mlen);
    msgpack_pack_str_body(&pk, method, mlen);
    msgpack_pack_array(&pk, n_params);
    if (fn) fn(&pk, user);
}

/* ── response handling ────────────────────────────────────────────── */

static int read_response(int fd, msgpack_unpacked *out)
{
    msgpack_unpacker unp;
    if (!msgpack_unpacker_init(&unp, 8192)) return -1;
    for (;;) {
        if (msgpack_unpacker_buffer_capacity(&unp) < 1024) {
            if (!msgpack_unpacker_reserve_buffer(&unp, 4096)) {
                msgpack_unpacker_destroy(&unp); return -1;
            }
        }
        char *buf = msgpack_unpacker_buffer(&unp);
        size_t cap = msgpack_unpacker_buffer_capacity(&unp);
        ssize_t n = read(fd, buf, cap);
        if (n == 0) { msgpack_unpacker_destroy(&unp); return -1; }
        if (n < 0) { if (errno == EINTR) continue;
                     msgpack_unpacker_destroy(&unp); return -1; }
        msgpack_unpacker_buffer_consumed(&unp, (size_t)n);
        msgpack_unpacked_init(out);
        msgpack_unpack_return r = msgpack_unpacker_next(&unp, out);
        if (r == MSGPACK_UNPACK_SUCCESS) {
            msgpack_unpacker_destroy(&unp);
            return 0;
        }
        if (r != MSGPACK_UNPACK_CONTINUE) {
            msgpack_unpacked_destroy(out);
            msgpack_unpacker_destroy(&unp);
            return -1;
        }
    }
}

/* Unwrap [1, msgid, error, result] envelope.
 *   On success returns 0 and sets *result to the `result` field.
 *   On RPC error returns 1 and prints the error string to stderr.
 *   On parse error returns -1.
 * `result` aliases into `*resp` — caller must keep `resp` alive. */
static int unwrap(msgpack_unpacked *resp, msgpack_object **result)
{
    msgpack_object *o = &resp->data;
    if (o->type != MSGPACK_OBJECT_ARRAY || o->via.array.size < 4) {
        fprintf(stderr, "yctl: malformed response\n");
        return -1;
    }
    msgpack_object *e = &o->via.array.ptr[2];
    if (e->type == MSGPACK_OBJECT_STR) {
        fprintf(stderr, "yctl: server error: %.*s\n",
                (int)e->via.str.size, e->via.str.ptr);
        return 1;
    }
    *result = &o->via.array.ptr[3];
    return 0;
}

/* ── round-trip helper ────────────────────────────────────────────── */

static int rpc_call(const char *sock_path, const char *method,
                    size_t n_params, params_fn fn, void *user,
                    msgpack_unpacked *resp)
{
    int fd = connect_sock(sock_path);
    if (fd < 0) return -1;

    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);
    build_request(&sbuf, 1, method, n_params, fn, user);
    int rc = write_all(fd, sbuf.data, sbuf.size);
    msgpack_sbuffer_destroy(&sbuf);
    if (rc != 0) { close(fd); return -1; }

    rc = read_response(fd, resp);
    close(fd);
    return rc;
}

/* ── printers ─────────────────────────────────────────────────────── */

static const char *state_name(int64_t s)
{
    switch (s) {
    case 0: return "FREE";
    case 1: return "READY";
    case 2: return "RUNNING";
    case 3: return "ZOMBIE";
    default: return "?";
    }
}

static int find_map_key(const msgpack_object *m, const char *key,
                         msgpack_object **out)
{
    if (m->type != MSGPACK_OBJECT_MAP) return -1;
    size_t klen = strlen(key);
    for (uint32_t i = 0; i < m->via.map.size; i++) {
        msgpack_object_kv *kv = &m->via.map.ptr[i];
        if (kv->key.type == MSGPACK_OBJECT_STR
            && kv->key.via.str.size == klen
            && memcmp(kv->key.via.str.ptr, key, klen) == 0) {
            *out = &kv->val;
            return 0;
        }
    }
    return -1;
}

static int64_t obj_i64(const msgpack_object *o)
{
    if (o->type == MSGPACK_OBJECT_POSITIVE_INTEGER) return (int64_t)o->via.u64;
    if (o->type == MSGPACK_OBJECT_NEGATIVE_INTEGER) return o->via.i64;
    return 0;
}

static void print_str(const msgpack_object *o)
{
    if (o->type == MSGPACK_OBJECT_STR)
        fwrite(o->via.str.ptr, 1, o->via.str.size, stdout);
}

static void print_list(const msgpack_object *r)
{
    if (r->type != MSGPACK_OBJECT_ARRAY) {
        fprintf(stderr, "yctl: list: unexpected result\n"); return;
    }
    printf("%-6s %-6s %-8s %s\n", "PID", "PPID", "STATE", "COMM");
    for (uint32_t i = 0; i < r->via.array.size; i++) {
        msgpack_object *p = &r->via.array.ptr[i];
        msgpack_object *pid = NULL, *ppid = NULL, *state = NULL, *comm = NULL;
        find_map_key(p, "pid",   &pid);
        find_map_key(p, "ppid",  &ppid);
        find_map_key(p, "state", &state);
        find_map_key(p, "comm",  &comm);
        printf("%-6" PRId64 " %-6" PRId64 " %-8s ",
               pid ? obj_i64(pid) : -1,
               ppid ? obj_i64(ppid) : -1,
               state ? state_name(obj_i64(state)) : "?");
        if (comm) print_str(comm);
        putchar('\n');
    }
}

static void print_get(const msgpack_object *r)
{
    msgpack_object *o;
    if (find_map_key(r, "pid",   &o) == 0) printf("pid:       %" PRId64 "\n", obj_i64(o));
    if (find_map_key(r, "ppid",  &o) == 0) printf("ppid:      %" PRId64 "\n", obj_i64(o));
    if (find_map_key(r, "pgid",  &o) == 0) printf("pgid:      %" PRId64 "\n", obj_i64(o));
    if (find_map_key(r, "sid",   &o) == 0) printf("sid:       %" PRId64 "\n", obj_i64(o));
    if (find_map_key(r, "tgid",  &o) == 0) printf("tgid:      %" PRId64 "\n", obj_i64(o));
    if (find_map_key(r, "state", &o) == 0)
        printf("state:     %s (%" PRId64 ")\n", state_name(obj_i64(o)), obj_i64(o));
    if (find_map_key(r, "exit_code", &o) == 0) printf("exit_code: %" PRId64 "\n", obj_i64(o));
    if (find_map_key(r, "comm",  &o) == 0) { printf("comm:      "); print_str(o); putchar('\n'); }
    if (find_map_key(r, "exe",   &o) == 0) { printf("exe:       "); print_str(o); putchar('\n'); }
    if (find_map_key(r, "cmdline", &o) == 0 && o->type == MSGPACK_OBJECT_ARRAY) {
        printf("cmdline:  ");
        for (uint32_t i = 0; i < o->via.array.size; i++) {
            putchar(' '); print_str(&o->via.array.ptr[i]);
        }
        putchar('\n');
    }
}

static void print_mem(const msgpack_object *r)
{
    msgpack_object *o;
    if (find_map_key(r, "memory_size", &o) == 0)
        printf("memory_size: %" PRId64 "\n", obj_i64(o));
    if (find_map_key(r, "heap_end", &o) == 0)
        printf("heap_end:    0x%" PRIx64 "\n", obj_i64(o));
    if (find_map_key(r, "mmap_top", &o) == 0)
        printf("mmap_top:    0x%" PRIx64 "\n", obj_i64(o));
    if (find_map_key(r, "free", &o) == 0 && o->type == MSGPACK_OBJECT_ARRAY) {
        printf("free regions (%u):\n", (unsigned)o->via.array.size);
        for (uint32_t i = 0; i < o->via.array.size; i++) {
            msgpack_object *e = &o->via.array.ptr[i];
            msgpack_object *addr = NULL, *len = NULL;
            find_map_key(e, "addr", &addr);
            find_map_key(e, "len",  &len);
            printf("  0x%08" PRIx64 "  %" PRId64 " B\n",
                   addr ? obj_i64(addr) : 0,
                   len  ? obj_i64(len)  : 0);
        }
    }
}

static void print_fd(const msgpack_object *r)
{
    if (r->type != MSGPACK_OBJECT_ARRAY) return;
    printf("%-6s %s\n", "WFD", "HFD");
    for (uint32_t i = 0; i < r->via.array.size; i++) {
        msgpack_object *e = &r->via.array.ptr[i];
        msgpack_object *w = NULL, *h = NULL;
        find_map_key(e, "wfd", &w);
        find_map_key(e, "hfd", &h);
        printf("%-6" PRId64 " %" PRId64 "\n",
               w ? obj_i64(w) : -1, h ? obj_i64(h) : -1);
    }
}

static void print_sig(const msgpack_object *r)
{
    msgpack_object *o;
    if (find_map_key(r, "mask", &o) == 0)
        printf("mask:    0x%08" PRIx64 "\n", obj_i64(o));
    if (find_map_key(r, "pending", &o) == 0)
        printf("pending: 0x%08" PRIx64 "\n", obj_i64(o));
    if (find_map_key(r, "handlers", &o) == 0 && o->type == MSGPACK_OBJECT_ARRAY) {
        printf("handlers (signo: wasm-table-idx):\n");
        for (uint32_t i = 0; i < o->via.array.size; i++) {
            int64_t v = obj_i64(&o->via.array.ptr[i]);
            if (v == 0) continue;     /* SIG_DFL — skip the noise */
            printf("  %2u: 0x%" PRIx64 "\n", (unsigned)i, v);
        }
    }
}

/* ── params packers ───────────────────────────────────────────────── */

struct pid_arg   { int64_t pid; };
struct kill_arg  { int64_t pid, sig; };
struct bool_arg  { bool v; };
struct str_arg   { const char *s; };

static void pp_pid(msgpack_packer *pk, void *u) {
    struct pid_arg *a = u; msgpack_pack_int64(pk, a->pid);
}
static void pp_kill(msgpack_packer *pk, void *u) {
    struct kill_arg *a = u;
    msgpack_pack_int64(pk, a->pid);
    msgpack_pack_int64(pk, a->sig);
}
static void pp_bool(msgpack_packer *pk, void *u) {
    struct bool_arg *a = u;
    if (a->v) msgpack_pack_true(pk); else msgpack_pack_false(pk);
}
static void pp_str(msgpack_packer *pk, void *u) {
    struct str_arg *a = u;
    size_t n = strlen(a->s);
    msgpack_pack_str(pk, n);
    msgpack_pack_str_body(pk, a->s, n);
}

/* ── subcommand dispatch ─────────────────────────────────────────── */

static int cmd_simple(const char *sock, const char *method,
                      void (*printer)(const msgpack_object *))
{
    msgpack_unpacked resp;
    int rc = rpc_call(sock, method, 0, NULL, NULL, &resp);
    if (rc != 0) return 1;
    msgpack_object *result = NULL;
    int u = unwrap(&resp, &result);
    if (u == 0 && printer) printer(result);
    else if (u == 0) print_str(result);
    msgpack_unpacked_destroy(&resp);
    return u != 0;
}

static int cmd_pid(const char *sock, const char *method, int64_t pid,
                   void (*printer)(const msgpack_object *))
{
    struct pid_arg a = { .pid = pid };
    msgpack_unpacked resp;
    int rc = rpc_call(sock, method, 1, pp_pid, &a, &resp);
    if (rc != 0) return 1;
    msgpack_object *result = NULL;
    int u = unwrap(&resp, &result);
    if (u == 0 && printer) printer(result);
    msgpack_unpacked_destroy(&resp);
    return u != 0;
}

int main(int argc, char **argv)
{
    const char *sock = YCTL_DEFAULT_SOCK;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            sock = argv[i + 1]; i += 2; continue;
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(); return 0;
        }
        break;
    }
    if (i >= argc) { usage(); return 2; }

    const char *cmd = argv[i++];

    if (strcmp(cmd, "version") == 0)  return cmd_simple(sock, "version",   NULL);
    if (strcmp(cmd, "list")    == 0)  return cmd_simple(sock, "proc.list", print_list);

    if (strcmp(cmd, "get") == 0) {
        if (i >= argc) { fprintf(stderr, "yctl get: need PID\n"); return 2; }
        return cmd_pid(sock, "proc.get", atoll(argv[i]), print_get);
    }
    if (strcmp(cmd, "mem") == 0) {
        if (i >= argc) { fprintf(stderr, "yctl mem: need PID\n"); return 2; }
        return cmd_pid(sock, "mem.regions", atoll(argv[i]), print_mem);
    }
    if (strcmp(cmd, "fd") == 0) {
        if (i >= argc) { fprintf(stderr, "yctl fd: need PID\n"); return 2; }
        return cmd_pid(sock, "fd.table", atoll(argv[i]), print_fd);
    }
    if (strcmp(cmd, "sig") == 0) {
        if (i >= argc) { fprintf(stderr, "yctl sig: need PID\n"); return 2; }
        return cmd_pid(sock, "sig.state", atoll(argv[i]), print_sig);
    }
    if (strcmp(cmd, "kill") == 0) {
        if (i + 1 >= argc) { fprintf(stderr, "yctl kill: need PID SIG\n"); return 2; }
        struct kill_arg a = { .pid = atoll(argv[i]), .sig = atoll(argv[i + 1]) };
        msgpack_unpacked resp;
        if (rpc_call(sock, "proc.kill", 2, pp_kill, &a, &resp) != 0) return 1;
        msgpack_object *r;
        int u = unwrap(&resp, &r);
        msgpack_unpacked_destroy(&resp);
        return u != 0;
    }
    if (strcmp(cmd, "trace") == 0) {
        if (i >= argc) { fprintf(stderr, "yctl trace: need on|off\n"); return 2; }
        struct bool_arg a;
        if (strcmp(argv[i], "on") == 0)       a.v = true;
        else if (strcmp(argv[i], "off") == 0) a.v = false;
        else { fprintf(stderr, "yctl trace: bad mode %s\n", argv[i]); return 2; }
        msgpack_unpacked resp;
        if (rpc_call(sock, "trace.set", 1, pp_bool, &a, &resp) != 0) return 1;
        msgpack_object *r;
        int u = unwrap(&resp, &r);
        msgpack_unpacked_destroy(&resp);
        return u != 0;
    }
    if (strcmp(cmd, "perf") == 0) {
        if (i >= argc) { fprintf(stderr, "yctl perf: need on|off|stop\n"); return 2; }
        struct str_arg a = { .s = argv[i] };
        msgpack_unpacked resp;
        if (rpc_call(sock, "perf.set", 1, pp_str, &a, &resp) != 0) return 1;
        msgpack_object *r;
        int u = unwrap(&resp, &r);
        msgpack_unpacked_destroy(&resp);
        return u != 0;
    }

    fprintf(stderr, "yctl: unknown subcommand '%s'\n", cmd);
    usage();
    return 2;
}
