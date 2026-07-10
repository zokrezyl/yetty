/*
 * test_ssh_sigaction_mask.c — sigaction + sigprocmask round-trip.
 *
 * WHAT this verifies:
 *   sigaction(SIGUSR1, &new, &old) installs a handler; the bridge
 *   marshals the FreeBSD 16-byte sigset_t and 12-byte sigaction
 *   struct. sigprocmask(SIG_BLOCK/SIG_UNBLOCK/SIG_SETMASK, &set,
 *   &old) maintains the per-thread mask and reads it back through
 *   the same wasm-side struct layout.
 *
 * WHY this matters:
 *   ssh wraps every channel-loop iteration in sigprocmask /
 *   sigaction / sigprocmask. If the bridge truncates the sigset_t
 *   to the host's 128-byte glibc shape (or sa_mask offset shifts on
 *   darwin vs Linux), ssh's saved-mask state corrupts and SIGPIPE
 *   delivery during the next write trips an unhandled signal trap.
 *
 * Expected: exit 0, stdout contains "ssh-sigmask ok".
 */

typedef unsigned int   size_t;
typedef int            ssize_t;

#define SIG_BLOCK    1
#define SIG_UNBLOCK  2
#define SIG_SETMASK  3

#define SIGINT       2
#define SIGUSR1      30          /* FreeBSD */
#define SIGUSR2      31          /* FreeBSD */

/* FreeBSD sigset_t — uint32_t[4] = 16 bytes. */
typedef struct { unsigned int bits[4]; } sigset_t;

/* FreeBSD sigaction (12 bytes on wasm32): handler, flags, sa_mask. */
struct sigaction {
    unsigned int sa_handler;
    int          sa_flags;
    sigset_t     sa_mask;
};

__attribute__((import_module("env"), import_name("sigaction")))
int sigaction(int signum, const struct sigaction *act, struct sigaction *oact);

__attribute__((import_module("env"), import_name("sigprocmask")))
int sigprocmask(int how, const sigset_t *set, sigset_t *oset);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t n);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

static void sigaddset(sigset_t *s, int sig)
{
    int idx = (sig - 1) / 32;
    int bit = (sig - 1) % 32;
    s->bits[idx] |= 1u << bit;
}

static int sigismember(const sigset_t *s, int sig)
{
    int idx = (sig - 1) / 32;
    int bit = (sig - 1) % 32;
    return (s->bits[idx] >> bit) & 1;
}

static void sigemptyset(sigset_t *s)
{
    for (int i = 0; i < 4; i++) s->bits[i] = 0;
}

void _start(void) {
    /* sigaction: install dummy handler for SIGUSR1, save old. */
    struct sigaction new_act, old_act;
    sigemptyset(&new_act.sa_mask);
    new_act.sa_handler = 1;          /* SIG_IGN */
    new_act.sa_flags   = 0;
    if (sigaction(SIGUSR1, &new_act, &old_act) != 0) {
        say("FAIL: sigaction install\n");
        _exit(1);
    }

    /* Restore original — bridge must read back what we just wrote. */
    if (sigaction(SIGUSR1, &old_act, 0) != 0) {
        say("FAIL: sigaction restore\n");
        _exit(2);
    }

    /* sigprocmask: block SIGUSR1, verify it's set in the new mask. */
    sigset_t block, saved, cur;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &block, &saved) != 0) {
        say("FAIL: sigprocmask BLOCK\n");
        _exit(3);
    }
    sigemptyset(&cur);
    if (sigprocmask(SIG_BLOCK, 0, &cur) != 0) {
        say("FAIL: sigprocmask read\n");
        _exit(4);
    }
    if (!sigismember(&cur, SIGUSR1)) {
        say("FAIL: SIGUSR1 not in mask after BLOCK\n");
        _exit(5);
    }

    /* UNBLOCK and confirm. */
    if (sigprocmask(SIG_UNBLOCK, &block, 0) != 0) {
        say("FAIL: sigprocmask UNBLOCK\n");
        _exit(6);
    }
    sigemptyset(&cur);
    if (sigprocmask(SIG_BLOCK, 0, &cur) != 0) {
        say("FAIL: read after UNBLOCK\n");
        _exit(7);
    }
    if (sigismember(&cur, SIGUSR1)) {
        say("FAIL: SIGUSR1 still blocked after UNBLOCK\n");
        _exit(8);
    }

    /* Restore the original mask. */
    if (sigprocmask(SIG_SETMASK, &saved, 0) != 0) {
        say("FAIL: sigprocmask SETMASK restore\n");
        _exit(9);
    }

    say("ssh-sigmask ok\n");
    _exit(0);
}
