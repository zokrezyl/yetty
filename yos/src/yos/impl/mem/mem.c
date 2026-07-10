#define _GNU_SOURCE
#include "yos/types.h"
#include <yos/ytrace/ytrace.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Forward declaration */
int32_t yos_mmap2(struct yos_exec_ctx *ctx, uint32_t addr, uint32_t length,
                       int32_t prot, int32_t flags, int32_t fd, uint32_t pgoffset);

/* Find best-fit free region in ctx's free list. Returns 0 if none found.
 * Caller must hold ctx->mem_lock. */
static uint32_t find_free_region(struct yos_exec_ctx *ctx, uint32_t len)
{
    int best = -1;
    uint32_t best_size = UINT32_MAX;
    for (int i = 0; i < ctx->free_count; i++) {
        if (ctx->free_list[i].len >= len && ctx->free_list[i].len < best_size) {
            best = i;
            best_size = ctx->free_list[i].len;
        }
    }
    if (best < 0) return 0;

    uint32_t addr = ctx->free_list[best].addr;
    uint32_t remaining = ctx->free_list[best].len - len;

    if (remaining >= 4096) {
        /* Split: keep remainder in free list */
        ctx->free_list[best].addr += len;
        ctx->free_list[best].len = remaining;
    } else {
        /* Remove from free list */
        ctx->free_list[best] = ctx->free_list[--ctx->free_count];
    }
    return addr;
}

/* Append a region to the free list verbatim, no coalescing. Caller must
 * hold ctx->mem_lock. */
static void free_list_push_raw(struct yos_exec_ctx *ctx, uint32_t addr, uint32_t len)
{
    if (len == 0) return;
    if (ctx->free_count >= YOS_MAX_FREE_REGIONS) {
        /* List full, drop oldest */
        ctx->free_list[0] = ctx->free_list[--ctx->free_count];
    }
    ctx->free_list[ctx->free_count].addr = addr;
    ctx->free_list[ctx->free_count].len = len;
    ctx->free_count++;
}

/* Add a region to ctx's free list, coalescing with any adjacent regions
 * first. Without coalescing, repeated map/unmap of neighbouring blocks
 * fragments the tiny wasm address space and eventually overflows the
 * fixed free_list (dropping live free space on the floor). Caller must
 * hold ctx->mem_lock. */
static void add_free_region(struct yos_exec_ctx *ctx, uint32_t addr, uint32_t len)
{
    if (len == 0) return;
    /* Merge with adjacent free regions. Repeat until no more merges so a
     * freshly-freed block can bridge two existing ones into a single
     * larger region. */
    int merged = 1;
    while (merged) {
        merged = 0;
        for (int i = 0; i < ctx->free_count; i++) {
            uint32_t region_addr = ctx->free_list[i].addr;
            uint32_t region_len  = ctx->free_list[i].len;
            if (region_addr + region_len == addr) {        /* existing ends at new */
                addr = region_addr;
                len += region_len;
            } else if (addr + len == region_addr) {        /* new ends at existing */
                len += region_len;
            } else {
                continue;
            }
            ctx->free_list[i] = ctx->free_list[--ctx->free_count];
            merged = 1;
            break;
        }
    }
    free_list_push_raw(ctx, addr, len);
}

/* Remove [addr, addr+len) from the free list, splitting any region that
 * partially overlaps so its non-overlapping remainder(s) stay free. Used
 * by MAP_FIXED so a fixed mapping that lands on previously-freed space
 * can't later be handed out a second time by find_free_region. Caller
 * must hold ctx->mem_lock. */
static void carve_free_range(struct yos_exec_ctx *ctx, uint32_t addr, uint32_t len)
{
    uint64_t end = (uint64_t)addr + len;
    for (int i = 0; i < ctx->free_count; ) {
        uint32_t region_addr = ctx->free_list[i].addr;
        uint32_t region_len  = ctx->free_list[i].len;
        uint64_t region_end  = (uint64_t)region_addr + region_len;
        if (region_end <= addr || region_addr >= end) { i++; continue; }
        /* Overlap: drop region i (swap-remove), re-add the remainders. */
        ctx->free_list[i] = ctx->free_list[--ctx->free_count];
        if (region_addr < addr)
            free_list_push_raw(ctx, region_addr, addr - region_addr);
        if (region_end > end)
            free_list_push_raw(ctx, (uint32_t)end, (uint32_t)(region_end - end));
        /* slot i now holds a swapped-in region — re-examine it */
    }
}

/* Does [addr, addr+len) touch any region currently on the free list?
 * Used to make munmap idempotent — a second munmap of an already-freed
 * range must NOT push a duplicate free region (which would let
 * find_free_region hand the same address out twice). Caller holds
 * ctx->mem_lock. */
static int range_overlaps_free(struct yos_exec_ctx *ctx, uint32_t addr, uint32_t len)
{
    uint64_t end = (uint64_t)addr + len;
    for (int i = 0; i < ctx->free_count; i++) {
        uint64_t region_addr = ctx->free_list[i].addr;
        uint64_t region_end  = region_addr + ctx->free_list[i].len;
        if (region_addr < end && addr < region_end) return 1;
    }
    return 0;
}

/* Record [addr, addr+len) as a live (owned) mapping. Best-effort: on
 * overflow the mapping just isn't tracked — no corruption. The trade-off
 * is that munmap only frees ranges it can find on the live list, so an
 * untracked-but-live mapping is NOT reclaimed by munmap; it is released
 * wholesale when the image is replaced (execve) or the proc exits. This
 * only bites past YOS_MAX_LIVE_REGIONS simultaneous mappings, and is far
 * preferable to the alternative (munmap clobbering heap/static data).
 * Caller holds mem_lock. */
static void live_add(struct yos_exec_ctx *ctx, uint32_t addr, uint32_t len)
{
    if (len == 0) return;
    if (ctx->live_count >= YOS_MAX_LIVE_REGIONS) {
        ydebug("mmap: live-region table full, not tracking 0x%x+0x%x\n", addr, len);
        return;
    }
    ctx->live_list[ctx->live_count].addr = addr;
    ctx->live_list[ctx->live_count].len = len;
    ctx->live_count++;
}

/* Drop [addr, addr+len) from the live list, splitting any region that
 * only partially overlaps so the surviving remainder(s) stay tracked.
 * Used by munmap (the range is being freed) and by MAP_FIXED (the range
 * is being re-owned, i.e. the old live mapping is replaced). Caller holds
 * mem_lock. */
static void live_remove(struct yos_exec_ctx *ctx, uint32_t addr, uint32_t len)
{
    uint64_t end = (uint64_t)addr + len;
    for (int i = 0; i < ctx->live_count; ) {
        uint32_t region_addr = ctx->live_list[i].addr;
        uint32_t region_len  = ctx->live_list[i].len;
        uint64_t region_end  = (uint64_t)region_addr + region_len;
        if (region_end <= addr || region_addr >= end) { i++; continue; }
        ctx->live_list[i] = ctx->live_list[--ctx->live_count];
        if (region_addr < addr)
            live_add(ctx, region_addr, addr - region_addr);
        if (region_end > end)
            live_add(ctx, (uint32_t)end, (uint32_t)(region_end - end));
        /* slot i now holds a swapped-in region — re-examine it */
    }
}

/* Does [addr, addr+len) touch any live mapping? Lets MAP_FIXED tell a
 * live range apart from a hole. Caller holds mem_lock. */
static int range_overlaps_live(struct yos_exec_ctx *ctx, uint32_t addr, uint32_t len)
{
    uint64_t end = (uint64_t)addr + len;
    for (int i = 0; i < ctx->live_count; i++) {
        uint64_t region_addr = ctx->live_list[i].addr;
        uint64_t region_end  = region_addr + ctx->live_list[i].len;
        if (region_addr < end && addr < region_end) return 1;
    }
    return 0;
}

int32_t yos_brk(struct yos_exec_ctx *ctx, uint32_t addr)
{
    if (addr == 0) {
        ydebug("brk(0) -> 0x%x (mem_size=0x%x)\n", ctx->heap_end, ctx->memory_size);
        return (int32_t)ctx->heap_end;
    }

    ydebug("brk(0x%x), heap_end=0x%x, mem_size=0x%x\n", addr, ctx->heap_end, ctx->memory_size);

    if (addr > ctx->memory_size) {
        ydebug("brk ENOMEM\n");
        return -ENOMEM;
    }

    /* Reserve the upper half of linear memory for anonymous mmap2 so
     * brk and mmap can never overlap. Real Linux keeps these in
     * separate VMAs; we can't (one flat wasm memory), so we keep them
     * disjoint by address. mmap2 anchors at memory_size/2 even before
     * its first call, so brk has to honor that boundary up front. */
    uint32_t mmap_anchor = ctx->mmap_top ? ctx->mmap_top : ctx->memory_size / 2;
    if (addr > mmap_anchor) {
        ydebug("brk ENOMEM (would cross mmap anchor=0x%x)\n", mmap_anchor);
        return -ENOMEM;
    }

    ctx->heap_end = addr;
    return (int32_t)addr;
}

/* mmap(addr, length, prot, flags, fd, offset) — POSIX. We support
 * anonymous mappings (fd == -1) by routing into yos_mmap2; non-
 * anonymous file mappings still return ENOSYS. The wasm-side
 * `pgoffset` stays in bytes; mmap2 ignores it for anonymous maps. */
int32_t yos_mmap(struct yos_exec_ctx *ctx, uint32_t addr, uint32_t length,
                 int32_t prot, int32_t flags, int32_t fd, uint64_t offset)
{
    (void)offset;
    return yos_mmap2(ctx, addr, length, prot, flags, fd, 0);
}

int32_t yos_munmap(struct yos_exec_ctx *ctx, uint32_t addr, uint32_t len)
{
    /* Page-align using uint64_t so a len near UINT32_MAX doesn't wrap
     * silently to 0 (or some small page) and let the addr+len check
     * pass on a bogus range. */
    uint64_t aligned = ((uint64_t)len + 4095u) & ~(uint64_t)4095u;
    if (aligned == 0 || aligned > ctx->memory_size ||
        (uint64_t)addr + aligned > ctx->memory_size) {
        ydebug("munmap(0x%x, 0x%x) -> -EINVAL (out of wasm memory 0x%x)\n",
               addr, len, (uint32_t)ctx->memory_size);
        return -EINVAL;
    }

    pthread_mutex_lock(&ctx->mem_lock);

    /* Idempotent: if the range is already (partly) on the free list this
     * is a double-unmap. Pushing it again would create a duplicate free
     * region and let find_free_region hand the same address out twice.
     * POSIX makes munmap of an unmapped range a successful no-op, so
     * return 0 without touching the lists. */
    if (range_overlaps_free(ctx, addr, (uint32_t)aligned)) {
        pthread_mutex_unlock(&ctx->mem_lock);
        ydebug("munmap(0x%x, 0x%x) -> 0 (already unmapped)\n",
               addr, (uint32_t)aligned);
        return 0;
    }

    /* Only the portions of [addr, end) that are tracked as LIVE mmap
     * regions may be zeroed and returned to the free list. A guest
     * munmap that lands on the brk heap, on static data, or on a hole
     * the bump allocator never handed out must not clobber those bytes
     * or feed them to find_free_region — that would zero live
     * heap/static data and later let mmap alias it (the mirror of the
     * MAP_FIXED-overlaps-heap guard in yos_mmap2). POSIX makes munmap of
     * an unmapped range a successful no-op, so non-live bytes are
     * silently skipped. Read the live list first (zero + free each
     * overlap; add_free_region only touches the free list, so iterating
     * live_list here is stable), then drop the whole requested span from
     * the live list in one pass — live_remove keeps any non-overlapping
     * remainders tracked. */
    uint64_t end = (uint64_t)addr + aligned;
    int freed_any = 0;
    for (int i = 0; i < ctx->live_count; i++) {
        uint32_t region_addr = ctx->live_list[i].addr;
        uint64_t region_end  = (uint64_t)region_addr + ctx->live_list[i].len;
        uint32_t overlap_lo  = region_addr > addr ? region_addr : addr;
        uint64_t overlap_hi  = region_end < end ? region_end : end;
        if (overlap_lo >= overlap_hi) continue;
        uint32_t overlap_len = (uint32_t)(overlap_hi - overlap_lo);
        memset(ctx->memory + overlap_lo, 0, overlap_len);
        add_free_region(ctx, overlap_lo, overlap_len);
        freed_any = 1;
    }
    if (freed_any) {
        live_remove(ctx, addr, (uint32_t)aligned);
        ydebug("munmap(0x%x, 0x%x) -> 0\n", addr, (uint32_t)aligned);
    } else {
        ydebug("munmap(0x%x, 0x%x) -> 0 (no live mapping; no-op)\n",
               addr, (uint32_t)aligned);
    }

    pthread_mutex_unlock(&ctx->mem_lock);

    return 0;
}

/* mprotect / mlock / mlockall etc. on wasm linear memory:
 * wasm has one flat memory; per-region permission bits don't exist in
 * the wasm execution model and forwarding to host mprotect would
 * (mis)apply to the runtime's whole memory mapping. We accept the call,
 * sanity-check the range, and trace. Callers relying on hard W^X /
 * lock-in-RAM for security MUST NOT depend on these on wasm. */

static int32_t check_range(struct yos_exec_ctx *ctx, uint32_t start, uint32_t len, const char *who)
{
    if (len == 0) return 0;
    /* 64-bit end calc — `start + len` in uint32 wraps for large inputs
     * (e.g. start=0x80000000, len=0x80000001 wraps to 1 and silently
     * "passes"). */
    if (start >= ctx->memory_size || len > ctx->memory_size ||
        (uint64_t)start + (uint64_t)len > ctx->memory_size) {
        ydebug("%s(0x%x, 0x%x) -> -ENOMEM (out of wasm memory 0x%x)\n",
               who, start, len, (uint32_t)ctx->memory_size);
        return -ENOMEM;
    }
    return 0;
}

int32_t yos_mprotect(struct yos_exec_ctx *ctx, uint32_t start, uint32_t len, int32_t prot)
{
    ydebug("mprotect(0x%x, 0x%x, 0x%x) -> 0 (advisory; wasm has no per-region prot)\n",
           start, len, prot);
    return check_range(ctx, start, len, "mprotect");
}

int32_t yos_mlock(struct yos_exec_ctx *ctx, uint32_t start, uint32_t len)
{
    ydebug("mlock(0x%x, 0x%x) -> 0 (advisory on wasm)\n", start, len);
    return check_range(ctx, start, len, "mlock");
}

int32_t yos_munlock(struct yos_exec_ctx *ctx, uint32_t start, uint32_t len)
{
    ydebug("munlock(0x%x, 0x%x) -> 0 (advisory on wasm)\n", start, len);
    return check_range(ctx, start, len, "munlock");
}

int32_t yos_mlockall(struct yos_exec_ctx *ctx, int32_t flags)
{
    (void)ctx;
    ydebug("mlockall(0x%x) -> 0 (advisory on wasm)\n", flags);
    return 0;
}

int32_t yos_munlockall(struct yos_exec_ctx *ctx)
{
    (void)ctx;
    ydebug("munlockall() -> 0 (advisory on wasm)\n");
    return 0;
}

int32_t yos_mremap(struct yos_exec_ctx *ctx, uint32_t addr, uint32_t old_len, uint32_t new_len, int32_t flags, uint32_t new_addr)
{
    ydebug("mremap(0x%x, 0x%x, 0x%x, %d, 0x%x)\n", addr, old_len, new_len, flags, new_addr);
    (void)old_len; (void)flags; (void)new_addr;
    /* For wasm, we can't really remap memory. If growing, try to allocate new space */
    if (new_len <= old_len) {
        return addr; /* shrinking - just return same address */
    }
    /* For growing, allocate new space via mmap2 and copy */
    uint32_t new_addr_out = yos_mmap2(ctx, 0, new_len, 3 /*PROT_READ|PROT_WRITE*/,
                                           0x22 /*MAP_PRIVATE|MAP_ANONYMOUS*/, -1, 0);
    if ((int32_t)new_addr_out < 0) {
        return new_addr_out; /* error */
    }
    /* Copy old data to new location */
    memcpy(ctx->memory + new_addr_out, ctx->memory + addr, old_len);
    ydebug("mremap -> 0x%x\n", new_addr_out);
    return new_addr_out;
}

/* Simple mmap2 implementation - allocates from heap space
 * mmap2(addr, length, prot, flags, fd, pgoffset)
 * For anonymous mappings (fd=-1), just allocate heap space */
#define MAP_FIXED 0x10

int32_t yos_mmap2(struct yos_exec_ctx *ctx, uint32_t addr, uint32_t length,
                       int32_t prot, int32_t flags, int32_t fd, uint32_t pgoffset)
{
    (void)prot; (void)pgoffset;

    /* only support anonymous private mappings */
    if (fd != -1 && fd != 0xffffffff) {
        ydebug("mmap2: fd=%d not supported -> ENOSYS\n", fd);
        return -ENOSYS;
    }

    /* Page-align length via uint64_t so a value near UINT32_MAX doesn't
     * wrap to 0 (or a small page) and slip past the bound checks. */
    uint64_t aligned = ((uint64_t)length + 4095u) & ~(uint64_t)4095u;
    if (aligned == 0 || aligned > ctx->memory_size) {
        ydebug("mmap2: length=0x%x -> ENOMEM (aligned 0x%lx out of range)\n",
               length, (unsigned long)aligned);
        return -ENOMEM;
    }
    length = (uint32_t)aligned;

    /* MAP_FIXED: use the requested address if it fits in memory. */
    if ((flags & MAP_FIXED) && addr != 0) {
        if ((uint64_t)addr + (uint64_t)length > ctx->memory_size) {
            ydebug("mmap2: MAP_FIXED addr=0x%x len=0x%x -> ENOMEM\n", addr, length);
            return -ENOMEM;
        }

        pthread_mutex_lock(&ctx->mem_lock);

        /* Refuse a fixed mapping that overlaps the live brk heap
         * [0, heap_end). The old code blindly zeroed the range, which
         * silently clobbered heap data the guest still owned. */
        if (addr < ctx->heap_end) {
            pthread_mutex_unlock(&ctx->mem_lock);
            ydebug("mmap2: MAP_FIXED addr=0x%x len=0x%x overlaps heap_end=0x%x -> EINVAL\n",
                   addr, length, ctx->heap_end);
            return -EINVAL;
        }

        /* Take ownership of the range. If it lands on a live mapping we
         * are replacing it (POSIX MAP_FIXED semantics) — drop the old
         * ownership; if it lands on a hole, carve it out of the free
         * list. Either way record the new live region and push mmap_top
         * past it so neither find_free_region nor the bump allocator can
         * hand the same bytes out again. */
        if (range_overlaps_live(ctx, addr, length))
            ydebug("mmap2: MAP_FIXED 0x%x+0x%x replaces a live mapping\n",
                   addr, length);
        live_remove(ctx, addr, length);
        carve_free_range(ctx, addr, length);
        live_add(ctx, addr, length);
        if (ctx->mmap_top == 0)
            ctx->mmap_top = ctx->memory_size / 2;
        uint64_t fixed_end = (uint64_t)addr + (uint64_t)length;
        if (fixed_end > ctx->mmap_top)
            ctx->mmap_top = (uint32_t)fixed_end;

        pthread_mutex_unlock(&ctx->mem_lock);

        /* anonymous mmap must return zeroed memory */
        memset(ctx->memory + addr, 0, length);
        ydebug("mmap2: MAP_FIXED addr=0x%x len=0x%x -> 0x%x\n", addr, length, addr);
        return (int32_t)addr;
    }

    pthread_mutex_lock(&ctx->mem_lock);

    /* Try to reuse a freed region first */
    uint32_t result = find_free_region(ctx, length);
    if (result) {
        live_add(ctx, result, length);
        pthread_mutex_unlock(&ctx->mem_lock);
        /* Reusing freed region - already zeroed by munmap */
        ydebug("mmap2: length=0x%x -> 0x%x (reused)\n", length, result);
        return (int32_t)result;
    }

    /* Anchor mmap2 well above the brk region so the heap can grow
     * without colliding. Without this, mmap2 returns chunks adjacent
     * to brk's heap, then a later brk grow stomps over them — silent
     * heap corruption when libuv/Lua read back garbage. */
    if (ctx->mmap_top == 0)
        ctx->mmap_top = ctx->memory_size / 2;
    if (ctx->mmap_top < ctx->heap_end) {
        ydebug("mmap2: brk grew past mmap anchor (heap_end=0x%x mmap_top=0x%x) -> ENOMEM\n",
               ctx->heap_end, ctx->mmap_top);
        pthread_mutex_unlock(&ctx->mem_lock);
        return -ENOMEM;
    }
    result = ctx->mmap_top;
    uint64_t new_end = (uint64_t)result + (uint64_t)length;

    if (new_end > ctx->memory_size) {
        pthread_mutex_unlock(&ctx->mem_lock);
        ydebug("mmap2: length=0x%x, mmap_top=0x%x -> ENOMEM\n", length, ctx->mmap_top);
        return -ENOMEM;
    }

    ctx->mmap_top = (uint32_t)new_end;
    live_add(ctx, result, length);
    pthread_mutex_unlock(&ctx->mem_lock);

    /* anonymous mmap must return zeroed memory */
    memset(ctx->memory + result, 0, length);
    ydebug("mmap2: length=0x%x -> 0x%x\n", length, result);
    return (int32_t)result;
}

int32_t yos_madvise(struct yos_exec_ctx *ctx, uint32_t start, uint32_t len, int32_t behavior)
{
    ydebug("madvise(0x%x, 0x%x, %d) -> 0 (advisory)\n", start, len, behavior);
    return check_range(ctx, start, len, "madvise");
}
