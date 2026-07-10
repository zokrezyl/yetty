/* impl/freebsd_userland.c — host-side impls for the FreeBSD libc
 * userland funcs that bridge.py auto-stubs (returning NULL or -1)
 * because their signatures are "complex" — return host-allocated
 * char *, write through char **, etc.
 *
 * Without these, every FreeBSD-base port that calls strdup, strerror,
 * dirname, asprintf, … traps the moment it tries to use the result
 * (NULL deref, missing import, …). The freebsd-tools second-batch
 * ports (cut, sort, grep, sed, awk, du, df, …) all hit this — sort
 * fails on its very first sort_strdup("-").
 *
 * Pattern: each impl calls the host's libc fn into a host-side buffer,
 * then yos_malloc's a guest-side buffer and copies in. The returned
 * value is the wasm offset, callable in the guest just like a real
 * malloc'd pointer. The guest's free() resolves to yos_free.
 *
 * Listed in main.c's bind block BEFORE yos_brg_link_imports so these
 * win against bridge.py's auto-stubs.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <libgen.h>

#include "wasm3.h"
#include "m3_env.h"
#include "yos/types.h"
#include <yos/ytrace/ytrace.h>

extern uint32_t yos_malloc(struct yos_exec_ctx *ctx, uint32_t size);
extern int yos_remap_errno_h2g(int);

/* Refresh ctx->memory before any guest-pointer-touching code; wasm3
 * may have grown the linear-memory buffer since the last raw-fn
 * entered, leaving ctx->memory pointing at the freed old block. */
static inline void refresh_mem(IM3Runtime rt, struct yos_exec_ctx *ctx) {
    uint32_t sz = 0;
    ctx->memory = m3_GetMemory(rt, &sz, 0);
    ctx->memory_size = sz;
}

static inline void write_errno(struct yos_exec_ctx *ctx) {
    if (ctx && ctx->memory && ctx->errno_off) {
        *(int *)(ctx->memory + ctx->errno_off) =
            yos_remap_errno_h2g(errno);
    }
}

/* Allocate a guest-side buffer of `len` bytes and copy `src` into
 * it. Returns the wasm offset, or 0 on alloc failure.
 *
 * `src` may itself live in wasm linear memory (e.g. when strdup'ing
 * a guest string). yos_malloc can grow the wasm memory and relocate
 * the host-side mmap, which leaves any stashed `src` pointer
 * dangling — reads from it then return poisoned-page bytes (often
 * 0xff). The fix: stage src to a host scratch buffer FIRST, then
 * allocate, then copy from the staged copy. The host scratch is
 * stable across the alloc.
 *
 * This was a real visible bug — after my first strdup landed, zsh's
 * %e formatter (printing strerror(0) → "Success") emitted 8 bytes
 * of 0xff into the user's prompt every time backspace triggered an
 * stderr message, looking on the user's screen exactly like
 * "backspace inserted a space-shaped glyph". */
static uint32_t guest_dup_bytes(struct yos_exec_ctx *ctx,
                                const void *src, size_t len)
{
    if (!src || len == 0) return 0;
    /* Stage to a host buffer first — small enough to live on the
     * host stack for typical strdup/asprintf workloads (errno
     * messages, basenames, format outputs). Larger inputs go
     * through malloc so we don't overflow the host stack. */
    char inline_buf[2048];
    char *scratch;
    int   scratch_heap = 0;
    if (len <= sizeof(inline_buf)) {
        scratch = inline_buf;
    } else {
        scratch = malloc(len);
        if (!scratch) return 0;
        scratch_heap = 1;
    }
    memcpy(scratch, src, len);

    uint32_t off = yos_malloc(ctx, (uint32_t)len);
    if (!off) {
        if (scratch_heap) free(scratch);
        return 0;
    }
    /* yos_malloc may have grown linear memory — ctx->memory should
     * already be refreshed by yos_malloc itself, but the stale-
     * pointer hazard is exactly what motivated the staging above.
     * memcpy from `scratch` is safe regardless. */
    memcpy(ctx->memory + off, scratch, len);
    if (scratch_heap) free(scratch);
    return off;
}

static uint32_t guest_dup_str(struct yos_exec_ctx *ctx, const char *s)
{
    if (!s) return 0;
    return guest_dup_bytes(ctx, s, strlen(s) + 1);
}

/* ── strdup / strndup — alloc guest buf + copy ────────────────────── */

static m3ApiRawFunction(m3_yos_strdup) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, s_off);
    struct yos_exec_ctx *ctx =
        (struct yos_exec_ctx *)m3_GetUserData(runtime);
    refresh_mem(runtime, ctx);
    if (!s_off || s_off >= ctx->memory_size) m3ApiReturn(0);
    const char *s = (const char *)(ctx->memory + s_off);
    uint32_t off = guest_dup_str(ctx, s);
    if (!off) { errno = ENOMEM; write_errno(ctx); }
    m3ApiReturn(off);
}

static m3ApiRawFunction(m3_yos_strndup) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, s_off);
    m3ApiGetArg(uint32_t, n);
    struct yos_exec_ctx *ctx =
        (struct yos_exec_ctx *)m3_GetUserData(runtime);
    refresh_mem(runtime, ctx);
    if (!s_off || s_off >= ctx->memory_size) m3ApiReturn(0);
    const char *s = (const char *)(ctx->memory + s_off);
    size_t real = strnlen(s, n);
    uint32_t off = yos_malloc(ctx, (uint32_t)(real + 1));
    if (!off) { errno = ENOMEM; write_errno(ctx); m3ApiReturn(0); }
    memcpy(ctx->memory + off, s, real);
    *(char *)(ctx->memory + off + real) = '\0';
    m3ApiReturn(off);
}

/* ── strerror / strerror_l — guest-allocated message buffer ───────── */

static m3ApiRawFunction(m3_yos_strerror) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(int32_t, errnum);
    struct yos_exec_ctx *ctx =
        (struct yos_exec_ctx *)m3_GetUserData(runtime);
    refresh_mem(runtime, ctx);
    /* host strerror() returns its own static buffer; copy into a
     * fresh guest buf each call. The guest's caller treats it as
     * static-lifetime, but a fresh malloc per call is the safest
     * thing — leak is bounded by program lifetime. */
    const char *m = strerror((int)errnum);
    if (!m) m = "Unknown error";
    uint32_t off = guest_dup_str(ctx, m);
    m3ApiReturn(off);
}

/* ── dirname / basename (libgen.h variants — write through arg) ──── */

static m3ApiRawFunction(m3_yos_dirname) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, p_off);
    struct yos_exec_ctx *ctx =
        (struct yos_exec_ctx *)m3_GetUserData(runtime);
    refresh_mem(runtime, ctx);
    /* libgen dirname() may modify the input AND returns a pointer
     * that may point inside it OR to a static string. Safest: dup
     * to a host buffer, run host dirname, dup result to guest. */
    const char *p = p_off ? (const char *)(ctx->memory + p_off) : NULL;
    char tmp[PATH_MAX];
    if (p) { strncpy(tmp, p, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = 0; }
    else   { tmp[0] = 0; }
    char *r = dirname(p ? tmp : NULL);
    uint32_t off = guest_dup_str(ctx, r);
    m3ApiReturn(off);
}

/* ── BSD-only fns the auto-bridge can't synthesise ─────────────────── */

/* MB_CUR_MAX = 1 in C locale, up to 4 in UTF-8. yos has no locale
 * subsystem yet, so claim single-byte. Tools that compare a value
 * to MB_CUR_MAX (tr, awk, sort) get the C-locale fast path. */
static m3ApiRawFunction(m3_yos_mb_cur_max) {
    m3ApiReturnType(int32_t)
    m3ApiReturn(1);
}

/* getbsize: returns block size for human-readable output and
 * optionally writes through *headerlenp / *blocksizep. df and du
 * call it as `getbsize(NULL, &bs)` to get a default unit. */
static m3ApiRawFunction(m3_yos_getbsize) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, headerlen_off);
    m3ApiGetArg(uint32_t, blocksize_off);
    struct yos_exec_ctx *ctx =
        (struct yos_exec_ctx *)m3_GetUserData(runtime);
    refresh_mem(runtime, ctx);
    /* Honour BLOCKSIZE env var if set, else 512. Keep it tiny. */
    long bs = 512;
    const char *e = getenv("BLOCKSIZE");
    if (e) {
        char *end = NULL;
        long v = strtol(e, &end, 10);
        if (v > 0) bs = v;
    }
    if (blocksize_off && blocksize_off + 8 <= ctx->memory_size) {
        *(int64_t *)(ctx->memory + blocksize_off) = bs;
    }
    if (headerlen_off && headerlen_off + 4 <= ctx->memory_size) {
        *(int32_t *)(ctx->memory + headerlen_off) = 4; /* "512" + nul */
    }
    /* Static guest string — alloc once-per-call; callers print it. */
    char header[16];
    snprintf(header, sizeof(header), "%ld-blocks", bs);
    uint32_t off = guest_dup_str(ctx, header);
    m3ApiReturn(off);
}

/* getmntinfo: return 0 mounts. df then prints just the header and
 * exits. Callers signature: int getmntinfo(struct statfs **mntbufp,
 * int mode). */
static m3ApiRawFunction(m3_yos_getmntinfo) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, mntbufp_off);
    m3ApiGetArg(int32_t,  mode);
    struct yos_exec_ctx *ctx =
        (struct yos_exec_ctx *)m3_GetUserData(runtime);
    refresh_mem(runtime, ctx);
    (void)mode;
    if (mntbufp_off && mntbufp_off + 4 <= ctx->memory_size) {
        *(uint32_t *)(ctx->memory + mntbufp_off) = 0;
    }
    m3ApiReturn(0);
}

/* strtonum(numstr, minval, maxval, errstr): BSD function not in
 * glibc. Returns the parsed value on success; on failure returns 0
 * and *errstr points at a static error string. */
static m3ApiRawFunction(m3_yos_strtonum) {
    m3ApiReturnType(int64_t)
    m3ApiGetArg(uint32_t, str_off);
    m3ApiGetArg(int64_t,  minval);
    m3ApiGetArg(int64_t,  maxval);
    m3ApiGetArg(uint32_t, errstrp_off);
    struct yos_exec_ctx *ctx =
        (struct yos_exec_ctx *)m3_GetUserData(runtime);
    refresh_mem(runtime, ctx);
    const char *s = (str_off && str_off < ctx->memory_size)
                  ? (const char *)(ctx->memory + str_off) : NULL;
    const char *err_msg = NULL;
    long long val = 0;
    if (!s) { err_msg = "invalid"; }
    else if (minval > maxval) { err_msg = "invalid"; }
    else {
        char *end = NULL;
        errno = 0;
        val = strtoll(s, &end, 10);
        if (s == end || (end && *end != 0))   err_msg = "invalid";
        else if (errno == ERANGE && val == LLONG_MIN) err_msg = "too small";
        else if (errno == ERANGE && val == LLONG_MAX) err_msg = "too large";
        else if (val < minval) err_msg = "too small";
        else if (val > maxval) err_msg = "too large";
    }
    /* Write *errstrp = (errstr ? guest-static : 0). The lifetime
     * contract says the string is statically valid; allocating a
     * fresh guest buf each call is a small leak but bounded — every
     * callsite I've seen walks errstr immediately and discards. */
    if (errstrp_off && errstrp_off + 4 <= ctx->memory_size) {
        uint32_t off = err_msg ? guest_dup_str(ctx, err_msg) : 0;
        *(uint32_t *)(ctx->memory + errstrp_off) = off;
    }
    m3ApiReturn(err_msg ? (int64_t)0 : (int64_t)val);
}

/* fgetln(fp, lenp): BSD, not in glibc. Returns a pointer to the
 * just-read line (NOT nul-terminated), with *lenp = byte length.
 * The buffer is "private to the stream" — the next fgetln call may
 * overwrite. We per-call allocate a fresh guest buf and free the
 * previous one to mimic that. Single-buffer-per-call is cheap and
 * correct enough for cut + grep's read paths. */
extern FILE *yos_handle_to_file(struct yos_exec_ctx *ctx, uint32_t h);
extern int   yos_free(struct yos_exec_ctx *ctx, uint32_t off);

/* Per-ctx fgetln stash. Was a process-wide static — concurrent
 * guests would free each other's wasm-side line buffer mid-read.
 * Lives on ctx->fgetln_stash; see types.h for the field layout
 * (we reuse buf_off/buf_cap from the on-ctx struct). */

static m3ApiRawFunction(m3_yos_fgetln) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fp_off);
    m3ApiGetArg(uint32_t, lenp_off);
    struct yos_exec_ctx *ctx =
        (struct yos_exec_ctx *)m3_GetUserData(runtime);
    refresh_mem(runtime, ctx);
    FILE *f = yos_handle_to_file(ctx, fp_off);
    if (!f) {
        errno = EBADF; write_errno(ctx);
        if (lenp_off && lenp_off + 4 <= ctx->memory_size)
            *(uint32_t *)(ctx->memory + lenp_off) = 0;
        m3ApiReturn(0);
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, f);
    if (n < 0) {
        free(line);
        if (lenp_off && lenp_off + 4 <= ctx->memory_size)
            *(uint32_t *)(ctx->memory + lenp_off) = 0;
        write_errno(ctx);
        m3ApiReturn(0);
    }
    /* Free previous stash so the "buffer is private to the stream"
     * contract holds — guests that hold the old pointer past the
     * next fgetln call get use-after-free, same as on real BSD. */
    if (ctx->fgetln_stash.buf_off) {
        yos_free(ctx, ctx->fgetln_stash.buf_off);
        ctx->fgetln_stash.buf_off = 0;
    }
    uint32_t off = yos_malloc(ctx, (uint32_t)n);
    if (!off) {
        free(line);
        errno = ENOMEM; write_errno(ctx);
        if (lenp_off && lenp_off + 4 <= ctx->memory_size)
            *(uint32_t *)(ctx->memory + lenp_off) = 0;
        m3ApiReturn(0);
    }
    memcpy(ctx->memory + off, line, (size_t)n);
    free(line);
    ctx->fgetln_stash.buf_off = off;
    ctx->fgetln_stash.buf_cap = (uint32_t)n;
    if (lenp_off && lenp_off + 4 <= ctx->memory_size)
        *(uint32_t *)(ctx->memory + lenp_off) = (uint32_t)n;
    m3ApiReturn(off);
}

/* getprogname / setprogname: per-process program name. We stash a
 * guest-side buffer once and return its offset on every call. */

static m3ApiRawFunction(m3_yos_getprogname) {
    m3ApiReturnType(uint32_t)
    struct yos_exec_ctx *ctx =
        (struct yos_exec_ctx *)m3_GetUserData(runtime);
    refresh_mem(runtime, ctx);
    if (!ctx->progname_off) {
        /* Synthesise a default from argv[0] if the guest never
         * set one. */
        ctx->progname_off = guest_dup_str(ctx, "yos-tool");
    }
    m3ApiReturn(ctx->progname_off);
}

static m3ApiRawFunction(m3_yos_setprogname) {
    struct yos_exec_ctx *ctx =
        (struct yos_exec_ctx *)m3_GetUserData(runtime);
    m3ApiGetArg(uint32_t, n_off);
    refresh_mem(runtime, ctx);
    if (!n_off || n_off >= ctx->memory_size) m3ApiSuccess();
    const char *raw = (const char *)(ctx->memory + n_off);
    /* Take just the basename — FreeBSD's setprogname does the same. */
    const char *base = strrchr(raw, '/');
    base = base ? base + 1 : raw;
    if (ctx->progname_off) yos_free(ctx, ctx->progname_off);
    ctx->progname_off = guest_dup_str(ctx, base);
    m3ApiSuccess();
}

/* asprintf / vasprintf: format + alloc + writeback through char **.
 *
 * Re-uses yos_vsnprintf (impl/printf.c) which already walks the
 * guest's variadic shadow-stack and writes into a host buffer. Then
 * we yos_malloc a guest buf the right size, copy, and write the
 * offset through the wasm-side `char **strp`. */
extern int32_t yos_vsnprintf_core(struct yos_exec_ctx *ctx,
                                  char *out, size_t out_cap,
                                  uint32_t fmt_off, uint32_t va_off);

static m3ApiRawFunction(m3_yos_asprintf) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, strp_off);
    m3ApiGetArg(uint32_t, fmt_off);
    m3ApiGetArg(uint32_t, va_off);
    struct yos_exec_ctx *ctx =
        (struct yos_exec_ctx *)m3_GetUserData(runtime);
    refresh_mem(runtime, ctx);
    char buf[8192];
    int32_t n = yos_vsnprintf_core(ctx, buf, sizeof(buf), fmt_off, va_off);
    if (n < 0) {
        if (strp_off && strp_off + 4 <= ctx->memory_size)
            *(uint32_t *)(ctx->memory + strp_off) = 0;
        m3ApiReturn(-1);
    }
    /* Cap at sizeof(buf)-1 — anything truncated stays truncated.
     * Real asprintf grows on demand; this is the documented limit
     * of yos's printf path. */
    size_t len = (size_t)n;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    uint32_t off = yos_malloc(ctx, (uint32_t)(len + 1));
    if (!off) {
        if (strp_off && strp_off + 4 <= ctx->memory_size)
            *(uint32_t *)(ctx->memory + strp_off) = 0;
        m3ApiReturn(-1);
    }
    memcpy(ctx->memory + off, buf, len);
    *(char *)(ctx->memory + off + len) = 0;
    if (strp_off && strp_off + 4 <= ctx->memory_size)
        *(uint32_t *)(ctx->memory + strp_off) = off;
    m3ApiReturn((int32_t)len);
}

/* vasprintf has the same wasm-ABI shape as asprintf — the guest's
 * libc wraps both into the same (strp, fmt, va_list) call. */
static m3ApiRawFunction(m3_yos_vasprintf) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, strp_off);
    m3ApiGetArg(uint32_t, fmt_off);
    m3ApiGetArg(uint32_t, va_off);
    struct yos_exec_ctx *ctx =
        (struct yos_exec_ctx *)m3_GetUserData(runtime);
    refresh_mem(runtime, ctx);
    char buf[8192];
    int32_t n = yos_vsnprintf_core(ctx, buf, sizeof(buf), fmt_off, va_off);
    if (n < 0) {
        if (strp_off && strp_off + 4 <= ctx->memory_size)
            *(uint32_t *)(ctx->memory + strp_off) = 0;
        m3ApiReturn(-1);
    }
    size_t len = (size_t)n;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    uint32_t off = yos_malloc(ctx, (uint32_t)(len + 1));
    if (!off) m3ApiReturn(-1);
    memcpy(ctx->memory + off, buf, len);
    *(char *)(ctx->memory + off + len) = 0;
    if (strp_off && strp_off + 4 <= ctx->memory_size)
        *(uint32_t *)(ctx->memory + strp_off) = off;
    m3ApiReturn((int32_t)len);
}

/* ── link entry point — called from main.c BEFORE yos_brg_link ────── */

void yos_freebsd_userland_link(IM3Module mod)
{
    /* signature `i(i)`  = takes one int32, returns int32 (or uint32). */
    m3_LinkRawFunction(mod, "env", "strdup",        "i(i)",     m3_yos_strdup);
    m3_LinkRawFunction(mod, "env", "strndup",       "i(ii)",    m3_yos_strndup);
    m3_LinkRawFunction(mod, "env", "strerror",      "i(i)",     m3_yos_strerror);
    m3_LinkRawFunction(mod, "env", "dirname",       "i(i)",     m3_yos_dirname);
    m3_LinkRawFunction(mod, "env", "___mb_cur_max", "i()",      m3_yos_mb_cur_max);
    m3_LinkRawFunction(mod, "env", "getbsize",      "i(ii)",    m3_yos_getbsize);
    m3_LinkRawFunction(mod, "env", "getmntinfo",    "i(ii)",    m3_yos_getmntinfo);
    m3_LinkRawFunction(mod, "env", "strtonum",      "I(iIIi)",  m3_yos_strtonum);
    m3_LinkRawFunction(mod, "env", "fgetln",        "i(ii)",    m3_yos_fgetln);
    m3_LinkRawFunction(mod, "env", "getprogname",   "i()",      m3_yos_getprogname);
    /* FreeBSD libc carries an internal weak alias `_getprogname` that
     * its own .c files use; build-from-source ports import the alias
     * rather than `getprogname`. Same body. */
    m3_LinkRawFunction(mod, "env", "_getprogname",  "i()",      m3_yos_getprogname);
    m3_LinkRawFunction(mod, "env", "setprogname",   "v(i)",     m3_yos_setprogname);
    m3_LinkRawFunction(mod, "env", "asprintf",      "i(iii)",   m3_yos_asprintf);
    m3_LinkRawFunction(mod, "env", "vasprintf",     "i(iii)",   m3_yos_vasprintf);
}

/* Called from impl/proc/proc.c after m3_FreeRuntime during execve.
 * ctx->progname_off is a wasm offset into the now-freed memory; the next
 * process's getprogname() would otherwise hand back the stale offset
 * and the guest would dereference whatever happens to live there
 * in the fresh wasm linear memory. */
void yos_freebsd_userland_post_execve_reset(struct yos_exec_ctx *ctx)
{
    ctx->progname_off = 0;
    ctx->fgetln_stash.buf_off = 0;
    ctx->fgetln_stash.buf_cap = 0;
}
