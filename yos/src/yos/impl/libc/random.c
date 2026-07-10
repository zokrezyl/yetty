/* impl/random.c — yos-side getentropy with proper EFAULT on NULL.
 *
 * The auto-generated bridge for getentropy translated wasm offset 0
 * to `ctx->memory + 0` (start of linear memory) and let host glibc
 * write a byte there — instead of returning -1+EFAULT as POSIX
 * requires. The FreeBSD `getentropy_fault` test pinned this; this
 * file restores the standard behaviour. */

#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#if defined(__APPLE__)
#  if defined(__has_include) && __has_include(<sys/random.h>)
#    include <sys/random.h>  /* getentropy lives here on macOS */
#  else
/* iPhoneSimulator SDK doesn't ship <sys/random.h>, but libSystem still
 * exports getentropy. Forward-declare so the call links. */
extern int getentropy(void *, size_t);
#  endif
#endif

#include "yos/types.h"
#include "impl/errno_helpers.h"
#include "impl/io/io-internal.h"   /* wptr_range */

int32_t yos_getentropy(struct yos_exec_ctx *ctx, uint32_t buf_off, uint32_t len)
{
    if (len > 256)
        return yos_errno_neg(ctx, EINVAL);
    /* wptr_range handles wrap-safe end calc AND the new zero-length
     * semantics: getentropy(NULL, 0) succeeds (no-op), getentropy(NULL,
     * len>0) returns EFAULT. Out-of-range / overflowing ranges all
     * land in the NULL → EFAULT branch below. */
    void *p = wptr_range(ctx, buf_off, len);
    if (!p) return yos_errno_neg(ctx, EFAULT);
    if (len == 0) return 0;
    return yos_errno_check(ctx, getentropy(p, (size_t)len));
}

/* arc4random_buf — implemented directly via getentropy(2) instead of
 * routing to host glibc's arc4random_buf. glibc's version maintains
 * a per-thread atfork-aware state; after our asyncify-fork (which
 * is a host pthread, not a real fork) the child thread's first
 * arc4random_buf call lands in glibc's atfork re-init path and
 * aborts with "Fatal glibc error: cannot get entropy for arc4random"
 * — even though host getentropy itself works fine for us. Filling
 * the buffer straight from getentropy(2), in 256-byte chunks per
 * its POSIX limit, sidesteps that entire dance and gives the FreeBSD
 * test the parent/child-differ guarantee it needs. */
void yos_arc4random_buf(struct yos_exec_ctx *ctx, uint32_t buf_off,
                        uint32_t len)
{
    if (!len) return;
    /* wptr_range does the wrap-safe 64-bit end calc. Pre-fix this used
     * `buf_off + len` in uint32 arithmetic; large buf_off + len wrapped
     * past memory_size and let getentropy scribble outside wasm
     * memory. */
    uint8_t *p = (uint8_t *)wptr_range(ctx, buf_off, len);
    if (!p) return;
    while (len > 0) {
        size_t n = len > 256 ? 256 : len;
        /* getentropy can't fail here (host always has /dev/urandom);
         * ignore returned errno and just stop on the (unreachable)
         * error path. */
        if (getentropy(p, n) != 0) return;
        p += n;
        len -= (uint32_t)n;
    }
}

/* arc4random — single uint32_t. Same rationale as arc4random_buf. */
uint32_t yos_arc4random(struct yos_exec_ctx *ctx)
{
    (void)ctx;
    uint32_t v = 0;
    (void)getentropy(&v, sizeof v);
    return v;
}

/* arc4random_uniform(upper) — returns a uniformly random value in
 * [0, upper). Bias-free rejection-sampling pattern (matches
 * FreeBSD's libc impl). */
uint32_t yos_arc4random_uniform(struct yos_exec_ctx *ctx, uint32_t upper)
{
    (void)ctx;
    if (upper < 2) return 0;
    /* Smallest threshold s.t. (UINT32_MAX - threshold) % upper == 0. */
    uint32_t min = (uint32_t)((UINT64_C(0x100000000) - upper) % upper);
    uint32_t r;
    do {
        if (getentropy(&r, sizeof r) != 0) return 0;
    } while (r < min);
    return r % upper;
}
