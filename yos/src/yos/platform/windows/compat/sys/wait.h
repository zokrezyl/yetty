/* <sys/wait.h> compat for Windows.
 *
 * yos's process subsystem on Windows doesn't fork host processes — it
 * runs forked guests as sibling pthreads under the same host. So
 * waitpid() against a host PID is irrelevant; the guest-side wait
 * implementation lives in impl/proc/proc.c against ctx->proc->state.
 * We still need the WIFEXITED / WEXITSTATUS macros and a waitpid()
 * declaration so the shared sources compile. */
#ifndef YOS_WIN_COMPAT_SYS_WAIT_H
#define YOS_WIN_COMPAT_SYS_WAIT_H

#include <sys/types.h>

#ifndef _PID_T_DEFINED
typedef int pid_t;
#define _PID_T_DEFINED 1
#endif

#define WNOHANG    1
#define WUNTRACED  2
#define WCONTINUED 8

#define WIFEXITED(s)     (((s) & 0x7f) == 0)
#define WEXITSTATUS(s)   (((s) >> 8) & 0xff)
#define WIFSIGNALED(s)   (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WTERMSIG(s)      ((s) & 0x7f)
#define WIFSTOPPED(s)    (((s) & 0xff) == 0x7f)
#define WSTOPSIG(s)      WEXITSTATUS(s)
#define WCOREDUMP(s)     ((s) & 0x80)
#define WIFCONTINUED(s)  ((s) == 0xffff)

static inline pid_t waitpid(pid_t pid, int *status, int options)
{
    (void)pid; (void)status; (void)options;
    return -1;
}

#endif
