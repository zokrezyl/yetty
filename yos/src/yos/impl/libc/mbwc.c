/* impl/mbwc.c — multibyte/widechar conversion bridges.
 *
 * The codegen passes the guest's mbstate_t pointer straight through
 * to glibc. That doesn't work: FreeBSD-i386's mbstate_t is a 128-byte
 * union (char __mbstate8[128] | long long __mbstateL); glibc's is an
 * 8-byte struct (int __count + union __value). Whatever happens to
 * live in the first 8 bytes of the guest's slot lands in glibc's
 * __count / __wch / __wchb fields. Non-zero garbage there trips
 *   "Fatal glibc error: mbrtowc.c:102 (__mbrtowc): assertion failed:
 *    __mbsinit (data.__statep)"
 * which aborts the process — what `ls -l` was hitting once it got
 * past the per-entry stats.
 *
 * We hand-bridge the mbrtowc family here and use a host-side scratch
 * mbstate_t that we ZERO before every call. For decoding complete
 * UTF-8 characters one-at-a-time (ls, sort, find, zsh's prompt
 * rendering) the start-of-call state is always zero anyway, so the
 * functional behaviour is unchanged. The case we lose is multi-call
 * decoding of a single character whose bytes arrive split across
 * buffers — uncommon in the tools we run today, and adding a real
 * guest-pointer → host-state map can come later if a guest needs it.
 *
 * yos_mbsinit: report "initial state" (true) for every guest pointer.
 * That's a white lie for any partial sequence we lost, but for fully-
 * decoded streams it's exactly right and the only practical answer
 * given that we don't actually inspect the guest's 128-byte slot.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <wchar.h>

#include "yos/types.h"
#include <yos/ytrace/ytrace.h>
#include "impl/errno_helpers.h"

/* Helper: translate a wasm offset (or 0) to a host pointer (or NULL). */
static void *wptr_or_null(struct yos_exec_ctx *ctx, uint32_t off)
{
    if (off == 0) return NULL;
    if ((uint64_t)off >= ctx->memory_size) return NULL;
    return ctx->memory + off;
}

/* int mbsinit(const mbstate_t *ps) — nonzero if state is initial.
 * We don't track per-guest state, so always report initial; that's
 * conservative for callers that probe before continuing a sequence. */
int32_t yos_mbsinit(struct yos_exec_ctx *ctx, uint32_t ps_off)
{
    (void)ctx; (void)ps_off;
    return 1;
}

/* size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);
 *
 * Returns:
 *   0     — s pointed at the NUL byte
 *   1..n  — bytes consumed for one complete char (stored in *pwc)
 *   (size_t)-2 — incomplete sequence
 *   (size_t)-1 — invalid sequence (errno = EILSEQ) */
uint32_t yos_mbrtowc(struct yos_exec_ctx *ctx, uint32_t pwc_off,
                     uint32_t s_off, uint32_t n, uint32_t ps_off)
{
    (void)ps_off; /* see header comment — per-call scratch state */
    wchar_t *pwc = (wchar_t *)wptr_or_null(ctx, pwc_off);
    const char *s = (const char *)wptr_or_null(ctx, s_off);
    mbstate_t host_state;
    memset(&host_state, 0, sizeof host_state);
    errno = 0;
    size_t r = mbrtowc(pwc, s, (size_t)n, &host_state);
    if (r == (size_t)-1) yos_errno_neg(ctx, errno);
    return (uint32_t)r;
}

/* size_t mbsrtowcs(wchar_t *dst, const char **src, size_t len,
 *                  mbstate_t *ps);
 *
 * The src argument is a pointer-to-pointer-to-char in both ABIs but
 * with different pointer widths (4 bytes guest, 8 bytes host). We
 * walk the guest's uint32 offset, deref to a wasm offset, translate
 * to a host pointer, call glibc against a host-side `const char *`
 * scratch slot, then write back the translated wasm offset. */
uint32_t yos_mbsrtowcs(struct yos_exec_ctx *ctx, uint32_t dst_off,
                       uint32_t src_pp_off, uint32_t len, uint32_t ps_off)
{
    (void)ps_off;
    wchar_t *dst = (wchar_t *)wptr_or_null(ctx, dst_off);
    if (!src_pp_off || (uint64_t)src_pp_off + 4 > ctx->memory_size) {
        return yos_errno_null(ctx, EFAULT);
    }
    uint32_t s_off = *(uint32_t *)(ctx->memory + src_pp_off);
    const char *s_host = (const char *)wptr_or_null(ctx, s_off);
    const char *src_cursor = s_host;
    mbstate_t host_state;
    memset(&host_state, 0, sizeof host_state);
    errno = 0;
    size_t r = mbsrtowcs(dst, &src_cursor, (size_t)len, &host_state);
    if (r == (size_t)-1) yos_errno_neg(ctx, errno);
    /* Translate the advanced host pointer back to a wasm offset. NULL
     * means glibc finished the whole string (wrote a terminating L'\0'
     * and stopped); guest convention is to store NULL there too. */
    uint32_t new_off = 0;
    if (src_cursor != NULL && s_host != NULL)
        new_off = s_off + (uint32_t)(src_cursor - s_host);
    *(uint32_t *)(ctx->memory + src_pp_off) = new_off;
    return (uint32_t)r;
}

/* int mbtowc(wchar_t *pwc, const char *s, size_t n) — stateless
 * variant (uses an internal libc state we don't need to preserve). */
int32_t yos_mbtowc(struct yos_exec_ctx *ctx, uint32_t pwc_off,
                   uint32_t s_off, uint32_t n)
{
    wchar_t *pwc = (wchar_t *)wptr_or_null(ctx, pwc_off);
    const char *s = (const char *)wptr_or_null(ctx, s_off);
    errno = 0;
    int r = mbtowc(pwc, s, (size_t)n);
    if (r < 0) return yos_errno_neg(ctx, errno);
    return r;
}

/* int mblen(const char *s, size_t n). */
int32_t yos_mblen(struct yos_exec_ctx *ctx, uint32_t s_off, uint32_t n)
{
    const char *s = (const char *)wptr_or_null(ctx, s_off);
    errno = 0;
    int r = mblen(s, (size_t)n);
    if (r < 0) return yos_errno_neg(ctx, errno);
    return r;
}

/* size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps) — encode one
 * wide char into the multibyte sequence at s (up to MB_CUR_MAX bytes).
 * Same per-call zero state as mbrtowc. */
uint32_t yos_wcrtomb(struct yos_exec_ctx *ctx, uint32_t s_off,
                     int32_t wc, uint32_t ps_off)
{
    (void)ps_off;
    char *s = (char *)wptr_or_null(ctx, s_off);
    mbstate_t host_state;
    memset(&host_state, 0, sizeof host_state);
    errno = 0;
    size_t r = wcrtomb(s, (wchar_t)wc, &host_state);
    if (r == (size_t)-1) yos_errno_neg(ctx, errno);
    return (uint32_t)r;
}

/* int wctomb(char *s, wchar_t wc). */
int32_t yos_wctomb(struct yos_exec_ctx *ctx, uint32_t s_off, int32_t wc)
{
    char *s = (char *)wptr_or_null(ctx, s_off);
    errno = 0;
    int r = wctomb(s, (wchar_t)wc);
    if (r < 0) return yos_errno_neg(ctx, errno);
    return r;
}

/* size_t mbstowcs(wchar_t *dst, const char *src, size_t n) —
 * stateless variant. Hand-written here only to live alongside its
 * sibling; the codegen's auto-bridge already handles this one
 * correctly because the *_t the guest passes is a flat string, no
 * mbstate involved. Listing it under custom_ in hooks would just
 * shadow the auto-bridge; leave it out and let codegen own it. */
