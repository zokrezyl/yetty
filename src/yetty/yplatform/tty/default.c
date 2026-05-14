/*
 * Default (POSIX) tty abstraction — implements
 * yetty_yplatform_tty_redirect_stderr_if_shared_with_stdout.
 *
 * See include/yetty/yplatform/tty.h for the rationale.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
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
