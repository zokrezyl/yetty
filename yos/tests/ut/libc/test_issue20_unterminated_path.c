/*
 * test_issue20_unterminated_path.c — hostile guest paths near the end
 * of wasm linear memory (issue #20, finding 1).
 *
 * WHAT this verifies:
 *   The path-taking bridges (open / access / stat / unlink / mkdir, …)
 *   now resolve guest strings through wstr_check(), which scans for a
 *   NUL terminator before memory_size and returns EFAULT if none is
 *   found. The old wstr() validated only the first byte, so a guest
 *   string that runs to the very end of linear memory with no NUL would
 *   make host libc (strdup/open/stat) walk off the end of wasm memory.
 *
 *   We build exactly that adversarial input: fill the last bytes of
 *   linear memory with non-NUL bytes (no terminator before the end) and
 *   hand a pointer into that region to each bridge. Each must return -1
 *   with errno == EFAULT — never crash, never read past the end.
 *
 *   The happy path is checked too: a normal NUL-terminated "/dev/null"
 *   still opens, proving wstr_check() didn't break legitimate calls.
 *
 * Expected: exit 0, stdout contains "unterminated-path ok".
 */

typedef unsigned int   size_t;
typedef int            ssize_t;

#define EFAULT   14
#define O_RDONLY 0x0000
#define F_OK     0

extern int   *__error(void);
#define errno (*__error())

extern int    open(const char *, int, ...);
extern int    close(int);
extern int    access(const char *, int);
extern int    unlink(const char *);
extern int    mkdir(const char *, int);
extern int    rmdir(const char *);
extern ssize_t write(int, const void *, size_t);
extern unsigned int strlen(const char *);
extern void   _exit(int) __attribute__((noreturn));

static void emit(const char *s) { write(1, s, (size_t)strlen(s)); }
static void emit_err(const char *s) { write(2, s, (size_t)strlen(s)); }

/* A path bridge applied to the hostile pointer must fail EFAULT. */
static int expect_efault(int rc, const char *who)
{
    if (rc != -1 || errno != EFAULT) {
        emit_err("FAIL: ");
        emit_err(who);
        emit_err(" did not EFAULT on an unterminated path\n");
        return 0;
    }
    return 1;
}

void _start(void)
{
    /* Happy path first: a proper NUL-terminated path still works. */
    int good = open("/dev/null", O_RDONLY);
    if (good < 0) { emit_err("FAIL: /dev/null open broke\n"); _exit(1); }
    close(good);

    /* Build the adversarial string: the last 256 bytes of linear memory
     * filled with a non-NUL byte, so there is NO terminator anywhere in
     * [p, memory_size). wstr_check() must scan to the end, find nothing,
     * and report EFAULT.
     *
     * Addresses are computed from the runtime memory size (a non-zero
     * offset near the top), NOT from a literal 0 base — writing through
     * a literal NULL is UB and clang at -O2 elides the stores. `volatile`
     * keeps the fill from being optimised away. */
    size_t mem_size = (size_t)__builtin_wasm_memory_size(0) * 65536u;
    volatile unsigned char *tail =
        (volatile unsigned char *)(unsigned long)(mem_size - 256);
    for (int i = 0; i < 256; i++) tail[i] = 0xAA;   /* never NUL */

    /* Pointer 16 bytes from the very end — 16 non-NUL bytes then the
     * memory boundary, no terminator. */
    const char *hostile = (const char *)(unsigned long)(mem_size - 16);

    errno = 0;
    if (!expect_efault(open(hostile, O_RDONLY), "open")) _exit(2);
    errno = 0;
    if (!expect_efault(access(hostile, F_OK), "access")) _exit(3);
    errno = 0;
    if (!expect_efault(unlink(hostile), "unlink")) _exit(4);
    errno = 0;
    if (!expect_efault(mkdir(hostile, 0755), "mkdir")) _exit(5);
    errno = 0;
    if (!expect_efault(rmdir(hostile), "rmdir")) _exit(6);

    emit("unterminated-path ok\n");
    _exit(0);
}
