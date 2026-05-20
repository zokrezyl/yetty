/*
 * Default (POSIX) tty abstraction — implements
 * yetty_yplatform_tty_redirect_stderr_if_shared_with_stdout.
 *
 * See include/yetty/yplatform/tty.h for the rationale.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <yetty/yplatform/paths.h>
#include <yetty/yplatform/tty.h>

struct yetty_yplatform_tty_redirected_result
yetty_yplatform_tty_redirect_stderr_if_shared_with_stdout(const char *basename)
{
    if (!basename || !*basename) {
        return YETTY_ERR(yetty_yplatform_tty_redirected,
                         "basename is required");
    }

    /* Both fds must be ttys for the PTY-share case to apply. */
    if (!isatty(STDERR_FILENO) || !isatty(STDOUT_FILENO)) {
        return YETTY_OK(yetty_yplatform_tty_redirected, 0);
    }

    struct stat so, se;
    if (fstat(STDOUT_FILENO, &so) != 0 || fstat(STDERR_FILENO, &se) != 0) {
        /* Treat as no-op; tools shouldn't error out for a probe failure. */
        return YETTY_OK(yetty_yplatform_tty_redirected, 0);
    }
    if (so.st_dev != se.st_dev || so.st_ino != se.st_ino) {
        /* stderr already routes somewhere different — caller already
         * redirected (`2>log` from the shell), nothing to do. */
        return YETTY_OK(yetty_yplatform_tty_redirected, 0);
    }

    /* They share. Build the log path, open, dup2 it onto stderr.
     * runtime_dir is the right home for short-lived process logs
     * (XDG_RUNTIME_DIR semantics; /tmp/yetty-<uid> fallback). */
    char path[512];
    const char *runtime = yetty_yplatform_get_runtime_dir();
    snprintf(path, sizeof(path), "%s/%s-%d.log",
             runtime, basename, (int)getpid());

    FILE *log = fopen(path, "w");
    if (!log) {
        char buf[256];
        snprintf(buf, sizeof(buf), "open %s for stderr redirect: %s",
                 path, strerror(errno));
        return YETTY_ERR(yetty_yplatform_tty_redirected, buf);
    }
    setvbuf(log, NULL, _IOLBF, 0);
    int log_fd = fileno(log);
    if (log_fd < 0 || dup2(log_fd, STDERR_FILENO) < 0) {
        fclose(log);
        return YETTY_ERR(yetty_yplatform_tty_redirected,
                         "dup2 onto stderr failed");
    }
    fclose(log);

    /* Force line buffering on stderr now that it points at a real file. */
    setvbuf(stderr, NULL, _IOLBF, 0);

    return YETTY_OK(yetty_yplatform_tty_redirected, 1);
}

/*=============================================================================
 * Raw-mode stdin + stdin wait/read used by interactive ydraw producers
 * (yjungle, yzoo, ymesh, ymaze) and capture tools (decode-ydraw).
 *===========================================================================*/

static struct termios saved_tty;
static int raw_active = 0;

void yetty_yplatform_tty_binary_io(void)
{
    /* POSIX is byte-stream by default. No-op. */
}

int yetty_yplatform_tty_stdin_is_tty(void)
{
    return isatty(STDIN_FILENO);
}

int yetty_yplatform_tty_stdout_is_tty(void)
{
    return isatty(STDOUT_FILENO);
}

int yetty_yplatform_tty_set_raw(void)
{
    if (!isatty(STDIN_FILENO)) {
        return 0;
    }
    if (tcgetattr(STDIN_FILENO, &saved_tty) < 0) {
        return -1;
    }
    struct termios raw = saved_tty;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
        return -1;
    }
    raw_active = 1;
    return 0;
}

void yetty_yplatform_tty_restore(void)
{
    if (!raw_active) {
        return;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_tty);
    raw_active = 0;
}

int yetty_yplatform_tty_stdin_wait(unsigned timeout_ms)
{
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    struct timeval tv = {
        .tv_sec = (time_t)(timeout_ms / 1000u),
        .tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u),
    };
    int rdy = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
    if (rdy < 0) {
        return (errno == EINTR) ? 0 : -1;
    }
    if (rdy == 0 || !FD_ISSET(STDIN_FILENO, &rfds)) {
        return 0;
    }
    return 1;
}

int yetty_yplatform_tty_stdin_read(void *buf, size_t buf_size)
{
    for (;;) {
        ssize_t n = read(STDIN_FILENO, buf, buf_size);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        return (int)n;
    }
}
