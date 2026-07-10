/* impl/libc/libarchive.c — host-side bridges exposing host libarchive
 * to the wasm guest as env.archive_* imports.
 *
 * Architecture mirrors liblua/openssl: the yos host binary links host
 * libarchive, the wasm guest (bsdtar, or a small libarchive driver)
 * carries no libarchive bodies, and every archive_* call resolves to
 * env.<name> at module load.
 *
 * Why this is a clean Tier-1 case (see the libbridge globals analysis):
 * libarchive exports ZERO writable file-scope globals. All state lives
 * in the caller-owned `struct archive *` and `struct archive_entry *`.
 * Per-guest isolation is therefore automatic — each guest holds its own
 * handles; the bridge just maps the i32 handle to the host pointer via
 * ctx->arc_handles[]. No global-swap dance.
 *
 * Scope of this first cut — the read/list/extract-data path:
 *   archive_read_new / _support_format_all / _support_filter_all /
 *   _open_memory / _next_header / _data / _data_skip / _free,
 *   archive_entry_pathname / _size / _filetype,
 *   archive_error_string / archive_errno.
 * That is enough to open an archive the guest holds in linear memory,
 * walk its entries, and pull out each entry's bytes.
 *
 * Deferred (next increments):
 *   - File/fd-backed open (archive_read_open_fd / _filename). The
 *     correct shape routes I/O through yos's vfs via a host-side read
 *     callback keyed on a guest fd, so host libarchive never touches a
 *     guest fd number directly. _open_memory needs none of that.
 *   - The write/disk-extraction surface (archive_write_disk_*).
 *   - Custom client callbacks (archive_read_open2) — same host->guest
 *     trampoline problem liblua defers for lua_CFunction.
 *
 * Conventions match the sibling bridges:
 *   - m3 raw-function args at _sp[1..] for "i(...)"/"I(...)" sigs
 *     (result in _sp[0]); at _sp[0..] for "v(...)".
 *   - Opaque pointers wrapped as i32 handles in ctx->arc_handles[].
 *   - const char * returns copied into a per-ctx scratch at the tail
 *     of guest memory; the guest must consume the offset before the
 *     next string-returning bridge call.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "wasm3.h"
#include "m3_env.h"
#include "yos/types.h"
#include <yos/ytrace/ytrace.h>

/* ── yos subsystem entry points (impl/io) ────────────────────────────
 * Declared here the same way the sibling bridges do (posix.c, pwd.c):
 * the host build links these from impl/io, we just forward-declare the
 * prototypes we use rather than pull in an internal header. These are
 * what keep guest paths/fds from reaching host libarchive directly —
 * yos_open honours ctx->cwd + the VFS mount table + fake fifo/pty
 * routing; yos_fd_get translates a guest fd to the host fd it owns. */
extern int32_t yos_open    (struct yos_exec_ctx *ctx, uint32_t path,
                            int32_t flags, int32_t mode);
extern int32_t yos_read    (struct yos_exec_ctx *ctx, int32_t fd,
                            uint32_t buf, uint32_t count);
extern int32_t yos_write   (struct yos_exec_ctx *ctx, int32_t fd,
                            uint32_t buf, uint32_t count);
extern int32_t yos_fd_get  (struct yos_exec_ctx *ctx, int32_t wfd);
extern int32_t yos_fd_close(struct yos_exec_ctx *ctx, int32_t wfd);
/* Guest-heap allocator — used to carve a read-scratch region inside the
 * guest's own linear memory so the archive read callback can route bytes
 * through yos_read (virtual-fd aware) instead of a raw host read(). */
extern uint32_t yos_malloc (struct yos_exec_ctx *ctx, uint32_t size);
extern void     yos_free   (struct yos_exec_ctx *ctx, uint32_t off);

/* FreeBSD O_RDONLY — yos_open takes FreeBSD oflags and remaps them to
 * the host's. 0 on every BSD/Linux flavour; spelled out for clarity. */
#define YOS_ARC_O_RDONLY 0x0000

/* ── host libarchive forward decls ───────────────────────────────────
 * Declared here rather than via <archive.h> so the host build needs
 * only the shared library (-larchive), not libarchive's -dev headers
 * — same approach liblua.c takes with <lua.h>. Signatures pinned to
 * libarchive 3.x. la_int64_t/la_ssize_t are int64 on every host. */
struct archive;
struct archive_entry;
extern struct archive *archive_read_new(void);
extern int   archive_read_support_format_all(struct archive *);
extern int   archive_read_support_format_empty(struct archive *);
extern int   archive_read_support_format_raw(struct archive *);
extern int   archive_read_support_filter_all(struct archive *);
extern int   archive_read_open_memory(struct archive *, const void *, size_t);
extern int   archive_read_close(struct archive *);

/* Callback-based open. We use this instead of archive_read_open_filename
 * so the bytes come from a yos-opened fd (cwd/VFS/path-translation
 * honoured) rather than a guest path handed straight to host libarchive.
 * la_int64_t / la_ssize_t are 64-bit on every host we target. */
typedef int64_t la_arc_read_cb(struct archive *, void *client,
                               const void **buffer);
typedef int64_t la_arc_skip_cb(struct archive *, void *client,
                               int64_t request);
typedef int     la_arc_open_cb(struct archive *, void *client);
typedef int     la_arc_close_cb(struct archive *, void *client);
extern int   archive_read_open2(struct archive *, void *client,
                                la_arc_open_cb *, la_arc_read_cb *,
                                la_arc_skip_cb *, la_arc_close_cb *);
extern const char *archive_version_details(void);
extern int   archive_read_next_header(struct archive *, struct archive_entry **);
extern int64_t archive_read_data(struct archive *, void *, size_t);
extern int   archive_read_data_skip(struct archive *);
extern int   archive_read_free(struct archive *);
extern const char *archive_error_string(struct archive *);
extern int   archive_errno(struct archive *);
extern const char *archive_entry_pathname(struct archive_entry *);
extern int64_t archive_entry_size(struct archive_entry *);
extern int   archive_entry_filetype(struct archive_entry *);

#define CTX(rt) ((struct yos_exec_ctx *)m3_GetUserData(rt))

#define YOS_ARC_HANDLES_INIT 16
#define YOS_ARC_HANDLES_GROW 16

/* Slot kind tags (ctx->arc_handle_kinds[]). archive and entry pointers
 * share one table, but teardown must free only the archives — an entry
 * pointer is owned by its parent archive and freeing it directly would
 * double-free. */
enum {
    YOS_ARC_KIND_EMPTY   = 0,
    YOS_ARC_KIND_ARCHIVE = 1,
    YOS_ARC_KIND_ENTRY   = 2,
};

/* ── handle table (struct archive * and struct archive_entry *) ───────
 * Three parallel arrays indexed 1..cap-1 (slot 0 reserved): the pointer,
 * its kind, and — for ENTRY slots — the archive handle that owns it (so
 * freeing an archive can invalidate its dangling entries). */

static int arc_handles_reserve(struct yos_exec_ctx *ctx)
{
    if (ctx->arc_handles_cap == 0) {
        size_t cap = YOS_ARC_HANDLES_INIT;
        void **slots = calloc(cap, sizeof(void *));
        uint8_t *kinds = calloc(cap, sizeof(uint8_t));
        uint32_t *owner = calloc(cap, sizeof(uint32_t));
        if (!slots || !kinds || !owner) {
            free(slots); free(kinds); free(owner);
            return -1;
        }
        ctx->arc_handles = slots;
        ctx->arc_handle_kinds = kinds;
        ctx->arc_handle_owner = owner;
        ctx->arc_handles_cap = (uint32_t)cap;
    }
    for (uint32_t i = 1; i < ctx->arc_handles_cap; ++i)
        if (!ctx->arc_handles[i]) return 0;
    size_t newcap = (size_t)ctx->arc_handles_cap + YOS_ARC_HANDLES_GROW;
    void **next = realloc(ctx->arc_handles, newcap * sizeof(void *));
    if (!next) return -1;
    ctx->arc_handles = next;
    uint8_t *next_kinds = realloc(ctx->arc_handle_kinds,
                                  newcap * sizeof(uint8_t));
    if (!next_kinds) return -1;
    ctx->arc_handle_kinds = next_kinds;
    uint32_t *next_owner = realloc(ctx->arc_handle_owner,
                                   newcap * sizeof(uint32_t));
    if (!next_owner) return -1;
    ctx->arc_handle_owner = next_owner;
    memset(next + ctx->arc_handles_cap, 0,
           (newcap - ctx->arc_handles_cap) * sizeof(void *));
    memset(next_kinds + ctx->arc_handles_cap, 0,
           (newcap - ctx->arc_handles_cap) * sizeof(uint8_t));
    memset(next_owner + ctx->arc_handles_cap, 0,
           (newcap - ctx->arc_handles_cap) * sizeof(uint32_t));
    ctx->arc_handles_cap = (uint32_t)newcap;
    return 0;
}

static uint32_t arc_handles_wrap(struct yos_exec_ctx *ctx, void *p,
                                 uint8_t kind, uint32_t owner)
{
    if (!p) return 0;
    if (arc_handles_reserve(ctx) < 0) return 0;
    for (uint32_t i = 1; i < ctx->arc_handles_cap; ++i)
        if (!ctx->arc_handles[i]) {
            ctx->arc_handles[i] = p;
            ctx->arc_handle_kinds[i] = kind;
            ctx->arc_handle_owner[i] = owner;
            return i;
        }
    return 0;
}

/* Wrap p, reusing an existing handle if p is already in the table.
 * archive_read_next_header reuses one internal archive_entry across
 * calls, so without this the table would grow one slot per entry. The
 * owner is refreshed so the slot always points at the live archive. */
static uint32_t arc_handles_wrap_unique(struct yos_exec_ctx *ctx, void *p,
                                        uint8_t kind, uint32_t owner)
{
    if (!p) return 0;
    for (uint32_t i = 1; i < ctx->arc_handles_cap; ++i)
        if (ctx->arc_handles[i] == p) {
            ctx->arc_handle_kinds[i] = kind;
            ctx->arc_handle_owner[i] = owner;
            return i;
        }
    return arc_handles_wrap(ctx, p, kind, owner);
}

/* Resolve a handle ONLY if it is of the expected kind. A guest that
 * passes an entry handle where an archive is expected (or vice versa, or
 * a handle whose owning archive was already freed) gets NULL — never a
 * cross-type pointer cast. */
static void *arc_handles_resolve_kind(struct yos_exec_ctx *ctx, uint32_t h,
                                      uint8_t kind)
{
    if (!ctx || !ctx->arc_handles || h == 0 || h >= ctx->arc_handles_cap)
        return NULL;
    if (ctx->arc_handle_kinds[h] != kind) return NULL;
    return ctx->arc_handles[h];
}

static void *arc_handles_release(struct yos_exec_ctx *ctx, uint32_t h)
{
    if (!ctx || !ctx->arc_handles || h == 0 || h >= ctx->arc_handles_cap)
        return NULL;
    void *p = ctx->arc_handles[h];
    ctx->arc_handles[h] = NULL;
    ctx->arc_handle_kinds[h] = YOS_ARC_KIND_EMPTY;
    ctx->arc_handle_owner[h] = 0;
    return p;
}

/* Release an archive slot AND invalidate every entry slot it owns — the
 * entry pointers belong to the archive and dangle once it is freed.
 * Returns the archive pointer, or NULL if h is not a live archive slot
 * (so the caller never frees an entry handle as an archive). */
static struct archive *arc_release_archive(struct yos_exec_ctx *ctx, uint32_t h)
{
    void *p = arc_handles_resolve_kind(ctx, h, YOS_ARC_KIND_ARCHIVE);
    if (!p) return NULL;
    for (uint32_t i = 1; i < ctx->arc_handles_cap; ++i)
        if (ctx->arc_handle_kinds[i] == YOS_ARC_KIND_ENTRY &&
            ctx->arc_handle_owner[i] == h) {
            ctx->arc_handles[i] = NULL;
            ctx->arc_handle_kinds[i] = YOS_ARC_KIND_EMPTY;
            ctx->arc_handle_owner[i] = 0;
        }
    arc_handles_release(ctx, h);
    return (struct archive *)p;
}

/* ── guest-memory helpers ───────────────────────────────────────────── */

static void *guest_buf_rw(struct yos_exec_ctx *ctx, uint32_t off, size_t len)
{
    if (!ctx || !ctx->memory || off == 0) return NULL;
    if (off >= ctx->memory_size) return NULL;
    if (len > ctx->memory_size || off + len > ctx->memory_size) return NULL;
    return ctx->memory + off;
}

static const void *guest_buf_ro(struct yos_exec_ctx *ctx, uint32_t off,
                                size_t len)
{
    return guest_buf_rw(ctx, off, len);
}

/* Resolve a guest NUL-terminated string to a host pointer into guest
 * memory, verifying the NUL falls within bounds. Returns NULL for a
 * 0 offset (the caller maps that to "no filename" / stdin). */
static const char *guest_str(struct yos_exec_ctx *ctx, uint32_t off)
{
    if (!ctx || !ctx->memory || off == 0 || off >= ctx->memory_size)
        return NULL;
    for (uint32_t i = off; i < ctx->memory_size; i++)
        if (ctx->memory[i] == 0)
            return (const char *)(ctx->memory + off);
    return NULL;
}

/* Stash a host string into a fixed scratch slot at the tail of guest
 * memory and return its offset. The guest must consume the offset
 * before the next string-returning bridge call (same caveat as the
 * liblua/openssl string-return path). */
static uint32_t guest_stash_string(struct yos_exec_ctx *ctx, const char *s)
{
    if (!s || !ctx || !ctx->memory || ctx->memory_size < 4096 + 16)
        return 0;
    size_t len = strlen(s);
    if (len > 4095) len = 4095;
    uint32_t at = ctx->memory_size - 4096;
    memcpy(ctx->memory + at, s, len);
    ctx->memory[at + len] = 0;
    return at;
}

/* ── bridges ─────────────────────────────────────────────────────────
 * Reusable shorthands: A(slot) resolves an archive handle, E(slot) an
 * entry handle (same table). For "i(...)"/"I(...)" sigs args begin at
 * _sp[1]; the result is written to _sp[0]. */
#define A(slot) ((struct archive *)arc_handles_resolve_kind(CTX(rt), (uint32_t)_sp[slot], YOS_ARC_KIND_ARCHIVE))
#define E(slot) ((struct archive_entry *)arc_handles_resolve_kind(CTX(rt), (uint32_t)_sp[slot], YOS_ARC_KIND_ENTRY))

/* env.archive_read_new — i(). Returns an i32 handle (0 on failure). */
static const void *m3_yos_archive_read_new(IM3Runtime rt, IM3ImportContext _c,
                                           uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct archive *a = archive_read_new();
    _sp[0] = (uint64_t)arc_handles_wrap(CTX(rt), a, YOS_ARC_KIND_ARCHIVE, 0);
    return NULL;
}

/* env.archive_read_support_format_all — i(a). */
static const void *m3_yos_archive_read_support_format_all(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct archive *a = A(1);
    _sp[0] = (uint64_t)(uint32_t)(a ? archive_read_support_format_all(a) : -1);
    return NULL;
}

/* env.archive_read_support_format_empty — i(a). */
static const void *m3_yos_archive_read_support_format_empty(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct archive *a = A(1);
    _sp[0] = (uint64_t)(uint32_t)(a ? archive_read_support_format_empty(a) : -1);
    return NULL;
}

/* env.archive_read_support_format_raw — i(a). */
static const void *m3_yos_archive_read_support_format_raw(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct archive *a = A(1);
    _sp[0] = (uint64_t)(uint32_t)(a ? archive_read_support_format_raw(a) : -1);
    return NULL;
}

/* env.archive_read_support_filter_all — i(a). */
static const void *m3_yos_archive_read_support_filter_all(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct archive *a = A(1);
    _sp[0] = (uint64_t)(uint32_t)(a ? archive_read_support_filter_all(a) : -1);
    return NULL;
}

/* ── yos-fd-backed read client for archive_read_open2 ────────────────
 * archive_read_open_filename would hand a guest path straight to host
 * libarchive, bypassing ctx->cwd, the VFS mount table, fake fifo/pty
 * routing and platform path translation — and a guest fd is not a host
 * fd. Instead we open the path through yos_open (full yos semantics),
 * then feed libarchive from that yos fd via these callbacks. The client
 * owns a host-side read buffer and is freed in the close callback
 * (which libarchive invokes exactly once, on close or free — so a
 * leaked archive freed at teardown also releases the client). */
#define YOS_ARC_SCRATCH_LEN 65536u

struct yos_arc_file_client {
    struct yos_exec_ctx *ctx;
    int32_t  wfd;            /* yos wasm fd backing the archive          */
    int      owns_fd;        /* did WE open wfd? (vs a borrowed std fd)  */
    uint32_t scratch_off;    /* guest-memory read scratch (yos_malloc'd) */
    uint32_t scratch_len;
};

/* Read callback: route bytes through yos_read so the full yos fd model
 * applies — virtual fds (procfs etc.), fd translation, signal pumping —
 * instead of a raw host read() on a translated host fd. We read into a
 * scratch region of the guest's OWN linear memory and hand libarchive a
 * pointer into it (ctx->memory is host-addressable). */
static int64_t arc_file_read(struct archive *a, void *client,
                             const void **buffer)
{
    (void)a;
    struct yos_arc_file_client *fc = client;
    if (!fc->scratch_off) return -1;
    int32_t n = yos_read(fc->ctx, fc->wfd, fc->scratch_off, fc->scratch_len);
    if (n < 0) return -1;                 /* yos_read returns -errno      */
    *buffer = fc->ctx->memory + fc->scratch_off;
    return (int64_t)n;                    /* 0 == EOF                      */
}

static int arc_file_close(struct archive *a, void *client)
{
    (void)a;
    struct yos_arc_file_client *fc = client;
    if (fc) {
        if (fc->scratch_off) yos_free(fc->ctx, fc->scratch_off);
        /* Only close a fd we opened ourselves; never a borrowed std
         * stream (the NULL-filename=stdin path). The explicit owns_fd
         * flag — not a wfd>2 guess — is authoritative even if yos_open
         * ever recycles a low fd number. */
        if (fc->owns_fd && fc->wfd >= 0) yos_fd_close(fc->ctx, fc->wfd);
        free(fc);
    }
    return 0; /* ARCHIVE_OK */
}

/* env.archive_read_open_filename — i(a, filename_off, block_size).
 * A 0 offset means NULL = stdin (wasm fd 0). block_size is ignored: our
 * read callback supplies its own guest-memory scratch. The guest path is
 * opened through yos_open so cwd/VFS/fake-device routing all apply, and
 * the archive is fed via yos_read — it never sees a raw guest path or a
 * raw host fd. Skip is left NULL so libarchive read-skips through the
 * same yos_read path rather than seeking a host fd behind yos's back. */
static const void *m3_yos_archive_read_open_filename(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    struct archive *a = A(1);
    uint32_t fn_off = (uint32_t)_sp[2];
    if (!a) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }

    int32_t wfd;
    int owns_fd;
    if (fn_off == 0) {
        wfd = 0;        /* NULL filename → borrow the guest's stdin */
        owns_fd = 0;
    } else {
        /* Validate the guest string is bounded before yos_open walks
         * it (yos_open re-validates, but reject here so a bad pointer
         * never reaches the open path). */
        if (!guest_str(ctx, fn_off)) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }
        wfd = yos_open(ctx, fn_off, YOS_ARC_O_RDONLY, 0);
        if (wfd < 0) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }
        owns_fd = 1;
    }

    struct yos_arc_file_client *fc = calloc(1, sizeof *fc);
    uint32_t scratch = fc ? yos_malloc(ctx, YOS_ARC_SCRATCH_LEN) : 0;
    if (!fc || !scratch) {
        if (scratch) yos_free(ctx, scratch);
        free(fc);
        if (owns_fd) yos_fd_close(ctx, wfd);
        _sp[0] = (uint64_t)(uint32_t)-1;
        return NULL;
    }
    fc->ctx = ctx;
    fc->wfd = wfd;
    fc->owns_fd = owns_fd;
    fc->scratch_off = scratch;
    fc->scratch_len = YOS_ARC_SCRATCH_LEN;

    int r = archive_read_open2(a, fc, NULL, arc_file_read, NULL,
                               arc_file_close);
    /* On failure libarchive still invokes arc_file_close (freeing fc,
     * the scratch and the fd); on success it owns fc until close/free.
     * Either way we must not touch fc again here. */
    _sp[0] = (uint64_t)(uint32_t)r;
    return NULL;
}

/* env.archive_read_close — i(a). */
static const void *m3_yos_archive_read_close(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct archive *a = A(1);
    _sp[0] = (uint64_t)(uint32_t)(a ? archive_read_close(a) : -1);
    return NULL;
}

/* env.archive_read_data_into_fd — I(a, fd). Streams the current entry's
 * data to a yos wasm fd. The guest fd is NOT a host fd, so we must not
 * hand it to host libarchive's archive_read_data_into_fd (which would
 * write to a translated host fd behind yos's back, skipping virtual-fd
 * handling, yos_write's error/short-write semantics, stderr tracking and
 * tty/fork output filtering). Instead pull the decompressed bytes into a
 * guest-memory scratch with archive_read_data and push them out through
 * yos_write — the same yos fd model the read side already routes through.
 * Returns ARCHIVE_OK(0)/_FATAL etc. as an int64. */
static const void *m3_yos_archive_read_data_into_fd(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    struct archive *a = A(1);
    int32_t wfd = (int32_t)_sp[2];
    if (!a) { _sp[0] = (uint64_t)(int64_t)-1; return NULL; }

    uint32_t scratch = yos_malloc(ctx, YOS_ARC_SCRATCH_LEN);
    if (!scratch) { _sp[0] = (uint64_t)(int64_t)-1; return NULL; }

    int64_t status = 0;  /* ARCHIVE_OK */
    for (;;) {
        int64_t n = archive_read_data(a, ctx->memory + scratch,
                                      YOS_ARC_SCRATCH_LEN);
        if (n == 0) break;                     /* end of entry data        */
        if (n < 0) { status = n; break; }      /* ARCHIVE_FATAL/_WARN etc. */
        /* Honour short writes — yos_write may return fewer than asked. */
        uint32_t done = 0;
        while (done < (uint32_t)n) {
            int32_t w = yos_write(ctx, wfd, scratch + done,
                                  (uint32_t)n - done);
            if (w <= 0) { status = -1; break; }   /* ARCHIVE_FATAL         */
            done += (uint32_t)w;
        }
        if (status != 0) break;
    }

    yos_free(ctx, scratch);
    _sp[0] = (uint64_t)status;
    return NULL;
}

/* env.archive_version_details — i(). Returns a stashed-string offset. */
static const void *m3_yos_archive_version_details(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    const char *s = archive_version_details();
    _sp[0] = (uint64_t)(s ? guest_stash_string(CTX(rt), s) : 0);
    return NULL;
}

/* env.archive_read_open_memory — i(a, buf_off, size).
 * libarchive references the buffer for the archive's lifetime; the
 * guest's linear memory is stable for the guest's lifetime, so handing
 * ctx->memory + off straight in is safe. */
static const void *m3_yos_archive_read_open_memory(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    struct archive *a = A(1);
    uint32_t buf_off = (uint32_t)_sp[2];
    size_t size = (size_t)(uint32_t)_sp[3];
    const void *buf = guest_buf_ro(ctx, buf_off, size);
    if (!a || !buf) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }
    _sp[0] = (uint64_t)(uint32_t)archive_read_open_memory(a, buf, size);
    return NULL;
}

/* env.archive_read_next_header — i(a, entry_slot_off).
 * Writes the entry's i32 handle into the guest's *entry slot and
 * returns the libarchive status (ARCHIVE_OK=0, ARCHIVE_EOF=1, ...). */
static const void *m3_yos_archive_read_next_header(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    uint32_t arc_handle = (uint32_t)_sp[1];
    struct archive *a = A(1);
    uint32_t slot_off = (uint32_t)_sp[2];
    if (!a) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }
    struct archive_entry *e = NULL;
    int r = archive_read_next_header(a, &e);
    if (r == 0 && e) {
        /* Tag the entry with its owning archive handle so freeing the
         * archive invalidates it (the entry pointer is the archive's). */
        uint32_t h = arc_handles_wrap_unique(ctx, e, YOS_ARC_KIND_ENTRY,
                                             arc_handle);
        uint32_t *slot = (uint32_t *)guest_buf_rw(ctx, slot_off,
                                                  sizeof(uint32_t));
        if (slot) *slot = h;
    }
    _sp[0] = (uint64_t)(uint32_t)r;
    return NULL;
}

/* env.archive_read_data — I(a, buf_off, size). Returns bytes read
 * (>=0), 0 at end of entry, or a negative libarchive error code. */
static const void *m3_yos_archive_read_data(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    struct archive *a = A(1);
    uint32_t buf_off = (uint32_t)_sp[2];
    size_t size = (size_t)(uint32_t)_sp[3];
    void *buf = guest_buf_rw(ctx, buf_off, size);
    if (!a || !buf) { _sp[0] = (uint64_t)(int64_t)-1; return NULL; }
    _sp[0] = (uint64_t)archive_read_data(a, buf, size);
    return NULL;
}

/* env.archive_read_data_skip — i(a). */
static const void *m3_yos_archive_read_data_skip(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct archive *a = A(1);
    _sp[0] = (uint64_t)(uint32_t)(a ? archive_read_data_skip(a) : -1);
    return NULL;
}

/* env.archive_read_free — i(a). Releases the handle and invalidates any
 * entry handles it owns. arc_release_archive refuses a non-archive
 * handle, so a guest that passes an entry (or stale) handle here gets a
 * no-op instead of an entry pointer cast to struct archive * and freed. */
static const void *m3_yos_archive_read_free(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    struct archive *a = arc_release_archive(ctx, (uint32_t)_sp[1]);
    _sp[0] = (uint64_t)(uint32_t)(a ? archive_read_free(a) : 0);
    return NULL;
}

/* env.archive_error_string — i(a). Returns a stashed-string offset
 * (0 if none). */
static const void *m3_yos_archive_error_string(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    struct archive *a = A(1);
    const char *s = a ? archive_error_string(a) : NULL;
    _sp[0] = (uint64_t)(s ? guest_stash_string(ctx, s) : 0);
    return NULL;
}

/* env.archive_errno — i(a). */
static const void *m3_yos_archive_errno(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct archive *a = A(1);
    _sp[0] = (uint64_t)(uint32_t)(a ? archive_errno(a) : 0);
    return NULL;
}

/* env.archive_entry_pathname — i(e). Returns a stashed-string offset. */
static const void *m3_yos_archive_entry_pathname(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = CTX(rt);
    struct archive_entry *e = E(1);
    const char *s = e ? archive_entry_pathname(e) : NULL;
    _sp[0] = (uint64_t)(s ? guest_stash_string(ctx, s) : 0);
    return NULL;
}

/* env.archive_entry_size — I(e). */
static const void *m3_yos_archive_entry_size(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct archive_entry *e = E(1);
    _sp[0] = (uint64_t)(e ? archive_entry_size(e) : 0);
    return NULL;
}

/* env.archive_entry_filetype — i(e). */
static const void *m3_yos_archive_entry_filetype(IM3Runtime rt,
        IM3ImportContext _c, uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct archive_entry *e = E(1);
    _sp[0] = (uint64_t)(uint32_t)(e ? archive_entry_filetype(e) : 0);
    return NULL;
}

/* ── teardown ───────────────────────────────────────────────────────
 * Free any archive handles the guest leaked (didn't archive_read_free)
 * and drop the table. archive_entry pointers are owned by their parent
 * archive, so we free ONLY the slots tagged YOS_ARC_KIND_ARCHIVE;
 * freeing an entry pointer directly would double-free. archive_read_free
 * also runs the registered close callback, releasing any yos fd /
 * read-client backing a filename-opened archive. */
void yos_libarchive_ctx_free(struct yos_exec_ctx *ctx)
{
    if (!ctx || !ctx->arc_handles) return;
    if (ctx->arc_handle_kinds) {
        for (uint32_t i = 1; i < ctx->arc_handles_cap; ++i) {
            if (ctx->arc_handles[i] &&
                ctx->arc_handle_kinds[i] == YOS_ARC_KIND_ARCHIVE) {
                archive_read_free((struct archive *)ctx->arc_handles[i]);
                ctx->arc_handles[i] = NULL;
                ctx->arc_handle_kinds[i] = YOS_ARC_KIND_EMPTY;
            }
        }
    }
    free(ctx->arc_handles);
    free(ctx->arc_handle_kinds);
    free(ctx->arc_handle_owner);
    ctx->arc_handles = NULL;
    ctx->arc_handle_kinds = NULL;
    ctx->arc_handle_owner = NULL;
    ctx->arc_handles_cap = 0;
}

/* ── link ───────────────────────────────────────────────────────────── */

void yos_libarchive_link(IM3Module mod)
{
    m3_LinkRawFunction(mod, "env", "archive_read_new",            "i()",    m3_yos_archive_read_new);
    m3_LinkRawFunction(mod, "env", "archive_read_support_format_all", "i(i)", m3_yos_archive_read_support_format_all);
    m3_LinkRawFunction(mod, "env", "archive_read_support_format_empty", "i(i)", m3_yos_archive_read_support_format_empty);
    m3_LinkRawFunction(mod, "env", "archive_read_support_format_raw", "i(i)", m3_yos_archive_read_support_format_raw);
    m3_LinkRawFunction(mod, "env", "archive_read_support_filter_all", "i(i)", m3_yos_archive_read_support_filter_all);
    m3_LinkRawFunction(mod, "env", "archive_read_open_memory",    "i(iii)", m3_yos_archive_read_open_memory);
    m3_LinkRawFunction(mod, "env", "archive_read_open_filename",  "i(iii)", m3_yos_archive_read_open_filename);
    m3_LinkRawFunction(mod, "env", "archive_read_close",          "i(i)",   m3_yos_archive_read_close);
    m3_LinkRawFunction(mod, "env", "archive_read_data_into_fd",   "I(ii)",  m3_yos_archive_read_data_into_fd);
    m3_LinkRawFunction(mod, "env", "archive_version_details",     "i()",    m3_yos_archive_version_details);
    m3_LinkRawFunction(mod, "env", "archive_read_next_header",    "i(ii)",  m3_yos_archive_read_next_header);
    m3_LinkRawFunction(mod, "env", "archive_read_data",           "I(iii)", m3_yos_archive_read_data);
    m3_LinkRawFunction(mod, "env", "archive_read_data_skip",      "i(i)",   m3_yos_archive_read_data_skip);
    m3_LinkRawFunction(mod, "env", "archive_read_free",           "i(i)",   m3_yos_archive_read_free);
    m3_LinkRawFunction(mod, "env", "archive_error_string",        "i(i)",   m3_yos_archive_error_string);
    m3_LinkRawFunction(mod, "env", "archive_errno",               "i(i)",   m3_yos_archive_errno);
    m3_LinkRawFunction(mod, "env", "archive_entry_pathname",      "i(i)",   m3_yos_archive_entry_pathname);
    m3_LinkRawFunction(mod, "env", "archive_entry_size",          "I(i)",   m3_yos_archive_entry_size);
    m3_LinkRawFunction(mod, "env", "archive_entry_filetype",      "i(i)",   m3_yos_archive_entry_filetype);
}
