/*
 * test_issue20_mmap_fixed.c — mmap bookkeeping edge cases (issue #20,
 * finding 4): MAP_FIXED, overlapping regions, repeated munmap, and
 * fragmented (coalesced) reuse.
 *
 * The yos mmap allocator lives in src/yos/impl/mem/mem.c: a bump
 * pointer (mmap_top) over the upper half of wasm linear memory plus a
 * best-fit free list. This test pins the hardening:
 *
 *   1. Fragmented reuse — two adjacent pages freed must coalesce into
 *      one region so a later double-size request is satisfied at the
 *      same base. Without coalescing the two single pages can't serve a
 *      two-page request and the allocator bumps to fresh space.
 *   2. Reused memory is zeroed (anonymous mmap contract; munmap zeroes).
 *   3. MAP_FIXED over a freed region carves it out of the free list, so
 *      a subsequent anonymous mmap can't hand the same bytes out again.
 *   4. MAP_FIXED that overlaps the live brk heap is refused (EINVAL),
 *      instead of silently zeroing live heap data.
 *   5. Repeated munmap of the same region does not crash.
 *
 * Expected: exit 0, stdout contains "mmap-fixed ok".
 */

typedef unsigned int   size_t;
typedef int            ssize_t;

#define PROT_RW   3            /* PROT_READ | PROT_WRITE */
#define MAP_ANON_PRIV 0x1002   /* MAP_ANON | MAP_PRIVATE (FreeBSD)      */
#define MAP_FIXED 0x10         /* same value on FreeBSD and Linux       */
#define PAGE      0x1000

extern void  *mmap(void *addr, size_t len, int prot, int flags, int fd, long long off);
extern int    munmap(void *addr, size_t len);
extern ssize_t write(int, const void *, size_t);
extern unsigned int strlen(const char *);
extern void   _exit(int) __attribute__((noreturn));

static void emit(const char *s) { write(1, s, (size_t)strlen(s)); }
static void die(const char *s) { write(2, s, (size_t)strlen(s)); _exit(1); }

/* wasm32 pointer is 32-bit; an error return is a small negative errno. */
static unsigned int off_of(void *p) { return (unsigned int)(unsigned long)p; }
static int is_err(void *p) { return (int)off_of(p) < 0; }

static void *xmap(size_t len)
{
    return mmap((void *)0, len, PROT_RW, MAP_ANON_PRIV, -1, 0);
}

void _start(void)
{
    /* ── 1+2: fragmented reuse + coalescing + zeroing ────────────────
     * Two fresh single-page allocations come back adjacent from the
     * bump allocator. Dirty them, free both, then request two pages —
     * the coalesced free region must serve it at the first page's base,
     * and the bytes must read back zero. */
    unsigned char *p1 = xmap(PAGE);
    unsigned char *p2 = xmap(PAGE);
    if (is_err(p1) || is_err(p2)) die("FAIL: initial mmap\n");
    if (off_of(p2) != off_of(p1) + PAGE)
        die("FAIL: fresh allocations not adjacent (cannot test coalescing)\n");

    for (int i = 0; i < PAGE; i++) { p1[i] = 0xCC; p2[i] = 0xCC; }

    if (munmap(p1, PAGE) != 0) die("FAIL: munmap p1\n");
    if (munmap(p2, PAGE) != 0) die("FAIL: munmap p2\n");

    unsigned char *p3 = xmap(2 * PAGE);
    if (is_err(p3)) die("FAIL: two-page mmap after free\n");
    if (off_of(p3) != off_of(p1))
        die("FAIL: freed pages did not coalesce (no reuse at base)\n");
    for (int i = 0; i < 2 * PAGE; i++)
        if (p3[i] != 0) die("FAIL: reused region not zeroed\n");

    /* ── 3: MAP_FIXED over a freed region carves the free list ───────
     * Free p3, then MAP_FIXED exactly over it. The fixed mapping must
     * take ownership; a following anonymous mmap must NOT be handed the
     * same address back. */
    if (munmap(p3, 2 * PAGE) != 0) die("FAIL: munmap p3\n");

    void *fx = mmap(p3, 2 * PAGE, PROT_RW, MAP_ANON_PRIV | MAP_FIXED, -1, 0);
    if (is_err(fx)) die("FAIL: MAP_FIXED over freed region\n");
    if (off_of(fx) != off_of(p3)) die("FAIL: MAP_FIXED returned wrong addr\n");
    /* MAP_FIXED must also zero. */
    unsigned char *fxb = fx;
    for (int i = 0; i < 2 * PAGE; i++)
        if (fxb[i] != 0) die("FAIL: MAP_FIXED region not zeroed\n");

    void *after = xmap(PAGE);
    if (is_err(after)) die("FAIL: mmap after MAP_FIXED\n");
    if (off_of(after) == off_of(p3))
        die("FAIL: carve failed — fixed range handed out again\n");

    /* ── 4: MAP_FIXED over a LIVE mapping replaces it ────────────────
     * Allocate a live region, dirty it, then MAP_FIXED over it. The
     * allocator must recognise it as live (not a hole), return the same
     * address with the bytes zeroed (replacement), and keep ownership so
     * a later anonymous mmap doesn't collide with it. */
    unsigned char *live = xmap(PAGE);
    if (is_err(live)) die("FAIL: live mmap\n");
    for (int i = 0; i < PAGE; i++) live[i] = 0xDD;
    void *fx2 = mmap(live, PAGE, PROT_RW, MAP_ANON_PRIV | MAP_FIXED, -1, 0);
    if (is_err(fx2) || off_of(fx2) != off_of(live))
        die("FAIL: MAP_FIXED over live region\n");
    for (int i = 0; i < PAGE; i++)
        if (live[i] != 0) die("FAIL: MAP_FIXED did not replace/zero live\n");
    void *after_live = xmap(PAGE);
    if (is_err(after_live) || off_of(after_live) == off_of(live))
        die("FAIL: alloc collided with live MAP_FIXED range\n");

    /* ── 5: MAP_FIXED overlapping the brk heap is refused ────────────
     * heap_end is >= 0x50000; 0x10000 is firmly inside the live heap
     * region. A fixed mapping there must fail, not clobber heap data. */
    void *bad = mmap((void *)0x10000, PAGE, PROT_RW,
                     MAP_ANON_PRIV | MAP_FIXED, -1, 0);
    if (!is_err(bad))
        die("FAIL: MAP_FIXED over heap was not refused\n");

    /* ── 6: repeated munmap is idempotent — no duplicate free region ─
     * Free `after` twice. The second free must NOT push a duplicate
     * free region; if it did, two subsequent single-page allocations
     * could both be handed `after`. Assert the two allocations are
     * distinct addresses. */
    if (munmap(after, PAGE) != 0) die("FAIL: first munmap(after)\n");
    if (munmap(after, PAGE) != 0) die("FAIL: second munmap(after)\n");
    void *r1 = xmap(PAGE);
    void *r2 = xmap(PAGE);
    if (is_err(r1) || is_err(r2)) die("FAIL: mmap after double-free\n");
    if (off_of(r1) == off_of(r2))
        die("FAIL: double-free produced a duplicate free region\n");

    emit("mmap-fixed ok\n");
    _exit(0);
}
