/* impl/libc/iconv.c — hand bridges for the iconv(3) trio.
 *
 * Why the codegen passthrough cannot work here:
 *
 *   - iconv()'s `char **inbuf` / `char **outbuf` carry a guest OFFSET
 *     behind the outer pointer. The generated thunk translated only the
 *     outer pointer, so host iconv dereferenced a 32-bit wasm offset as
 *     a host address — SIGSEGV inside glibc. nvim's :terminal hit this
 *     on its first typed byte (terminal input-encoding conversion).
 *   - iconv_open() returns an opaque host `iconv_t` (a pointer); the
 *     generated bridge truncated it to the guest's uint32.
 *
 * The guest instead gets a small HANDLE (ctx->iconv_slots index + 1);
 * every call resolves it to the host iconv_t. In/out buffer pointers
 * are re-based into wasm memory around the host call and written back
 * as offsets afterwards, with the size_t narrows the other thunks
 * already do.
 *
 * Handles are per-ctx, NOT inherited across fork (a host iconv_t is
 * neither copyable nor safely shareable between the parent's and
 * child's host threads); an exec or exit releases them via
 * yos_iconv_ctx_free.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <iconv.h>

#include "yos/types.h"
#include <yos/ytrace/ytrace.h>

#define YOS_ICONV_SLOTS \
    ((int)(sizeof ((struct yos_exec_ctx *)0)->iconv_slots / \
           sizeof ((struct yos_exec_ctx *)0)->iconv_slots[0]))

extern int yos_remap_errno_h2g(int);

static void set_guest_errno(struct yos_exec_ctx *ctx, int host_errno)
{
    if (ctx && ctx->memory && ctx->errno_off)
        *(int *)(ctx->memory + ctx->errno_off) = yos_remap_errno_h2g(host_errno);
}

/* Bounds-checked guest C-string fetch: returns NULL on a bad offset. */
static const char *guest_str(struct yos_exec_ctx *ctx, uint32_t off)
{
    if (off == 0 || off >= ctx->memory_size)
        return NULL;
    const char *p = (const char *)(ctx->memory + off);
    const char *end = (const char *)(ctx->memory + ctx->memory_size);
    for (const char *q = p; q < end; q++)
        if (*q == 0)
            return p;
    return NULL;
}

uint32_t yos_iconv_open(struct yos_exec_ctx *ctx, uint32_t tocode, uint32_t fromcode)
{
    const char *to = guest_str(ctx, tocode);
    const char *from = guest_str(ctx, fromcode);
    if (!to || !from) {
        set_guest_errno(ctx, EFAULT);
        return (uint32_t)-1;
    }
    int slot = -1;
    for (int i = 0; i < YOS_ICONV_SLOTS; i++) {
        if (ctx->iconv_slots[i] == NULL) { slot = i; break; }
    }
    if (slot < 0) {
        set_guest_errno(ctx, EMFILE);
        return (uint32_t)-1;
    }
    iconv_t handle = iconv_open(to, from);
    if (handle == (iconv_t)-1) {
        ydebug("iconv_open(%s, %s) failed errno=%d\n", to, from, errno);
        set_guest_errno(ctx, errno);
        return (uint32_t)-1;
    }
    ctx->iconv_slots[slot] = (void *)handle;
    ydebug("iconv_open(%s, %s) = handle %d\n", to, from, slot + 1);
    return (uint32_t)(slot + 1);
}

static iconv_t resolve(struct yos_exec_ctx *ctx, uint32_t cd)
{
    if (cd < 1 || cd > (uint32_t)YOS_ICONV_SLOTS)
        return (iconv_t)-1;
    void *handle = ctx->iconv_slots[cd - 1];
    return handle ? (iconv_t)handle : (iconv_t)-1;
}

uint32_t yos_iconv(struct yos_exec_ctx *ctx, uint32_t cd,
                   uint32_t inbufpp, uint32_t inleftp,
                   uint32_t outbufpp, uint32_t outleftp)
{
    iconv_t handle = resolve(ctx, cd);
    if (handle == (iconv_t)-1) {
        set_guest_errno(ctx, EBADF);
        return (uint32_t)-1;
    }
    /* Validate every guest pointer slot we will read/write. */
    if ((inbufpp && inbufpp + 4 > ctx->memory_size) ||
        (inleftp && inleftp + 4 > ctx->memory_size) ||
        (outbufpp && outbufpp + 4 > ctx->memory_size) ||
        (outleftp && outleftp + 4 > ctx->memory_size)) {
        set_guest_errno(ctx, EFAULT);
        return (uint32_t)-1;
    }

    char *inp = NULL, *outp = NULL;
    size_t inleft = 0, outleft = 0;
    char **inpp = NULL, **outpp = NULL;
    size_t *inleftpp = NULL, *outleftpp = NULL;
    uint32_t in_off = 0, out_off = 0;

    if (inbufpp) {
        in_off = *(uint32_t *)(ctx->memory + inbufpp);
        if (in_off >= ctx->memory_size) { set_guest_errno(ctx, EFAULT); return (uint32_t)-1; }
        inp = in_off ? (char *)(ctx->memory + in_off) : NULL;
        inpp = &inp;
    }
    if (inleftp) {
        inleft = *(uint32_t *)(ctx->memory + inleftp);
        inleftpp = &inleft;
    }
    if (outbufpp) {
        out_off = *(uint32_t *)(ctx->memory + outbufpp);
        if (out_off >= ctx->memory_size) { set_guest_errno(ctx, EFAULT); return (uint32_t)-1; }
        outp = out_off ? (char *)(ctx->memory + out_off) : NULL;
        outpp = &outp;
    }
    if (outleftp) {
        outleft = *(uint32_t *)(ctx->memory + outleftp);
        outleftpp = &outleft;
    }

    errno = 0;
    size_t converted = iconv(handle, inpp, inleftpp, outpp, outleftpp);
    int saved = errno;

    /* Write back advanced buffer positions as guest offsets + narrowed
     * byte counts, exactly the (buf**, left*) contract the guest sees. */
    if (inbufpp)
        *(uint32_t *)(ctx->memory + inbufpp) =
            inp ? (uint32_t)((uint8_t *)inp - ctx->memory) : 0;
    if (inleftp)
        *(uint32_t *)(ctx->memory + inleftp) = (uint32_t)inleft;
    if (outbufpp)
        *(uint32_t *)(ctx->memory + outbufpp) =
            outp ? (uint32_t)((uint8_t *)outp - ctx->memory) : 0;
    if (outleftp)
        *(uint32_t *)(ctx->memory + outleftp) = (uint32_t)outleft;

    if (converted == (size_t)-1) {
        set_guest_errno(ctx, saved);
        return (uint32_t)-1;
    }
    return (uint32_t)converted;
}

int32_t yos_iconv_close(struct yos_exec_ctx *ctx, uint32_t cd)
{
    if (cd < 1 || cd > (uint32_t)YOS_ICONV_SLOTS || !ctx->iconv_slots[cd - 1]) {
        set_guest_errno(ctx, EBADF);
        return -1;
    }
    iconv_close((iconv_t)ctx->iconv_slots[cd - 1]);
    ctx->iconv_slots[cd - 1] = NULL;
    return 0;
}

/* Release every open handle — called from the exit path (and safe to
 * call twice: slots are nulled as they close). */
void yos_iconv_ctx_free(struct yos_exec_ctx *ctx)
{
    if (!ctx)
        return;
    for (int i = 0; i < YOS_ICONV_SLOTS; i++) {
        if (ctx->iconv_slots[i]) {
            iconv_close((iconv_t)ctx->iconv_slots[i]);
            ctx->iconv_slots[i] = NULL;
        }
    }
}
