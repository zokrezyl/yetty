/* unix-process.c - POSIX impl of yplatform/process.h */

#include <yetty/yplatform/process.h>
#include <yetty/yplatform/time.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct yetty_yplatform_yprocess {
    pid_t pid;
};

struct yetty_yplatform_yprocess *yetty_yplatform_yprocess_spawn(const char *const argv[], int detached, int stdio_to_null)
{
    if (!argv || !argv[0]) {
        return YPROCESS_INVALID;
    }

    pid_t pid = fork();
    if (pid < 0) {
        return YPROCESS_INVALID;
    }

    if (pid == 0) {
        /* Child */
        if (stdio_to_null) {
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                if (devnull > STDERR_FILENO) {
                    close(devnull);
                }
            }
        }
        if (detached) {
            setsid();
        }

        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }

    struct yetty_yplatform_yprocess *p = malloc(sizeof(*p));
    if (!p) {
        /* Leaked child PID — best we can do without process control. */
        return YPROCESS_INVALID;
    }
    p->pid = pid;
    return p;
}

void yetty_yplatform_yprocess_terminate(struct yetty_yplatform_yprocess *proc, unsigned grace_ms)
{
    if (!proc) {
        return;
    }

    kill(proc->pid, SIGTERM);
    if (grace_ms > 0) {
        yetty_yplatform_ytime_sleep_ms(grace_ms);
    }

    /* If still alive, force-kill. waitpid below reaps in either case. */
    int status;
    pid_t r = waitpid(proc->pid, &status, WNOHANG);
    if (r == 0) {
        kill(proc->pid, SIGKILL);
        waitpid(proc->pid, &status, 0);
    }

    free(proc);
}

int yetty_yplatform_yprocess_is_running(struct yetty_yplatform_yprocess *proc)
{
    if (!proc) {
        return 0;
    }

    int status;
    pid_t r = waitpid(proc->pid, &status, WNOHANG);
    /* 0 means child still running; >0 means it has exited (and we just
     * reaped it); -1 is an error (treat as not running). */
    return r == 0;
}
