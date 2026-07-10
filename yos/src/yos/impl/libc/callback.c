/* impl/callback.c — libc fns that take a guest function-pointer
 * argument (qsort, bsearch, atexit, …).
 *
 * The auto-generated bridge can't pass these through to host libc:
 * the wasm guest passes a function-table INDEX (uint32_t), not a
 * host function pointer. Casting the index to (void *) and handing
 * it to host qsort would dereference garbage host memory.
 *
 * For each such fn we implement the algorithm host-side and invoke
 * the wasm callback via m3_Call(table0[idx], …). The callback runs
 * on the same wasm runtime that called us — no thread-spawn, no TLS
 * gymnastics. Only correctness concern: the callback can re-enter
 * yos imports (it usually only calls memcmp) which is fine because
 * wasm3's stack is re-entrant.
 *
 * Listed in hooks.yaml as `runtime_owned` so bridge.py emits no
 * conflicting body. main.c calls yos_callback_link() to bind us.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#include "wasm3.h"
#include "m3_env.h"
#include "yos/types.h"

/* Look up a wasm function by table0 index, JIT-compiling on first
 * use. Returns NULL on failure. */
static IM3Function lookup_fn(IM3Runtime rt, uint32_t idx)
{
    IM3Module mod = rt->modules;
    if (!mod || idx >= mod->table0Size) return NULL;
    IM3Function fn = mod->table0[idx];
    if (!fn) return NULL;
    if (!fn->compiled) {
        /* Indirect-call targets are JIT-compiled lazily. */
        if (CompileFunction(fn) != NULL) return NULL;
    }
    return fn;
}

/* Invoke a wasm comparator(a, b) where a/b are wasm offsets into the
 * sorted array. Returns the int result; on m3 failure, returns 0
 * (treat as "equal" — keeps the algorithm running, may produce
 * non-deterministic order but no crash). */
static int call_cmp(IM3Runtime rt, IM3Function cmp,
                    uint32_t a_off, uint32_t b_off)
{
    M3Result r = m3_CallV(cmp, a_off, b_off);
    if (r) return 0;
    int32_t out = 0;
    m3_GetResultsV(cmp, &out);
    return (int)out;
}

/* Simple insertion sort over a wasm-side array of `n` elements of
 * `size` bytes each. O(n^2) but fine for the small arrays nvim
 * sorts at startup (option lists, key tables, etc). Replace with
 * qsort/quickersort when a profile says it matters.
 *
 * Shared by qsort (void return) and the BSD mergesort/heapsort (int
 * return) — all three carry the same (base, nmemb, size, cmp-index)
 * contract and only differ in the value they hand back. `ctx->memory`
 * must already point at the current wasm memory. Silently a no-op on
 * an out-of-range/oversize array, matching qsort's original guard. */
static void yos_sort_inplace(IM3Runtime runtime, struct yos_exec_ctx *ctx,
                             uint32_t base, uint32_t n, uint32_t size,
                             uint32_t cmp_idx, uint32_t mem_size)
{
    /* Wrap-safe range check. The old form `(uint64_t)n*size > mem_size - base`
     * underflows the subtraction in unsigned 32-bit when base > mem_size,
     * yielding a huge value that passes the comparison; then `ctx->memory +
     * base` is out of bounds. Use additive form, all promoted to uint64. */
    if (!size || n < 2 || base >= mem_size ||
        (uint64_t)base + (uint64_t)n * (uint64_t)size > (uint64_t)mem_size)
        return;

    IM3Function cmp = lookup_fn(runtime, cmp_idx);
    if (!cmp) return;

    /* Scratch slot for swap, host-side. */
    uint8_t scratch[512];
    if (size > sizeof(scratch)) return; /* skip if elems too big */

    uint8_t *arr = ctx->memory + base;
    for (uint32_t i = 1; i < n; i++) {
        memcpy(scratch, arr + i * size, size);
        uint32_t j = i;
        while (j > 0) {
            uint32_t left_off  = base + (j - 1) * size;
            /* Pass the scratch's CURRENT wasm position as the b arg.
             * Since arr[j] hasn't been written yet (we lifted it into
             * scratch), use base + i*size as a stable pseudo-address —
             * the comparator reads from that offset which still holds
             * the lifted value (we haven't overwritten it yet at j=i,
             * but we may have shifted it; safer to recompute). For
             * simplicity, copy scratch back to position j first. */
            memcpy(arr + j * size, scratch, size);
            int c = call_cmp(runtime, cmp, left_off, base + j * size);
            if (c <= 0) break;
            memcpy(arr + j * size, arr + (j - 1) * size, size);
            j--;
        }
        memcpy(arr + j * size, scratch, size);
    }
}

static m3ApiRawFunction(m3_yos_qsort)
{
    m3ApiGetArg(uint32_t, base);
    m3ApiGetArg(uint32_t, n);
    m3ApiGetArg(uint32_t, size);
    m3ApiGetArg(uint32_t, cmp_idx);

    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    uint32_t mem_size = 0;
    ctx->memory = m3_GetMemory(runtime, &mem_size, 0);
    ctx->memory_size = mem_size;

    yos_sort_inplace(runtime, ctx, base, n, size, cmp_idx, mem_size);
    m3ApiSuccess();
}

/* mergesort/heapsort(base, nmemb, size, cmp) — BSD stdlib sorters with
 * the same argument shape as qsort but an `int` return (0 success,
 * -1 error). glibc ships neither, so the auto-bridge would ENOSYS-stub
 * them (return -1 without touching the array). tr's cset builder sorts
 * its character list with mergesort and then indexes it as if sorted —
 * an unsorted (or, with the stub, untouched) array walks past the end
 * and traps. Serve both with the one in-place sort qsort uses and
 * report success. */
static m3ApiRawFunction(m3_yos_bsdsort)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, base);
    m3ApiGetArg(uint32_t, n);
    m3ApiGetArg(uint32_t, size);
    m3ApiGetArg(uint32_t, cmp_idx);

    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    uint32_t mem_size = 0;
    ctx->memory = m3_GetMemory(runtime, &mem_size, 0);
    ctx->memory_size = mem_size;

    yos_sort_inplace(runtime, ctx, base, n, size, cmp_idx, mem_size);
    m3ApiReturn(0);
}

/* bsearch(key, base, n, size, cmp) — linear-walk with the wasm
 * comparator. n is small for nvim's use; OK if not literally
 * binary-search. */
static m3ApiRawFunction(m3_yos_bsearch)
{
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, key);
    m3ApiGetArg(uint32_t, base);
    m3ApiGetArg(uint32_t, n);
    m3ApiGetArg(uint32_t, size);
    m3ApiGetArg(uint32_t, cmp_idx);

    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    uint32_t mem_size = 0;
    ctx->memory = m3_GetMemory(runtime, &mem_size, 0);
    ctx->memory_size = mem_size;

    /* Same wrap-safe additive form as qsort above. Also validate the
     * `key` offset: the comparator dereferences it as a guest pointer
     * via `call_cmp`, and if key sits past mem_size we'd otherwise
     * spend the whole search comparing against wasm memory we don't
     * own. */
    if (!size || !n || base >= mem_size || key >= mem_size ||
        (uint64_t)base + (uint64_t)n * (uint64_t)size > (uint64_t)mem_size ||
        (uint64_t)key + (uint64_t)size > (uint64_t)mem_size)
        m3ApiReturn(0);

    IM3Function cmp = lookup_fn(runtime, cmp_idx);
    if (!cmp) m3ApiReturn(0);

    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        int c = call_cmp(runtime, cmp, key, base + mid * size);
        if (c == 0) m3ApiReturn(base + mid * size);
        if (c < 0) hi = mid;
        else       lo = mid + 1;
    }
    m3ApiReturn(0);
}

/* scandir(path, namelist_off, filter_idx, compar_idx) — enumerate
 * directory entries.
 *
 * The bridge can't pass `compar` and `filter` directly because they
 * are wasm function-table indices, not host function pointers. We
 * use the same lookup_fn/call_cmp machinery as qsort to dispatch.
 *
 * Result layout in wasm memory (POSIX scandir contract):
 *
 *   namelist[0] → wasm_offset → FreeBSD struct dirent { d_fileno (8),
 *                                                       d_off (8),
 *                                                       d_reclen (2),
 *                                                       d_type (1),
 *                                                       d_pad0 (1),
 *                                                       d_namlen (2),
 *                                                       d_pad1 (2),
 *                                                       d_name[N] }
 *   namelist[1] → wasm_offset → ...
 *   ...
 *
 * Each dirent is allocated via yos_malloc rounded to 8-byte alignment
 * to keep the d_fileno 64-bit-aligned. The namelist array itself is
 * also yos_malloc'd. */
extern uint32_t yos_malloc(struct yos_exec_ctx *ctx, uint32_t size);
extern void     yos_free  (struct yos_exec_ctx *ctx, uint32_t off);

/* Invoke a wasm filter(const struct dirent *) → int. On m3 failure,
 * keep the entry (return 1) — being permissive matches "we couldn't
 * call the filter; assume keep" semantics that the guest can recover
 * from, vs. silently dropping every entry. */
static int call_filter(IM3Runtime rt, IM3Function fn, uint32_t e_off)
{
    M3Result r = m3_CallV(fn, e_off);
    if (r) return 1;
    int32_t out = 0;
    m3_GetResultsV(fn, &out);
    return (int)out;
}

static m3ApiRawFunction(m3_yos_scandir)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, path_off);
    m3ApiGetArg(uint32_t, namelist_off);
    m3ApiGetArg(uint32_t, filter_idx);
    m3ApiGetArg(uint32_t, compar_idx);

    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    uint32_t mem_size = 0;
    ctx->memory = m3_GetMemory(runtime, &mem_size, 0);
    ctx->memory_size = mem_size;

    if (!path_off || path_off >= mem_size) m3ApiReturn(-1);
    const char *path = (const char *)(ctx->memory + path_off);

    /* The host's opendir/readdir gives us the directory contents in
     * host-libc dirent shape; we convert each to FreeBSD shape on
     * the wasm side. */
    extern const char *yos_path_resolve(struct yos_exec_ctx *ctx, const char *p);
    const char *resolved = yos_path_resolve(ctx, path);
    DIR *d = opendir(resolved);
    if (!d) m3ApiReturn(-1);

    IM3Function filter = filter_idx ? lookup_fn(runtime, filter_idx) : NULL;
    IM3Function compar = compar_idx ? lookup_fn(runtime, compar_idx) : NULL;

    /* Collect wasm offsets of accepted dirents. host-side dyn array. */
    uint32_t *offsets = NULL;
    size_t cap = 0, n = 0;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        size_t namlen = strlen(de->d_name);
        if (namlen > 255) namlen = 255;

        /* FreeBSD struct dirent: 24-byte header (d_fileno 8, d_off 8,
         * d_reclen 2, d_type 1, d_pad0 1, d_namlen 2, d_pad1 2) +
         * d_name[N+1] (NUL-terminated), rounded to 8-byte alignment
         * so d_fileno of the NEXT entry would stay aligned. */
        const uint32_t hdr_sz = 24;
        uint32_t need        = hdr_sz + (uint32_t)namlen + 1;
        uint32_t alloc_sz    = (need + 7u) & ~7u;
        uint32_t e_off = yos_malloc(ctx, alloc_sz);
        if (!e_off) {
            closedir(d);
            for (size_t i = 0; i < n; i++) yos_free(ctx, offsets[i]);
            free(offsets);
            m3ApiReturn(-1);
        }
        uint8_t *p = ctx->memory + e_off;
        memset(p, 0, alloc_sz);
        /* d_fileno (uint64 on FreeBSD wasm32) — host ino_t may be 64-
         * or 32-bit depending on platform; widen to 64-bit safely. */
        *(uint64_t *)(p + 0)  = (uint64_t)de->d_ino;
        /* d_off (uint64) — we don't get a portable telldir-equivalent
         * here, leave zero. The guest's only POSIX-valid use is to
         * seekdir(d, dent->d_off) which yos doesn't support anyway. */
        *(uint64_t *)(p + 8)  = 0;
        *(uint16_t *)(p + 16) = (uint16_t)alloc_sz;       /* d_reclen */
        p[18]                 = de->d_type;               /* d_type   */
        p[19]                 = 0;                        /* d_pad0   */
        *(uint16_t *)(p + 20) = (uint16_t)namlen;         /* d_namlen */
        *(uint16_t *)(p + 22) = 0;                        /* d_pad1   */
        memcpy(p + 24, de->d_name, namlen);
        p[24 + namlen] = '\0';

        /* filter(const struct dirent *) returns nonzero to keep. */
        if (filter) {
            if (!call_filter(runtime, filter, e_off)) {
                yos_free(ctx, e_off);
                continue;
            }
        }

        if (n == cap) {
            size_t newcap = cap ? cap * 2 : 16;
            uint32_t *nv = (uint32_t *)realloc(offsets, newcap * sizeof(uint32_t));
            if (!nv) {
                closedir(d);
                yos_free(ctx, e_off);
                for (size_t i = 0; i < n; i++) yos_free(ctx, offsets[i]);
                free(offsets);
                m3ApiReturn(-1);
            }
            offsets = nv;
            cap = newcap;
        }
        offsets[n++] = e_off;
    }
    closedir(d);

    /* Allocate the wasm-side namelist (array of n wasm pointers). */
    uint32_t list_off = 0;
    if (n) {
        uint32_t list_sz = (uint32_t)(n * 4);  /* wasm32 pointer = 4 B */
        list_off = yos_malloc(ctx, list_sz);
        if (!list_off) {
            for (size_t i = 0; i < n; i++) yos_free(ctx, offsets[i]);
            free(offsets);
            m3ApiReturn(-1);
        }
        for (size_t i = 0; i < n; i++) {
            *(uint32_t *)(ctx->memory + list_off + (uint32_t)i * 4u) = offsets[i];
        }
    }
    free(offsets);

    /* Sort the namelist via compar. POSIX scandir's compar takes
     * (const struct dirent **, const struct dirent **) — i.e. pointers
     * INTO the namelist array. We pass the wasm offsets of consecutive
     * slots. O(n²) insertion sort; sufficient for typical directories. */
    if (compar && n > 1) {
        for (size_t i = 1; i < n; i++) {
            for (size_t j = i; j > 0; j--) {
                uint32_t a_off = list_off + (uint32_t)(j - 1) * 4u;
                uint32_t b_off = list_off + (uint32_t)j * 4u;
                int c = call_cmp(runtime, compar, a_off, b_off);
                if (c <= 0) break;
                uint32_t av = *(uint32_t *)(ctx->memory + a_off);
                uint32_t bv = *(uint32_t *)(ctx->memory + b_off);
                *(uint32_t *)(ctx->memory + a_off) = bv;
                *(uint32_t *)(ctx->memory + b_off) = av;
            }
        }
    }

    /* Write *namelist = list_off (NULL when n == 0). */
    if (namelist_off && namelist_off + 4 <= mem_size) {
        *(uint32_t *)(ctx->memory + namelist_off) = list_off;
    }
    m3ApiReturn((int32_t)n);
}

/* atexit / __cxa_atexit / at_quick_exit — register a wasm callback
 * to run at exit. We don't actually run them (yos exits via host
 * exit() which doesn't go back into wasm). Return success silently
 * so the guest's libc startup doesn't bail. */
static m3ApiRawFunction(m3_yos_atexit_noop)
{
    m3ApiReturnType(int32_t);
    m3ApiReturn(0);
}

void yos_callback_link(IM3Module mod)
{
    m3_LinkRawFunction(mod, "env", "qsort",         "v(iiii)",   m3_yos_qsort);
    m3_LinkRawFunction(mod, "env", "mergesort",     "i(iiii)",   m3_yos_bsdsort);
    m3_LinkRawFunction(mod, "env", "heapsort",      "i(iiii)",   m3_yos_bsdsort);
    m3_LinkRawFunction(mod, "env", "bsearch",       "i(iiiii)",  m3_yos_bsearch);
    m3_LinkRawFunction(mod, "env", "scandir",       "i(iiii)",   m3_yos_scandir);
    m3_LinkRawFunction(mod, "env", "atexit",        "i(i)",      m3_yos_atexit_noop);
    m3_LinkRawFunction(mod, "env", "__cxa_atexit",  "i(iii)",    m3_yos_atexit_noop);
    m3_LinkRawFunction(mod, "env", "at_quick_exit", "i(i)",      m3_yos_atexit_noop);
}
