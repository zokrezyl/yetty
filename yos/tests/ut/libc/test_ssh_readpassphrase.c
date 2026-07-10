/*
 * test_ssh_readpassphrase.c — readpassphrase(3) prompts and reads.
 *
 * WHAT this verifies:
 *   readpassphrase(prompt, buf, bufsize, RPP_STDIN) reads a line from
 *   stdin into buf and returns buf. With RPP_STDIN the bridge takes
 *   the input/output fds from the guest's stdio mapping instead of
 *   opening /dev/tty (the test harness pipes a line in, so /dev/tty
 *   isn't a real tty either — and we want to test the line-reading
 *   path without needing a real terminal).
 *
 * WHY this matters:
 *   ssh / sshd / ssh-add / ssh-keygen all call readpassphrase when
 *   they need a key passphrase or a yes/no host-key prompt. The
 *   symbol lives in OpenBSD libc; glibc doesn't have it and the
 *   host-API extractor misses the darwin version too. Without a
 *   hand-bridge the wasm module fails to load with
 *   "unresolved import env.readpassphrase" the first time ssh's
 *   verify_host_key needs user input.
 *
 * Expected: exit 0, stdout contains "readpassphrase ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

#define RPP_ECHO_OFF   0x00
#define RPP_STDIN      0x20

#define O_RDONLY       0x0000
#define O_WRONLY       0x0001

__attribute__((import_module("env"), import_name("readpassphrase")))
char *readpassphrase(const char *prompt, char *buf, size_t bufsize, int flags);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t n);

__attribute__((import_module("env"), import_name("open")))
int open(const char *path, int flags, ...);

__attribute__((import_module("env"), import_name("dup2")))
int dup2(int oldfd, int newfd);

__attribute__((import_module("env"), import_name("close")))
int close(int fd);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }
static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

void _start(void) {
    /* Pipe a known passphrase to ourselves via a tmp file. We use
     * dup2 to swap stdin to the file, then RPP_STDIN reads from it.
     * mkstemp would be nicer but pulling that in adds bridge surface;
     * a fixed path under /tmp is fine for an in-process test. */
    const char *path = "/tmp/test_ssh_readpassphrase.in";
    int wfd = open(path, O_WRONLY | 0x0200 /* O_CREAT */, 0600);
    if (wfd < 0) { say("FAIL: open(write)\n"); _exit(1); }
    static const char line[] = "topsecret\n";
    if (write(wfd, line, sizeof line - 1) != (ssize_t)(sizeof line - 1)) {
        say("FAIL: write\n"); _exit(2);
    }
    close(wfd);

    int rfd = open(path, O_RDONLY);
    if (rfd < 0) { say("FAIL: open(read)\n"); _exit(3); }
    if (dup2(rfd, 0) != 0) { say("FAIL: dup2 stdin\n"); _exit(4); }
    close(rfd);

    char buf[64];
    for (size_t i = 0; i < sizeof buf; i++) buf[i] = 'X';
    char *r = readpassphrase("passphrase: ", buf, sizeof buf,
                             RPP_ECHO_OFF | RPP_STDIN);
    if (r != buf) {
        say("FAIL: return != buf\n");
        _exit(5);
    }
    if (!streq(buf, "topsecret")) {
        say("FAIL: contents=\"");
        say(buf);
        say("\"\n");
        _exit(6);
    }

    /* Bufsize-1 truncation: pass a 3-char buffer (with room for NUL),
     * verify we get "to" + NUL (not "topsecret" overrun) and no crash. */
    int rfd2 = open(path, O_RDONLY);
    if (rfd2 < 0) { say("FAIL: open2\n"); _exit(7); }
    if (dup2(rfd2, 0) != 0) { say("FAIL: dup2 v2\n"); _exit(8); }
    close(rfd2);
    char small[3];
    small[0] = small[1] = small[2] = 'Z';
    r = readpassphrase("", small, sizeof small, RPP_STDIN);
    if (r != small || small[2] != 0 || small[0] != 't' || small[1] != 'o') {
        say("FAIL: truncation\n");
        _exit(9);
    }

    say("readpassphrase ok\n");
    _exit(0);
}
