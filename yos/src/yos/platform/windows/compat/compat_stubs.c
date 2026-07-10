/* Windows POSIX-shim stub bodies. Linked into yos_exe so shared
 * sources that call sigaction/sigprocmask/etc. resolve; behaviour
 * is "do nothing successfully" or -1+ENOSYS where the caller expects
 * a real outcome.
 *
 * Real Windows-flavoured replacements (Ctrl-C handler via
 * SetConsoleCtrlHandler etc.) come later. */

#include <errno.h>
#include <signal.h>
#include <sys/types.h>

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
    (void)signum; (void)act; (void)oldact;
    errno = ENOSYS;
    return -1;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    (void)how;
    if (oldset) *oldset = 0;
    (void)set;
    return 0;
}

int sigwait(const sigset_t *set, int *sig)
{
    (void)set; (void)sig;
    errno = ENOSYS;
    return ENOSYS;
}

int sigpending(sigset_t *set)
{
    if (set) *set = 0;
    return 0;
}

int sigsuspend(const sigset_t *mask)
{
    (void)mask;
    errno = EINTR;
    return -1;
}

int kill(pid_t pid, int sig)
{
    (void)pid; (void)sig;
    errno = ENOSYS;
    return -1;
}

/* sysconf lives in compat_libc.c. */
