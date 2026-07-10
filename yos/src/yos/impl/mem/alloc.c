/* alloc.c — guest-facing malloc/free/realloc/etc.
 *
 * Simple first-fit free-list allocator that lives ENTIRELY inside the
 * guest's wasm linear memory. No host-side state. No mimalloc, no
 * global registry, no per-ctx pointer-tracking the host has to clean
 * up on execve — when the guest's linear memory is replaced (execve,
 * fork-then-exec) the allocator state goes with it automatically
 * because it IS the linear memory.
 *
 * Trade-offs against mimalloc: single global lock per ctx, no thread-
 * local caches, first-fit fragmentation. For the workloads yos cares
 * about today (a handful of guest processes, modest allocation rate)
 * this is fine; if it ever becomes a bottleneck the right fix is to
 * compile mimalloc INSIDE the wasm guest (link libmimalloc.a into
 * each guest's libc as a Tier-2 libc extension) so the allocator
 * lives in the same address space it manages — not on the host side.
 *
 * Layout: [alloc_lo, alloc_hi) of ctx->memory is the heap region.
 * The first 16-byte-aligned block at alloc_lo is the initial free
 * block. Each block has an 8-byte header:
 *   offset  size  field
 *      0     4    block_size  (incl. header, 16-aligned)
 *      4     4    next        (wasm offset of next free block; only
 *                              meaningful when the block is free)
 *
 * Caller-visible pointer = block_offset + 8. Used blocks reuse the
 * `next` field as padding (the allocator doesn't read it back).
 *
 * Concurrency: a single pthread_mutex per ctx (held briefly, only
 * across list walks). Cross-thread free is fine — same lock.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include "yos/types.h"
#include <yos/ytrace/ytrace.h>
#include "alloc.h"

#define HDR_SIZE   8u
#define MIN_BLOCK  32u
#define ALIGN      16u

static inline uint32_t align_up(uint32_t x, uint32_t a) {
    return (x + (a - 1u)) & ~(a - 1u);
}

/* Per-ctx allocator lock. Kept as a static map by ctx pointer so we
 * don't pollute the ctx struct (and ctx is host-side, never frees
 * spontaneously during a guest's lifetime — same as how impl/file.c
 * keeps its FILE-table mutex). */
static pthread_mutex_t g_alloc_lock = PTHREAD_MUTEX_INITIALIZER;

static inline uint32_t blk_size(struct yos_exec_ctx *ctx, uint32_t off) {
    return *(uint32_t *)(ctx->memory + off);
}
static inline void blk_set_size(struct yos_exec_ctx *ctx, uint32_t off, uint32_t s) {
    *(uint32_t *)(ctx->memory + off) = s;
}
static inline uint32_t blk_next(struct yos_exec_ctx *ctx, uint32_t off) {
    return *(uint32_t *)(ctx->memory + off + 4u);
}
static inline void blk_set_next(struct yos_exec_ctx *ctx, uint32_t off, uint32_t n) {
    *(uint32_t *)(ctx->memory + off + 4u) = n;
}

/* Carve the heap region the first time we're called for this ctx.
 * Returns 0 on success, -1 if the layout is unworkable. Must be
 * called with g_alloc_lock held. */
static int alloc_init_locked(struct yos_exec_ctx *ctx)
{
    if (ctx->alloc_hi) return 0;
    uint32_t lo = align_up(ctx->heap_end, ALIGN);
    /* Upper half is reserved for mmap2 (impl/mem.c brk() guards on
     * memory_size/2 too). End one page below to leave guard slack. */
    uint32_t hi = (ctx->memory_size / 2u) & ~0xfffu;
    if (hi <= lo + (1u << 20)) return -1;  /* less than 1 MiB free */
    ctx->alloc_lo = lo;
    ctx->alloc_hi = hi;
    ctx->alloc_free_head = lo;
    blk_set_size(ctx, lo, hi - lo);
    blk_set_next(ctx, lo, 0);
    /* Push brk past the heap so a later brk() can't carve into us.
     * impl/mem.c also guards on memory_size/2; this is belt+braces. */
    ctx->heap_end = hi;
    ydebug("alloc_init: heap=[0x%x..0x%x) (%u MiB)\n",
           lo, hi, (hi - lo) >> 20);
    return 0;
}

/* Walk the free list and log it — diagnostic for the rare case where
 * do_alloc returns 0 (out of memory) on a heap that should have space. */
static void dump_free_list(struct yos_exec_ctx *ctx, uint32_t need)
{
    yerror("alloc OOM: need=%u head=0x%x lo=0x%x hi=0x%x",
           need, ctx->alloc_free_head, ctx->alloc_lo, ctx->alloc_hi);
    uint32_t cur = ctx->alloc_free_head;
    uint32_t n = 0;
    uint64_t total = 0;
    while (cur && n < 16u) {
        uint32_t bsz = blk_size(ctx, cur);
        yerror("  free[%u] off=0x%x size=%u next=0x%x", n, cur, bsz,
               blk_next(ctx, cur));
        total += bsz;
        cur = blk_next(ctx, cur);
        n++;
    }
    yerror("alloc OOM: walked %u blocks, %llu bytes (truncated at 16)",
           n, (unsigned long long)total);
}

static inline uint32_t do_alloc(struct yos_exec_ctx *ctx, uint32_t need)
{
    /* First-fit walk. A corrupt free list can form a cycle; bound the
     * walk so a bad node can't spin forever, and so we can report it. */
    uint32_t prev_off = 0;
    uint32_t cur = ctx->alloc_free_head;
    uint32_t guard = 0;
    while (cur) {
        if (++guard > (1u << 22)) {        /* > 4M nodes ⇒ a cycle */
            yerror("alloc: free-list cycle at off=0x%x — aborting walk", cur);
            return 0;
        }
        uint32_t bsz   = blk_size(ctx, cur);
        uint32_t bnext = blk_next(ctx, cur);
        if (bsz >= need) {
            /* Optionally split if the leftover is big enough to be
             * a meaningful block on its own. */
            uint32_t after_next = bnext;
            if (bsz >= need + MIN_BLOCK) {
                uint32_t split = cur + need;
                blk_set_size(ctx, split, bsz - need);
                blk_set_next(ctx, split, bnext);
                blk_set_size(ctx, cur, need);
                after_next = split;
            }
            /* Unlink `cur`. */
            if (prev_off) blk_set_next(ctx, prev_off, after_next);
            else          ctx->alloc_free_head = after_next;
            return cur + HDR_SIZE;
        }
        prev_off = cur;
        cur = bnext;
    }
    dump_free_list(ctx, need);
    return 0;  /* out of memory */
}

uint32_t yos_malloc(struct yos_exec_ctx *ctx, uint32_t size)
{
    if (size == 0) size = 1;
    uint32_t need = align_up(size + HDR_SIZE, ALIGN);
    if (need < MIN_BLOCK) need = MIN_BLOCK;
    pthread_mutex_lock(&g_alloc_lock);
    if (alloc_init_locked(ctx) != 0) {
        pthread_mutex_unlock(&g_alloc_lock);
        return 0;
    }
    uint32_t off = do_alloc(ctx, need);
    pthread_mutex_unlock(&g_alloc_lock);
    return off;
}

uint32_t yos_calloc(struct yos_exec_ctx *ctx, uint32_t nmemb, uint32_t size)
{
    if (nmemb && size > UINT32_MAX / nmemb) return 0;
    uint32_t total = nmemb * size;
    uint32_t off   = yos_malloc(ctx, total);
    if (!off) return 0;
    /* Zero through the END of the underlying block, not just the
     * caller-visible `total`. yos_malloc rounds up to the alignment
     * boundary (ALIGN=16) plus HDR_SIZE for the block header — any
     * slop between `total` and the block end is uninitialised heap.
     * zsh's hash-table walk on arm64 + wasm3 reads a struct field
     * past `total` and pulls in garbage; defensively zero the
     * whole block so the caller never sees uninit bytes regardless
     * of which side of the size boundary it touches. */
    uint32_t blk_off = off - HDR_SIZE;
    uint32_t bsz     = blk_size(ctx, blk_off);
    uint32_t usable  = (bsz > HDR_SIZE) ? bsz - HDR_SIZE : total;
    if (usable < total) usable = total;
    memset(ctx->memory + off, 0, usable);
    return off;
}

/* posix_memalign returns a pointer shifted past block_off + HDR_SIZE so
 * the byte at `off` satisfies the caller's (>HDR_SIZE) alignment. The
 * raw block start is stashed at off-4 so we can recover it on free /
 * realloc. Normal yos_malloc pointers have (off & 15) == 8 because
 * block_off is 16-aligned and HDR_SIZE = 8; a shifted pointer is at
 * least 16-aligned, giving (off & 15) == 0. We use that as a fast
 * discriminator, then validate the back-pointer to avoid mis-treating
 * a normal block whose stored `next` field happens to look heap-shaped.
 *
 * Returns the raw block user-pointer (suitable for yos_free's block-
 * header math) if `off` is a shifted posix_memalign pointer, else
 * returns `off` unchanged. */
static uint32_t unshift_user_off(struct yos_exec_ctx *ctx, uint32_t off)
{
    if ((off & 15u) != 0) return off;
    /* Need at least 4 bytes of back-pointer slot before off, and the
     * back-pointer itself sits inside the heap. */
    if (off < ctx->alloc_lo + 2u * HDR_SIZE) return off;
    uint32_t raw = *(uint32_t *)(ctx->memory + off - 4u);
    if (raw < ctx->alloc_lo + HDR_SIZE || raw >= off) return off;
    if (raw + HDR_SIZE > ctx->alloc_hi) return off;
    uint32_t bo = raw - HDR_SIZE;
    uint32_t bs = blk_size(ctx, bo);
    if (bs < MIN_BLOCK || bo + bs > ctx->alloc_hi) return off;
    /* `off` must lie strictly inside the block's payload range. */
    if (off >= bo + bs) return off;
    return raw;
}

void yos_free(struct yos_exec_ctx *ctx, uint32_t off)
{
    if (off == 0) return;
    if (off < ctx->alloc_lo + HDR_SIZE || off >= ctx->alloc_hi) return;
    off = unshift_user_off(ctx, off);
    uint32_t blk_off = off - HDR_SIZE;
    pthread_mutex_lock(&g_alloc_lock);
    /* Sanity-check the size field — block must fit in the heap. */
    uint32_t sz = blk_size(ctx, blk_off);
    if (sz < MIN_BLOCK || blk_off + sz > ctx->alloc_hi) {
        pthread_mutex_unlock(&g_alloc_lock);
        return;
    }
    /* Insert into the free list sorted by ascending address, coalescing
     * with any adjacent free blocks. Without this, every alloc/free
     * cycle grew the free list by one node; for tools that allocate
     * + free many small buffers (find(1) does ~5 per directory entry
     * for fts path bookkeeping), the list reached hundreds of thousands
     * of nodes after a few hundred thousand entries. First-fit walked
     * the whole list on every malloc → ~O(N²) cumulative slowdown —
     * find's throughput collapsed from ~500k entries/sec to <2k/sec
     * past the 350k-entry mark.
     *
     * Sorted insert + coalesce keeps the list to roughly the number
     * of distinct free regions (typically a few dozen for steady-
     * state workloads), so malloc stays fast indefinitely.
     *
     * Cost: O(N) walk on free to find the insertion point. Acceptable
     * because the list size now stays small. If this ever becomes a
     * bottleneck the next step is segregated lists by size class. */
    uint32_t prev = 0;
    uint32_t cur  = ctx->alloc_free_head;
    while (cur && cur < blk_off) {
        /* Double-free / free-into-a-free-region guard: if blk_off is the
         * start of, or lies inside, an already-free block, re-inserting
         * it corrupts the list (duplicate node or cycle) and silently
         * loses the rest of the heap. Detect and drop the bogus free. */
        if (blk_off < cur + blk_size(ctx, cur)) {
            ydebug("free: 0x%x already inside free block 0x%x (sz=%u) — "
                   "ignoring double free", blk_off, cur, blk_size(ctx, cur));
            pthread_mutex_unlock(&g_alloc_lock);
            return;
        }
        prev = cur;
        cur  = blk_next(ctx, cur);
    }
    /* `cur` is now the first free block with cur >= blk_off. If they are
     * equal, the block is already free (double free of a list head/node). */
    if (cur == blk_off) {
        ydebug("free: 0x%x already free (head/node) — ignoring double free",
               blk_off);
        pthread_mutex_unlock(&g_alloc_lock);
        return;
    }
    /* Try coalescing with the previous block (prev + prev->size == blk). */
    if (prev) {
        uint32_t prev_sz = blk_size(ctx, prev);
        if (prev + prev_sz == blk_off) {
            blk_set_size(ctx, prev, prev_sz + sz);
            blk_off = prev;
            sz      = prev_sz + sz;
            /* Don't double-link `blk` — it's now merged into prev. */
        } else {
            blk_set_next(ctx, prev, blk_off);
            blk_set_next(ctx, blk_off, cur);
        }
    } else {
        blk_set_next(ctx, blk_off, cur);
        ctx->alloc_free_head = blk_off;
    }
    /* Try coalescing with the next block (blk + blk->size == cur). */
    if (cur && blk_off + sz == cur) {
        uint32_t cur_sz   = blk_size(ctx, cur);
        uint32_t cur_next = blk_next(ctx, cur);
        blk_set_size(ctx, blk_off, sz + cur_sz);
        blk_set_next(ctx, blk_off, cur_next);
    }
    pthread_mutex_unlock(&g_alloc_lock);
}

uint32_t yos_realloc(struct yos_exec_ctx *ctx, uint32_t off, uint32_t newsize)
{
    if (off == 0) return yos_malloc(ctx, newsize);
    if (newsize == 0) { yos_free(ctx, off); return 0; }
    if (off < ctx->alloc_lo + HDR_SIZE || off >= ctx->alloc_hi)
        return 0;  /* Foreign pointer — refuse, same as free's bound check. */
    /* If `off` came from posix_memalign, unshift to the raw block start
     * so the size math below reads the right header. The realloc result
     * is no longer guaranteed to keep the original alignment — that's
     * C99/POSIX-conformant. */
    off = unshift_user_off(ctx, off);
    uint32_t blk_off = off - HDR_SIZE;
    uint32_t oldsz   = blk_size(ctx, blk_off);
    if (oldsz < HDR_SIZE || blk_off + oldsz > ctx->alloc_hi) return 0;
    uint32_t old_payload = oldsz - HDR_SIZE;
    if (newsize <= old_payload) return off;  /* fits in place */
    uint32_t newoff = yos_malloc(ctx, newsize);
    if (!newoff) return 0;
    memcpy(ctx->memory + newoff, ctx->memory + off, old_payload);
    yos_free(ctx, off);
    return newoff;
}

uint32_t yos_reallocarray(struct yos_exec_ctx *ctx, uint32_t off,
                          uint32_t nmemb, uint32_t size)
{
    if (nmemb && size > UINT32_MAX / nmemb) return 0;
    return yos_realloc(ctx, off, nmemb * size);
}

int32_t yos_posix_memalign(struct yos_exec_ctx *ctx, uint32_t memptr_off,
                           uint32_t alignment, uint32_t size)
{
    if (memptr_off == 0 ||
        (uint64_t)memptr_off + 4ULL > (uint64_t)ctx->memory_size)
        return EINVAL;
    /* POSIX: alignment must be a power-of-two AND a multiple of
     * sizeof(void *). On wasm32 sizeof(void *) = 4. */
    if (alignment < sizeof(uint32_t) ||
        (alignment & (alignment - 1)) != 0) return EINVAL;
    /* yos_malloc returns a pointer at (block_off + HDR_SIZE), 8-aligned.
     * If the caller asks for ≤ 8 we return it directly — no shift, free
     * works trivially. */
    if (alignment <= HDR_SIZE) {
        uint32_t off = yos_malloc(ctx, size ? size : 1);
        if (!off) return ENOMEM;
        *(uint32_t *)(ctx->memory + memptr_off) = off;
        return 0;
    }
    /* alignment > 8 → over-allocate by `alignment` bytes so we can shift
     * forward and still fit `size` payload. Write the raw user-pointer
     * at (aligned - 4) so yos_free / yos_realloc can recover it via
     * unshift_user_off(). Shift-detection in free() keys on
     * (off & 15) == 0; alignment > 8 + power-of-two-and-multiple-of-4
     * → alignment ≥ 16, so aligned is always 16-aligned. */
    uint64_t need = (uint64_t)size + (uint64_t)alignment;
    if (need == 0) need = 1;
    if (need > (uint64_t)UINT32_MAX) return ENOMEM;
    uint32_t raw = yos_malloc(ctx, (uint32_t)need);
    if (!raw) return ENOMEM;
    uint32_t aligned = (raw + alignment - 1u) & ~(alignment - 1u);
    /* Ensure we have at least 4 bytes of back-pointer slot between raw
     * and aligned. If raw is already alignment-aligned (so aligned ==
     * raw), bump forward by `alignment` bytes — we over-allocated for
     * exactly this case. */
    if (aligned == raw) aligned = raw + alignment;
    *(uint32_t *)(ctx->memory + aligned - 4u) = raw;
    *(uint32_t *)(ctx->memory + memptr_off) = aligned;
    return 0;
}

uint32_t yos_aligned_alloc(struct yos_exec_ctx *ctx, uint32_t alignment,
                           uint32_t size)
{
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) return 0;
    if (alignment > ALIGN) return 0;  /* see yos_posix_memalign */
    return yos_malloc(ctx, size);
}

uint32_t yos_memalign(struct yos_exec_ctx *ctx, uint32_t alignment,
                      uint32_t size)
{
    return yos_aligned_alloc(ctx, alignment, size);
}

uint32_t yos_valloc(struct yos_exec_ctx *ctx, uint32_t size)
{
    /* page-aligned; wasm page is 64 KiB but most callers expect
     * host-page-aligned, and our allocator only guarantees 16. Just
     * round size up to a multiple of 16 and return — callers that
     * actually depend on 4-KiB alignment will fail elsewhere first. */
    return yos_malloc(ctx, size);
}

uint32_t yos_malloc_usable_size(struct yos_exec_ctx *ctx, uint32_t off)
{
    if (off == 0) return 0;
    if (off < ctx->alloc_lo + HDR_SIZE || off >= ctx->alloc_hi) return 0;
    uint32_t blk_off = off - HDR_SIZE;
    uint32_t sz = blk_size(ctx, blk_off);
    if (sz < HDR_SIZE) return 0;
    return sz - HDR_SIZE;
}
