/*
 * test_libarchive_read_memory.c — drive host libarchive through the
 * env.archive_* surface (impl/libc/libarchive.c).
 *
 * WHAT this verifies:
 *   The guest hand-builds a minimal POSIX ustar archive in its own
 *   linear memory (one regular file "hello.txt" containing "hi\n"),
 *   hands it to host libarchive via archive_read_open_memory, then
 *   walks it: archive_read_next_header -> archive_entry_pathname /
 *   archive_entry_size -> archive_read_data. It asserts the pathname,
 *   the size (3), the file bytes ("hi\n"), and that a second
 *   next_header reports end-of-archive.
 *
 * WHY this matters:
 *   It is the end-to-end proof that libarchive is correctly on the
 *   yos surface: opaque `struct archive *` / `struct archive_entry *`
 *   round-trip as i32 handles, the in-memory buffer pointer is
 *   translated into guest linear memory, a const char* return is
 *   marshalled back as a guest offset, and an int64 (entry size /
 *   bytes read) crosses the boundary intact. This is what tar's
 *   read/list/extract path is built on.
 *
 * Expected: exit 0, stdout contains "libarchive ok".
 */

typedef unsigned int   size_t;   /* wasm32 / FreeBSD-i386: 4 bytes */
typedef int            ssize_t;   /* 4 bytes — must match env.write's i32 ret */
typedef long long      int64_t;   /* 8 bytes — archive int64 returns */

/* opaque handles (i32) on the guest side */
struct archive;
struct archive_entry;

__attribute__((import_module("env"), import_name("archive_read_new")))
struct archive *archive_read_new(void);
__attribute__((import_module("env"), import_name("archive_read_support_format_all")))
int archive_read_support_format_all(struct archive *);
__attribute__((import_module("env"), import_name("archive_read_open_memory")))
int archive_read_open_memory(struct archive *, const void *, size_t);
__attribute__((import_module("env"), import_name("archive_read_next_header")))
int archive_read_next_header(struct archive *, struct archive_entry **);
__attribute__((import_module("env"), import_name("archive_read_data")))
int64_t archive_read_data(struct archive *, void *, size_t);
__attribute__((import_module("env"), import_name("archive_read_free")))
int archive_read_free(struct archive *);
__attribute__((import_module("env"), import_name("archive_entry_pathname")))
const char *archive_entry_pathname(struct archive_entry *);
__attribute__((import_module("env"), import_name("archive_entry_size")))
int64_t archive_entry_size(struct archive_entry *);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);
__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

#define ARCHIVE_OK   0
#define ARCHIVE_EOF  1

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, (size_t)slen(s)); }
static void die(const char *s) { say(s); _exit(1); }

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void zero(unsigned char *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = 0; }
static void copy(unsigned char *d, const char *s, size_t n)
{ for (size_t i = 0; i < n && s[i]; i++) d[i] = (unsigned char)s[i]; }

/* width includes the trailing NUL slot: (width-1) octal digits + NUL. */
static void octal(unsigned char *p, unsigned long v, int width)
{
    for (int i = width - 2; i >= 0; i--) { p[i] = (unsigned char)('0' + (v & 7)); v >>= 3; }
    p[width - 1] = '\0';
}

/* 2560 = 512 header + 512 data + 1024 (two zero EOF blocks). */
static unsigned char tar[2560];

static void build_tar(void)
{
    zero(tar, sizeof tar);
    unsigned char *h = tar;
    copy(h + 0,   "hello.txt", 100);   /* name      */
    octal(h + 100, 0644, 8);           /* mode      */
    octal(h + 108, 0,    8);           /* uid       */
    octal(h + 116, 0,    8);           /* gid       */
    octal(h + 124, 3,    12);          /* size = 3  */
    octal(h + 136, 0,    12);          /* mtime     */
    h[156] = '0';                      /* typeflag = regular file */
    copy(h + 257, "ustar", 6);         /* magic "ustar\0" */
    h[263] = '0'; h[264] = '0';        /* version "00" */

    /* checksum: 8 spaces, sum all 512 bytes, then 6 octal digits +
     * NUL + space. */
    for (int i = 148; i < 156; i++) h[i] = ' ';
    unsigned long sum = 0;
    for (int i = 0; i < 512; i++) sum += h[i];
    octal(h + 148, sum, 7);            /* 6 digits + NUL at h[154] */
    h[155] = ' ';

    /* file data block */
    tar[512] = 'h'; tar[513] = 'i'; tar[514] = '\n';
}

void _start(void)
{
    build_tar();

    struct archive *a = archive_read_new();
    if (!a) die("FAIL: archive_read_new returned 0\n");
    archive_read_support_format_all(a);
    if (archive_read_open_memory(a, tar, sizeof tar) != ARCHIVE_OK)
        die("FAIL: archive_read_open_memory\n");

    struct archive_entry *e = 0;
    int r = archive_read_next_header(a, &e);
    if (r != ARCHIVE_OK) die("FAIL: first next_header != OK\n");

    const char *name = archive_entry_pathname(e);
    if (!name || !streq(name, "hello.txt"))
        die("FAIL: pathname mismatch\n");

    if (archive_entry_size(e) != 3)
        die("FAIL: size != 3\n");

    char buf[8];
    for (int i = 0; i < 8; i++) buf[i] = 0;
    int64_t n = archive_read_data(a, buf, sizeof buf);
    if (n != 3 || buf[0] != 'h' || buf[1] != 'i' || buf[2] != '\n')
        die("FAIL: data mismatch\n");

    /* exactly one entry, then EOF */
    r = archive_read_next_header(a, &e);
    if (r != ARCHIVE_EOF) die("FAIL: expected EOF after one entry\n");

    archive_read_free(a);
    say("libarchive ok\n");
    _exit(0);
}
