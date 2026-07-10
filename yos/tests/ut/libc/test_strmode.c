/*
 * test_strmode.c — FreeBSD libc strmode(3) bridge.
 *
 * WHAT this verifies:
 *   yos_strmode is hand-bridged in impl/posix.c (codegen has no
 *   equivalent — glibc doesn't ship strmode). The test passes a
 *   handful of representative mode_t values and confirms the bridge
 *   writes the canonical 11-char column ls -l shows.
 *
 * WHY this matters:
 *   strmode was previously stubbed to a no-op. Every `ls -l` line
 *   started at the link-count column with no mode info — the same
 *   class of "ls output looks corrupted on the host" the user
 *   reported. With the bridge in place the column reads back
 *   correctly across regular files, dirs, symlinks, executables, and
 *   the special bits (setuid / setgid / sticky).
 *
 * Expected: exit 0, stdout contains "strmode ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;
typedef unsigned int  mode_t;   /* FreeBSD-i386 */

/* Match the FreeBSD-shape S_IF* / S_I* macros — these are what the
 * wasm guest passes to strmode. */
#define S_IFREG   0100000
#define S_IFDIR   0040000
#define S_IFLNK   0120000
#define S_IFCHR   0020000
#define S_IFBLK   0060000
#define S_IFIFO   0010000
#define S_IFSOCK  0140000
#define S_ISUID   0004000
#define S_ISGID   0002000
#define S_ISVTX   0001000

__attribute__((import_module("env"), import_name("strmode")))
void strmode(mode_t mode, char *p);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

static int streq(const char *a, const char *b)
{
    for (int i = 0; i < 11; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* Each case: (mode, expected 11-char string). Cover every file type
 * the type-byte switch handles plus the setuid/setgid/sticky branches
 * (both with and without the matching execute bit, since the encoding
 * differs: S/T for "bit on without exec", s/t for "both on"). */
struct tc { unsigned mode; const char *want; const char *label; };

void _start(void) {
    struct tc cases[] = {
        { S_IFREG | 0644,                  "-rw-r--r-- ", "regular file 0644" },
        { S_IFREG | 0755,                  "-rwxr-xr-x ", "executable 0755" },
        { S_IFDIR | 0755,                  "drwxr-xr-x ", "dir 0755" },
        { S_IFLNK | 0777,                  "lrwxrwxrwx ", "symlink 0777" },
        { S_IFCHR | 0666,                  "crw-rw-rw- ", "char dev 0666" },
        { S_IFBLK | 0640,                  "brw-r----- ", "block dev 0640" },
        { S_IFIFO | 0600,                  "prw------- ", "fifo 0600" },
        { S_IFSOCK | 0777,                 "srwxrwxrwx ", "socket 0777" },
        { S_IFREG | S_ISUID | 0755,        "-rwsr-xr-x ", "setuid + exec" },
        { S_IFREG | S_ISUID | 0644,        "-rwSr--r-- ", "setuid no exec" },
        { S_IFREG | S_ISGID | 0755,        "-rwxr-sr-x ", "setgid + exec" },
        { S_IFREG | S_ISGID | 0644,        "-rw-r-Sr-- ", "setgid no exec" },
        { S_IFDIR | S_ISVTX | 0777,        "drwxrwxrwt ", "sticky + exec (dir)" },
        { S_IFDIR | S_ISVTX | 0664,        "drw-rw-r-T ", "sticky no exec" },
        { 0,                               "?--------- ", "unknown type (0)" },
    };

    char out[12];
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        /* Pre-fill with garbage so we catch a "writes nothing" bridge. */
        for (int j = 0; j < 11; j++) out[j] = 'X';
        out[11] = '\0';
        strmode(cases[i].mode, out);
        if (!streq(out, cases[i].want)) {
            say("strmode FAIL: ");
            say(cases[i].label);
            say(" got=\"");
            write(1, out, 11);
            say("\" want=\"");
            write(1, cases[i].want, 11);
            say("\"\n");
            _exit(1);
        }
    }
    say("strmode ok\n");
    _exit(0);
}
