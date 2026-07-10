/*
 * test_munmap_heap_guard.c — munmap() must only release LIVE mmap
 * regions, never the brk heap or static data (src/yos/impl/mem/mem.c
 * yos_munmap).
 *
 * Before the fix, a guest munmap() whose range landed on static/.bss
 * or the brk heap was blindly zeroed and pushed onto the mmap free
 * list. Two things broke: live data got clobbered, and a later mmap()
 * could be handed a heap/static address out of that bogus free region
 * — aliasing memory the guest still owned. yos_munmap now zeroes and
 * frees only the portions of the range tracked as live mmap regions;
 * any non-live bytes are a POSIX no-op (return 0, no mutation). This
 * mirrors the MAP_FIXED-overlaps-heap refusal already pinned by
 * test_issue20_mmap_fixed.
 *
 * Checks:
 *   1. Fill a page-spanning static buffer, munmap() a page of it, and
 *      assert the bytes survive — munmap did not clobber static data.
 *   2. Assert a following anonymous mmap() does NOT return an address
 *      inside that static buffer — its bytes were never handed to the
 *      free list.
 *   3. Sanity: a genuine mmap()/munmap()/mmap() round-trip still reuses
 *      the freed region, so the live path is untouched.
 *
 * Expected: exit 0, stdout contains "munmap-guard ok".
 */

typedef unsigned int   size_t;
typedef int            ssize_t;

#define PROT_RW       3            /* PROT_READ | PROT_WRITE              */
#define MAP_ANON_PRIV 0x1002       /* MAP_ANON | MAP_PRIVATE (FreeBSD)    */
#define PAGE          0x1000

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

/* A static buffer lives in the guest's data/.bss segment, below
 * __heap_base (= heap_end) — i.e. firmly inside the brk-heap half of
 * linear memory, never in the mmap arena. Three pages so a page-aligned
 * window always fits inside it. */
static unsigned char heapbuf[3 * PAGE];

void _start(void)
{
    /* ── 1: munmap on static data must not zero it ───────────────────── */
    for (int i = 0; i < (int)sizeof(heapbuf); i++) heapbuf[i] = 0xAB;

    unsigned int base    = off_of(heapbuf);
    unsigned int aligned = (base + (PAGE - 1)) & ~(unsigned int)(PAGE - 1);
    /* The aligned page [aligned, aligned+PAGE) is fully inside heapbuf. */
    if (aligned + PAGE > base + sizeof(heapbuf))
        die("FAIL: aligned window escaped heapbuf\n");

    if (munmap((void *)(unsigned long)aligned, PAGE) != 0)
        die("FAIL: munmap(static) did not return 0\n");

    for (unsigned int a = aligned; a < aligned + PAGE; a++)
        if (heapbuf[a - base] != 0xAB)
            die("FAIL: munmap clobbered live static data\n");

    /* ── 2: the bogus munmap must not have donated static bytes to the
     * mmap free list — a fresh mapping must land outside heapbuf. */
    void *m = mmap((void *)0, PAGE, PROT_RW, MAP_ANON_PRIV, -1, 0);
    if (is_err(m)) die("FAIL: mmap after static munmap\n");
    unsigned int mo = off_of(m);
    if (mo + PAGE > base && mo < base + sizeof(heapbuf))
        die("FAIL: mmap aliased static-buffer memory\n");

    /* ── 3: the live mmap path is untouched — free + remap still reuses
     * the same region (anonymous mmap zeroes on reuse). */
    if (munmap(m, PAGE) != 0) die("FAIL: munmap(live) \n");
    void *m2 = mmap((void *)0, PAGE, PROT_RW, MAP_ANON_PRIV, -1, 0);
    if (is_err(m2)) die("FAIL: mmap reuse\n");
    if (off_of(m2) != mo) die("FAIL: live region was not reused\n");
    unsigned char *m2b = m2;
    for (int i = 0; i < PAGE; i++)
        if (m2b[i] != 0) die("FAIL: reused region not zeroed\n");

    emit("munmap-guard ok\n");
    _exit(0);
}
