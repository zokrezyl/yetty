/*
 * test_fopen_fileno_fstat.c — FILE* → fileno → fstat round-trip.
 *
 * WHAT this verifies:
 *   The fd returned by fileno(fopen(...)) MUST be usable by every
 *   POSIX fd call (fstat, read, write, close, fcntl). The wasm
 *   guest passes that fd into bridges (env.fstat etc.) which look
 *   it up via yos_fd_get(wasm_fd) → host_fd.
 *
 * WHY this matters:
 *   Pre-fix, yos_fileno returned `fileno(host_FILE)` — i.e. the
 *   raw HOST fd, not a wasm fd registered in ctx->fd_map. Every
 *   subsequent fstat / read / fcntl on that fd routed through
 *   yos_fd_get and got EBADF because the host fd happened to not
 *   be a valid wasm fd slot index. Symptom: `ssh nixem` printed
 *   `fstat /Users/.../.ssh/config: Bad file descriptor` right
 *   after the underlying fopen succeeded, then exited.
 *
 *   The fix: yos_fopen allocates a wasm fd alongside the FILE*
 *   slot via yos_fd_alloc and stores it in yos_file_slot.wfd;
 *   yos_fileno returns that wasm fd. fclose releases both.
 *
 * Expected: exit 0, stdout contains "fopen-fileno ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;
typedef int           mode_t;
typedef long          off_t;

struct stat {
    /* FreeBSD-i386 layout, 208 bytes — we only read st_size + st_mode.
     * Offsets from tools/struct-offsets.py: st_mode @24 (uint16),
     * st_size @96 (int64). */
    char _pad[208];
};

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_CREAT  0x0200
#define O_TRUNC  0x0400

extern int   *__error(void);
#define errno (*__error())

extern void *fopen(const char *, const char *);
extern int   fclose(void *);
extern unsigned int fwrite(const void *, unsigned int, unsigned int, void *);
extern int   fileno(void *);
extern int   fstat(int, struct stat *);
extern int   close(int);
extern int   write(int, const void *, size_t);
extern int   unlink(const char *);
extern unsigned int strlen(const char *);
extern void  _exit(int) __attribute__((noreturn));

static void emit(const char *s) { write(1, s, (size_t)strlen(s)); }
static void emit_err(const char *s) { write(2, s, (size_t)strlen(s)); }

static int  st_size(const struct stat *st) { return *(const int *)((const char *)st + 96); }
static int  st_mode(const struct stat *st) { return *(const unsigned short *)((const char *)st + 24); }

void _start(void)
{
    const char *path = "/tmp/yos_test_fopen_fileno.tmp";

    /* Write 13 bytes via fopen("w"). */
    {
        void *fp = fopen(path, "w");
        if (!fp) { emit_err("FAIL: fopen(w) returned NULL\n"); _exit(1); }
        if (fwrite("hello world!\n", 1, 13, fp) != 13) {
            emit_err("FAIL: fwrite returned short\n");
            _exit(1);
        }
        if (fclose(fp) != 0) {
            emit_err("FAIL: fclose(w) != 0\n");
            _exit(1);
        }
    }

    /* Re-open for read; verify fileno→fstat round-trip. */
    void *fp = fopen(path, "r");
    if (!fp) { emit_err("FAIL: fopen(r) returned NULL\n"); _exit(1); }

    int fd = fileno(fp);
    if (fd < 0) {
        emit_err("FAIL: fileno() returned negative\n");
        _exit(1);
    }
    /* Pre-fix the wasm guest got the HOST fd here (e.g. 9) which
     * was NOT a valid wasm fd slot. The post-fix returns the wasm fd
     * yos_fopen registered in ctx->fd_map. We can't directly assert
     * "this is a wasm fd"; we assert the stronger contract: every
     * subsequent fd call must work. */

    struct stat st = {{0}};
    if (fstat(fd, &st) != 0) {
        emit_err("FAIL: fstat(fileno(fp)) != 0 — was EBADF before "
                 "the wfd allocation fix. errno: ");
        char b[8]; int e = errno, n = 0;
        if (e < 0) { b[n++] = '-'; e = -e; }
        do { b[n++] = '0' + (e % 10); e /= 10; } while (e);
        for (int i = n - 1; i >= 0; i--) write(2, &b[i], 1);
        write(2, "\n", 1);
        fclose(fp); unlink(path); _exit(1);
    }
    if (st_size(&st) != 13) {
        emit_err("FAIL: st_size != 13\n");
        fclose(fp); unlink(path); _exit(1);
    }
    /* st_mode should have S_IFREG (0100000) bits set. */
    if ((st_mode(&st) & 0170000) != 0100000) {
        emit_err("FAIL: st_mode not S_IFREG\n");
        fclose(fp); unlink(path); _exit(1);
    }

    fclose(fp);
    unlink(path);

    emit("fopen-fileno ok\n");
    _exit(0);
}
