/*
 * test_libarchive_into_fd.c — drive archive_read_data_into_fd() out to a
 * yos wasm fd (src/yos/impl/libc/libarchive.c).
 *
 * WHAT this verifies:
 *   archive_read_data_into_fd() no longer translates the guest fd to a
 *   host fd and lets host libarchive write to it behind yos's back.
 *   Instead the bridge pulls the decompressed entry bytes into a
 *   guest-memory scratch with archive_read_data() and pushes them out
 *   through yos_write() — the same yos fd model the read side already
 *   uses (virtual fds, yos_write error/short-write semantics, stderr
 *   tracking, tty/fork output filtering).
 *
 *   The guest hand-builds a one-file ustar whose payload is LARGER than
 *   the host-side 64 KiB scratch (YOS_ARC_SCRATCH_LEN), so the bridge's
 *   read-then-write loop must iterate more than once and survive short
 *   writes. It extracts the entry into a /tmp file via
 *   archive_read_data_into_fd, then reopens the file and checks every
 *   byte against the generated pattern.
 *
 * Expected: exit 0, stdout contains "libarchive-into-fd ok".
 */

typedef unsigned int   size_t;
typedef int            ssize_t;
typedef long long      int64_t;

#define O_RDONLY 0x0000
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
__attribute__((import_module("env"), import_name("archive_read_open_memory")))
int archive_read_open_memory(struct archive *, const void *, size_t);
__attribute__((import_module("env"), import_name("archive_read_next_header")))
int archive_read_next_header(struct archive *, struct archive_entry **);
__attribute__((import_module("env"), import_name("archive_read_data_into_fd")))
int64_t archive_read_data_into_fd(struct archive *, int);
__attribute__((import_module("env"), import_name("archive_read_free")))
int archive_read_free(struct archive *);
__attribute__((import_module("env"), import_name("archive_entry_size")))
int64_t archive_entry_size(struct archive_entry *);

__attribute__((import_module("env"), import_name("open")))
int open(const char *, int, ...);
__attribute__((import_module("env"), import_name("read")))
ssize_t read(int, void *, size_t);
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

static void zero(unsigned char *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = 0; }
static void copy(unsigned char *d, const char *s, size_t n)
{ for (size_t i = 0; i < n && s[i]; i++) d[i] = (unsigned char)s[i]; }
static void octal(unsigned char *p, unsigned long v, int width)
{ for (int i = width - 2; i >= 0; i--) { p[i] = (unsigned char)('0' + (v & 7)); v >>= 3; } p[width - 1] = '\0'; }

/* Deterministic payload byte for index i. Mixed so a truncated or
 * mis-ordered write is caught, not just an all-zero hole. */
static unsigned char pat(unsigned int i) { return (unsigned char)((i * 31u + 7u) & 0xffu); }

/* Payload deliberately exceeds the host bridge's 64 KiB scratch so the
 * read/write loop iterates. 70000 = 0x11170. */
#define PAYLOAD 70000u
/* 512 header + payload rounded up to 512 + 1024 trailing zero blocks. */
#define DATA_BLOCKS (((PAYLOAD + 511u) / 512u) * 512u)
#define TAR_LEN (512u + DATA_BLOCKS + 1024u)

static unsigned char tar[TAR_LEN];

static void build_tar(void)
{
    zero(tar, sizeof tar);
    unsigned char *h = tar;
    copy(h + 0,   "big.bin", 100);
    octal(h + 100, 0644, 8);
    octal(h + 108, 0,    8);
    octal(h + 116, 0,    8);
    octal(h + 124, PAYLOAD, 12);
    octal(h + 136, 0,    12);
    h[156] = '0';
    copy(h + 257, "ustar", 6);
    h[263] = '0'; h[264] = '0';
    for (int i = 148; i < 156; i++) h[i] = ' ';
    unsigned long sum = 0;
    for (int i = 0; i < 512; i++) sum += h[i];
    octal(h + 148, sum, 7);
    h[155] = ' ';
    for (unsigned int i = 0; i < PAYLOAD; i++) tar[512 + i] = pat(i);
}

void _start(void)
{
    const char *path = "/tmp/yos-into-fd.bin";
    build_tar();

    struct archive *a = archive_read_new();
    if (!a) die("FAIL: archive_read_new\n");
    archive_read_support_format_all(a);
    if (archive_read_open_memory(a, tar, sizeof tar) != ARCHIVE_OK)
        die("FAIL: archive_read_open_memory\n");

    struct archive_entry *e = 0;
    if (archive_read_next_header(a, &e) != ARCHIVE_OK)
        die("FAIL: first next_header\n");
    if (archive_entry_size(e) != (int64_t)PAYLOAD) die("FAIL: entry size\n");

    /* Extract straight into a yos-owned fd — the path under test. */
    int outfd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outfd < 0) die("FAIL: open out\n");
    if (archive_read_data_into_fd(a, outfd) != ARCHIVE_OK)
        die("FAIL: archive_read_data_into_fd\n");
    close(outfd);
    archive_read_free(a);

    /* Read it back and check every byte. */
    int infd = open(path, O_RDONLY, 0);
    if (infd < 0) die("FAIL: open in\n");
    unsigned int total = 0;
    unsigned char buf[4096];
    for (;;) {
        ssize_t n = read(infd, buf, sizeof buf);
        if (n < 0) die("FAIL: read back\n");
        if (n == 0) break;
        for (ssize_t i = 0; i < n; i++)
            if (buf[i] != pat(total + (unsigned int)i))
                die("FAIL: byte mismatch in extracted file\n");
        total += (unsigned int)n;
    }
    close(infd);
    unlink(path);

    if (total != PAYLOAD) die("FAIL: extracted size mismatch\n");

    say("libarchive-into-fd ok\n");
    _exit(0);
}
