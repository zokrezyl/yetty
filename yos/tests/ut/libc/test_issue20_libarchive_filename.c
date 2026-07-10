/*
 * test_issue20_libarchive_filename.c — drive host libarchive through a
 * FILE-backed open that routes via yos's fd/path layer (issue #20,
 * finding 2).
 *
 * WHAT this verifies:
 *   archive_read_open_filename() no longer hands the guest path straight
 *   to host libarchive. The bridge now opens the path through yos_open
 *   (honouring ctx->cwd, the VFS mount table, fake fifo/pty routing and
 *   platform path translation) and feeds libarchive from that yos fd via
 *   archive_read_open2 callbacks — so the archive only ever sees a
 *   yos-owned fd, never a raw guest path or guest fd.
 *
 *   The guest writes a one-file ustar to /tmp through env.open/write,
 *   then reads it back by filename and checks the entry name, size and
 *   bytes. A regression to the old direct-path behaviour would still
 *   pass here (host passthrough), but the bridge change keeps this green
 *   while closing the boundary hole, and the test pins the open2/yos-fd
 *   plumbing so it can't silently break.
 *
 * Expected: exit 0, stdout contains "libarchive-filename ok".
 */

typedef unsigned int   size_t;
typedef int            ssize_t;
typedef long long      int64_t;

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_CREAT  0x0200
#define O_TRUNC  0x0400

#define ARCHIVE_OK   0
#define ARCHIVE_EOF  1

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
__attribute__((import_module("env"), import_name("archive_read_data")))
int64_t archive_read_data(struct archive *, void *, size_t);
__attribute__((import_module("env"), import_name("archive_read_free")))
int archive_read_free(struct archive *);
__attribute__((import_module("env"), import_name("archive_entry_pathname")))
const char *archive_entry_pathname(struct archive_entry *);
__attribute__((import_module("env"), import_name("archive_entry_size")))
int64_t archive_entry_size(struct archive_entry *);

__attribute__((import_module("env"), import_name("open")))
int open(const char *, int, ...);
__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);
__attribute__((import_module("env"), import_name("close")))
int close(int);
__attribute__((import_module("env"), import_name("unlink")))
int unlink(const char *);
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

/* 512 header + 512 data + 1024 trailing zero blocks. */
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
    const char *path = "/tmp/yos-issue20.tar";
    build_tar();

    /* Write the archive to a real file through the yos fd/path layer. */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) die("FAIL: open for write\n");
    size_t left = sizeof tar, off = 0;
    while (left) {
        ssize_t w = write(fd, tar + off, left);
        if (w <= 0) die("FAIL: write tar\n");
        off += (size_t)w; left -= (size_t)w;
    }
    close(fd);

    /* Read it back BY FILENAME — exercises the yos_open-backed
     * archive_read_open2 callback path. */
    struct archive *a = archive_read_new();
    if (!a) die("FAIL: archive_read_new\n");
    archive_read_support_format_all(a);
    if (archive_read_open_filename(a, path, 10240) != ARCHIVE_OK)
        die("FAIL: archive_read_open_filename\n");

    struct archive_entry *e = 0;
    if (archive_read_next_header(a, &e) != ARCHIVE_OK)
        die("FAIL: first next_header\n");
    const char *name = archive_entry_pathname(e);
    if (!name || !streq(name, "hello.txt")) die("FAIL: pathname\n");
    if (archive_entry_size(e) != 3) die("FAIL: size != 3\n");

    char buf[8];
    for (int i = 0; i < 8; i++) buf[i] = 0;
    int64_t n = archive_read_data(a, buf, sizeof buf);
    if (n != 3 || buf[0] != 'h' || buf[1] != 'i' || buf[2] != '\n')
        die("FAIL: data mismatch\n");

    if (archive_read_next_header(a, &e) != ARCHIVE_EOF)
        die("FAIL: expected EOF after one entry\n");

    archive_read_free(a);
    unlink(path);
    say("libarchive-filename ok\n");
    _exit(0);
}
