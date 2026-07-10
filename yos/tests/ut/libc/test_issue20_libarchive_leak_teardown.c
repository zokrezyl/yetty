/*
 * test_issue20_libarchive_leak_teardown.c — process-exit cleanup of a
 * leaked libarchive handle (issue #20, finding 4: yos_libarchive_ctx_free
 * is now actually invoked from the runtime teardown).
 *
 * WHAT this verifies:
 *   The guest opens an archive BY FILENAME (which allocates a yos fd plus
 *   a guest-memory read scratch behind an archive_read_open2 client),
 *   reads one entry, then exits WITHOUT calling archive_read_free. The
 *   host teardown must free the leaked archive — running its close
 *   callback to release the scratch and close the yos fd — without
 *   crashing or double-freeing. A clean exit 0 is the observable signal;
 *   a teardown bug (entry freed as archive, double-free, use of already
 *   torn-down memory) would crash the host and change the exit status.
 *
 * Expected: exit 0, stdout contains "leak-teardown ok".
 */

typedef unsigned int   size_t;
typedef int            ssize_t;
typedef long long      int64_t;

#define O_WRONLY 0x0001
#define O_CREAT  0x0200
#define O_TRUNC  0x0400
#define ARCHIVE_OK 0

struct archive;
struct archive_entry;

__attribute__((import_module("env"), import_name("archive_read_new")))
struct archive *archive_read_new(void);
__attribute__((import_module("env"), import_name("archive_read_support_format_all")))
int archive_read_support_format_all(struct archive *);
__attribute__((import_module("env"), import_name("archive_read_open_filename")))
int archive_read_open_filename(struct archive *, const char *, size_t);
__attribute__((import_module("env"), import_name("archive_read_next_header")))
int archive_read_next_header(struct archive *, struct archive_entry **);
__attribute__((import_module("env"), import_name("archive_entry_pathname")))
const char *archive_entry_pathname(struct archive_entry *);

__attribute__((import_module("env"), import_name("open")))
int open(const char *, int, ...);
__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);
__attribute__((import_module("env"), import_name("close")))
int close(int);
__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, (size_t)slen(s)); }
static void die(const char *s) { say(s); _exit(1); }
static int streq(const char *a, const char *b)
{ while (*a && *a == *b) { a++; b++; } return *a == *b; }

static void zero(unsigned char *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = 0; }
static void copy(unsigned char *d, const char *s, size_t n)
{ for (size_t i = 0; i < n && s[i]; i++) d[i] = (unsigned char)s[i]; }
static void octal(unsigned char *p, unsigned long v, int width)
{ for (int i = width - 2; i >= 0; i--) { p[i] = (unsigned char)('0' + (v & 7)); v >>= 3; } p[width - 1] = '\0'; }

static unsigned char tar[2560];

static void build_tar(void)
{
    zero(tar, sizeof tar);
    unsigned char *h = tar;
    copy(h + 0,   "hello.txt", 100);
    octal(h + 100, 0644, 8);
    octal(h + 108, 0,    8);
    octal(h + 116, 0,    8);
    octal(h + 124, 3,    12);
    octal(h + 136, 0,    12);
    h[156] = '0';
    copy(h + 257, "ustar", 6);
    h[263] = '0'; h[264] = '0';
    for (int i = 148; i < 156; i++) h[i] = ' ';
    unsigned long sum = 0;
    for (int i = 0; i < 512; i++) sum += h[i];
    octal(h + 148, sum, 7);
    h[155] = ' ';
    tar[512] = 'h'; tar[513] = 'i'; tar[514] = '\n';
}

void _start(void)
{
    const char *path = "/tmp/yos-issue20-leak.tar";
    build_tar();

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) die("FAIL: open for write\n");
    size_t left = sizeof tar, off = 0;
    while (left) {
        ssize_t w = write(fd, tar + off, left);
        if (w <= 0) die("FAIL: write tar\n");
        off += (size_t)w; left -= (size_t)w;
    }
    close(fd);

    struct archive *a = archive_read_new();
    if (!a) die("FAIL: archive_read_new\n");
    archive_read_support_format_all(a);
    if (archive_read_open_filename(a, path, 10240) != ARCHIVE_OK)
        die("FAIL: open_filename\n");

    struct archive_entry *e = 0;
    if (archive_read_next_header(a, &e) != ARCHIVE_OK || !e)
        die("FAIL: next_header\n");
    if (!streq(archive_entry_pathname(e), "hello.txt"))
        die("FAIL: pathname\n");

    /* Deliberately DO NOT archive_read_free(a). The host teardown must
     * reclaim it (and its fd + scratch) cleanly when we exit. */
    say("leak-teardown ok\n");
    _exit(0);
}
