/*
 * test_issue20_libarchive_kinds.c — libarchive handle-kind enforcement
 * and entry use-after-free protection (issue #20, finding 3).
 *
 * WHAT this verifies:
 *   archive and archive_entry handles share one table but are tagged by
 *   kind. The bridge now enforces that tag:
 *     1. archive_read_free() given an ENTRY handle is a no-op — it does
 *        NOT cast the entry pointer to struct archive * and free it
 *        (which would corrupt libarchive state). The real archive stays
 *        usable afterwards.
 *     2. After the owning archive IS freed, its entry handles are
 *        invalidated — entry APIs on a stale entry handle return empty
 *        instead of dereferencing a dangling, freed pointer.
 *
 * Expected: exit 0, stdout contains "libarchive-kinds ok".
 */

typedef unsigned int   size_t;
typedef int            ssize_t;
typedef long long      int64_t;

#define ARCHIVE_OK   0

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
__attribute__((import_module("env"), import_name("archive_read_free")))
int archive_read_free(struct archive *);
__attribute__((import_module("env"), import_name("archive_entry_pathname")))
const char *archive_entry_pathname(struct archive_entry *);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);
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

/* The guest sees handles as i32s behind opaque pointers. To pass an
 * entry handle where an archive is expected, we reinterpret the handle
 * value — exactly what a buggy or hostile guest could do. */
static struct archive *as_archive(struct archive_entry *e)
{ return (struct archive *)(void *)e; }

void _start(void)
{
    build_tar();

    struct archive *a = archive_read_new();
    if (!a) die("FAIL: archive_read_new\n");
    archive_read_support_format_all(a);
    if (archive_read_open_memory(a, tar, sizeof tar) != ARCHIVE_OK)
        die("FAIL: open_memory\n");

    struct archive_entry *e = 0;
    if (archive_read_next_header(a, &e) != ARCHIVE_OK || !e)
        die("FAIL: next_header\n");
    if (!streq(archive_entry_pathname(e), "hello.txt"))
        die("FAIL: initial pathname\n");

    /* (1) Free the ENTRY handle as if it were an archive. Kind
     * enforcement must make this a no-op: the archive is untouched and
     * the entry is still usable. A pre-fix build would free the entry
     * pointer as a struct archive * and corrupt state. */
    archive_read_free(as_archive(e));
    if (!streq(archive_entry_pathname(e), "hello.txt"))
        die("FAIL: archive_read_free(entry) was not a no-op\n");

    /* (2) Now free the archive properly. This must invalidate the entry
     * handle so a later entry API doesn't dereference the freed entry. */
    archive_read_free(a);
    const char *after = archive_entry_pathname(e);
    if (after != 0 && after[0] != 0)
        die("FAIL: entry handle still live after parent archive freed\n");

    say("libarchive-kinds ok\n");
    _exit(0);
}
