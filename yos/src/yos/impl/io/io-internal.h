#ifndef YOS_IO_INTERNAL_H
#define YOS_IO_INTERNAL_H

/* impl/io/io-internal.h — helpers shared between io.c and the per-
 * platform io-<os>.c slices. Not part of the yos public API; not
 * visible outside impl/io/.
 *
 * The "io" naming refers to the POSIX I/O bridges (open / read /
 * write / close + fd_map + dir + FILE* + FIFO + PTY emulation). NOT
 * the same thing as the custom VFS layer in src/yos/vfs/, which owns
 * the mount table + procfs synth backend. impl/io/io.c dispatches to
 * src/yos/vfs/ for /proc paths; the dependency goes io → vfs, never
 * the other way. */

#include "yos/types.h"
#include "platform.h"   /* yos_plat_isatty — used by the compat shim below */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

extern int32_t yos_fd_translate(struct yos_exec_ctx *ctx, int32_t fd);
extern int32_t yos_fd_get      (struct yos_exec_ctx *ctx, int32_t wfd);
extern int32_t yos_fd_alloc    (struct yos_exec_ctx *ctx, int host_fd);
extern int32_t yos_fd_close    (struct yos_exec_ctx *ctx, int32_t wfd);
extern int     yos_xlate_dfd   (struct yos_exec_ctx *ctx, int32_t wfd);
extern const char *yos_path_resolve(struct yos_exec_ctx *ctx, const char *p);
extern const char *yos_path_resolve_at(struct yos_exec_ctx *ctx, int host_dfd, const char *p);
extern void    yos_signal_pump (struct yos_exec_ctx *ctx);

struct iovec;
/* Bound on iovec entry count — matches Linux IOV_MAX. Anything beyond
 * this is treated as EINVAL. Sized so the YOS_IOV_MAX-element struct
 * iovec stack array in each readv/writev/preadv/pwritev/preadv2/
 * pwritev2/vmsplice/process_madvise/process_vm_*v caller stays at a
 * sane ~16 KiB on the host stack. */
#define YOS_IOV_MAX 1024
extern int yos_iovec_w32_to_host(struct yos_exec_ctx *ctx,
                                 uint32_t wasm_iov, int32_t iovcnt,
                                 struct iovec *host_iov);

/* wasm32 offset → host pointer. NULL offset and out-of-bounds both
 * return NULL so callers can EFAULT cleanly. */
static inline void *wptr(struct yos_exec_ctx *ctx, uint32_t offset)
{
    if (offset == 0 || offset >= ctx->memory_size) return 0;
    return ctx->memory + offset;
}

static inline const char *wstr(struct yos_exec_ctx *ctx, uint32_t offset)
{
    return (const char *)wptr(ctx, offset);
}

/* Validated [offset, offset+len) range → host pointer. Use this for any
 * buffer the host kernel/libc will read or write whose length is
 * guest-controlled — `wptr` alone validates only the first byte and
 * lets a `read(fd, p, count)` where p sits near memory_size and count
 * is huge stomp past wasm memory. End calc is 64-bit so wraparound
 * can't make the bound check pass.
 *
 * Zero-length calls (len==0) succeed even when offset==0 — POSIX
 * `read(fd, NULL, 0)` / `write(fd, NULL, 0)` etc. are defined as
 * no-ops and must not EFAULT. We return a non-NULL host pointer in
 * that case (so callers' `if (!p) return EFAULT` does not trip),
 * still validated against memory_size so an offset past the end
 * doesn't leak a wild pointer.
 *
 * For len>0, offset==0 returns NULL — that's a guest NULL pointer
 * with intent to write, which is EFAULT. */
static inline void *wptr_range(struct yos_exec_ctx *ctx, uint32_t offset, uint64_t len)
{
    if (len == 0) {
        /* `offset == memory_size` is a one-past-end pointer; legal
         * for zero-length per ISO C, and the kernel won't touch it. */
        return (offset <= ctx->memory_size) ? (ctx->memory + offset) : 0;
    }
    if (offset == 0) return 0;
    if (offset >= ctx->memory_size) return 0;
    if ((uint64_t)offset + len > (uint64_t)ctx->memory_size) return 0;
    return ctx->memory + offset;
}

/* Validate a guest NUL-terminated string. Returns the host pointer if
 * the string starts in-range AND a NUL byte is found before
 * memory_size, else NULL. Use whenever a bridge will pass the pointer
 * to host strdup/strlen/printf — without this, a non-terminated guest
 * string can make host code walk off the end of wasm memory. */
static inline const char *wstr_check(struct yos_exec_ctx *ctx, uint32_t offset)
{
    if (offset == 0 || offset >= ctx->memory_size) return 0;
    const char *p   = (const char *)(ctx->memory + offset);
    const char *end = (const char *)(ctx->memory + ctx->memory_size);
    for (const char *q = p; q < end; ++q) if (*q == 0) return p;
    return 0;
}

static inline int32_t host_fd(struct yos_exec_ctx *ctx, int32_t fd)
{
    return yos_fd_translate(ctx, fd);
}

/* ── fork/asyncify compatibility shim ─────────────────────────────────
 * NOT generic OS semantics. A forked child under yos's asyncify-based
 * fork can dribble short runs of all-0xff bytes onto a dup'd tty fd
 * (the snapshot/rewind corrupts a small scratch area — see impl/proc).
 * Those bytes are invalid UTF-8 and render as replacement glyphs,
 * breaking zsh ZLE's `\b \b` column accounting ("backspace inserts a
 * space"). Until the proc-side snapshot bug is rooted out, the write
 * bridges drop pure-0xff payloads (≤16 B) aimed at a terminal.
 *
 * Kept behind this single explicit, named gate so it reads as the
 * temporary compat shim it is, not as unconditional write semantics.
 * Pure-0xff is never a legitimate tty payload (no protocol, escape
 * sequence, or UTF-8), so this can't suppress real output. Set
 * YOS_NO_FF_DROP=1 to disable it and observe the raw corruption the
 * regression tests pin (tests/ut/libc/test_post_fork_stderr_clean). */
static inline int yos_compat_drop_fork_tty_garbage(int hfd, const void *p, size_t n)
{
    if (n == 0 || n > 16 || hfd < 0) return 0;
    if (getenv("YOS_NO_FF_DROP")) return 0;
    if (yos_plat_isatty(hfd) != 1) return 0;
    const uint8_t *bytes = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++) if (bytes[i] != 0xff) return 0;
    return 1;
}

/* ── platform-slice contract ──────────────────────────────────────── */
/* FreeBSD ↔ host ioctl request-number remap. Linux returns a table-
 * mapped Linux ioctl request; darwin returns the input unchanged
 * (BSD-lineage ioctl encoding matches FreeBSD verbatim). Defined in
 * io-linux.c / io-darwin.c respectively. */
uint32_t ioctl_cmd_fb_to_lx(uint32_t cmd);

#endif /* YOS_IO_INTERNAL_H */
