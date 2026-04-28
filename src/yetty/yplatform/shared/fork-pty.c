/* Fork PTY - PTY using forkpty(), shared by Linux and macOS */

#include "unix-pty.h"

#include <yetty/platform/pty.h>
#include <yetty/yconfig.h>
#include <yetty/ycore/types.h>

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

/* Fork PTY implementation */
struct fork_pty {
    struct yetty_yplatform_pty base;
    struct yetty_yplatform_pty_pipe_source pipe_source;
    int pty_master;
    pid_t child_pid;
    uint32_t cols;
    uint32_t rows;
    int running;
};

/* Forward declarations */
static void fork_pty_destroy(struct yetty_yplatform_pty *self);
static struct yetty_ycore_size_result fork_pty_read(struct yetty_yplatform_pty *self, char *buf, size_t max_len);
static struct yetty_ycore_size_result fork_pty_write(struct yetty_yplatform_pty *self, const char *data, size_t len);
static struct yetty_ycore_void_result fork_pty_resize(struct yetty_yplatform_pty *self, uint32_t cols, uint32_t rows);
static struct yetty_ycore_void_result fork_pty_stop(struct yetty_yplatform_pty *self);
static struct yetty_yplatform_pty_pipe_source *fork_pty_pipe_source(struct yetty_yplatform_pty *self);

/* Ops table */
static const struct yetty_yplatform_pty_ops fork_pty_ops = {
    .destroy = fork_pty_destroy,
    .read = fork_pty_read,
    .write = fork_pty_write,
    .resize = fork_pty_resize,
    .stop = fork_pty_stop,
    .pipe_source = fork_pty_pipe_source,
};

static void fork_pty_destroy(struct yetty_yplatform_pty *self)
{
    struct fork_pty *pty = container_of(self, struct fork_pty, base);

    fork_pty_stop(self);
    free(pty);
}

static struct yetty_ycore_size_result fork_pty_read(struct yetty_yplatform_pty *self, char *buf, size_t max_len)
{
    struct fork_pty *pty = container_of(self, struct fork_pty, base);
    ssize_t n;

    if (pty->pty_master < 0)
        return YETTY_ERR(yetty_ycore_size, "pty master not open");

    n = read(pty->pty_master, buf, max_len);
    if (n < 0)
        return YETTY_ERR(yetty_ycore_size, "read from pty failed");

    return YETTY_OK(yetty_ycore_size, (size_t)n);
}

static struct yetty_ycore_size_result fork_pty_write(struct yetty_yplatform_pty *self, const char *data, size_t len)
{
    struct fork_pty *pty = container_of(self, struct fork_pty, base);
    ssize_t written;

    if (pty->pty_master < 0)
        return YETTY_ERR(yetty_ycore_size, "pty master not open");

    if (len == 0)
        return YETTY_OK(yetty_ycore_size, 0);

    written = write(pty->pty_master, data, len);
    if (written < 0)
        return YETTY_ERR(yetty_ycore_size, "write to pty failed");

    return YETTY_OK(yetty_ycore_size, (size_t)written);
}

static struct yetty_ycore_void_result fork_pty_resize(struct yetty_yplatform_pty *self, uint32_t cols, uint32_t rows)
{
    struct fork_pty *pty = container_of(self, struct fork_pty, base);
    struct winsize ws;

    pty->cols = cols;
    pty->rows = rows;

    if (pty->pty_master < 0)
        return YETTY_ERR(yetty_ycore_void, "pty master not open");

    ws.ws_row = (unsigned short)rows;
    ws.ws_col = (unsigned short)cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    if (ioctl(pty->pty_master, TIOCSWINSZ, &ws) < 0)
        return YETTY_ERR(yetty_ycore_void, "ioctl TIOCSWINSZ failed");

    return YETTY_OK_VOID();
}

/* Poll waitpid(WNOHANG) until the child is reaped or the deadline passes.
 * Returns 1 if reaped, 0 if timed out, -1 on error. */
static int fork_pty_wait_ms(pid_t pid, int *status, int timeout_ms)
{
    int waited = 0;
    while (waited < timeout_ms) {
        pid_t r = waitpid(pid, status, WNOHANG);
        if (r == pid)
            return 1;
        if (r < 0)
            return -1;
        usleep(10 * 1000);
        waited += 10;
    }
    return 0;
}

static struct yetty_ycore_void_result fork_pty_stop(struct yetty_yplatform_pty *self)
{
    struct fork_pty *pty = container_of(self, struct fork_pty, base);
    int status;

    if (!pty->running)
        return YETTY_OK_VOID();

    pty->running = 0;

    /* Closing the master end disconnects the slave: the kernel sends SIGHUP
     * to the foreground process group via the controlling-terminal mechanism,
     * which is the canonical "the terminal went away" signal. Most shells
     * respond by exiting cleanly. */
    if (pty->pty_master >= 0) {
        close(pty->pty_master);
        pty->pty_master = -1;
    }

    if (pty->child_pid > 0) {
        /* Belt-and-suspenders: explicit SIGHUP in case master-close didn't
         * propagate (e.g. if the child detached from its controlling tty).
         * Then escalate: SIGHUP → 200ms grace → SIGTERM → 200ms → SIGKILL.
         * The escalation matters: a hung child must not block window close. */
        kill(pty->child_pid, SIGHUP);
        if (fork_pty_wait_ms(pty->child_pid, &status, 200) <= 0) {
            kill(pty->child_pid, SIGTERM);
            if (fork_pty_wait_ms(pty->child_pid, &status, 200) <= 0) {
                kill(pty->child_pid, SIGKILL);
                waitpid(pty->child_pid, &status, 0);
            }
        }
        pty->child_pid = -1;
    }

    return YETTY_OK_VOID();
}

static struct yetty_yplatform_pty_pipe_source *fork_pty_pipe_source(struct yetty_yplatform_pty *self)
{
    struct fork_pty *pty = container_of(self, struct fork_pty, base);
    return &pty->pipe_source;
}

/* Create fork PTY with shell */

struct yetty_yplatform_pty_result fork_pty_create(struct yetty_yconfig *config)
{
    struct fork_pty *pty;
    struct yetty_yconfig_shell_argv shellv;
    struct yetty_ycore_void_result argv_res;
    struct winsize ws;
    int flags;

    pty = malloc(sizeof(struct fork_pty));
    if (!pty)
        return YETTY_ERR(yetty_yplatform_pty, "failed to allocate pty");

    pty->base.ops = &fork_pty_ops;
    pty->pty_master = -1;
    pty->child_pid = -1;
    pty->cols = 80;
    pty->rows = 24;
    pty->running = 0;
    pty->pipe_source.abstract = -1;

    argv_res = config->ops->get_shell_argv(config, &shellv);
    if (!YETTY_IS_OK(argv_res)) {
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "failed to resolve shell argv");
    }

    ws.ws_row = (unsigned short)pty->rows;
    ws.ws_col = (unsigned short)pty->cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    pty->child_pid = forkpty(&pty->pty_master, NULL, NULL, &ws);

    if (pty->child_pid < 0) {
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "forkpty failed");
    }

    if (pty->child_pid == 0) {
        /* Child process */
        int fd;
        for (fd = 3; fd < 1024; fd++)
            close(fd);

        execvp(shellv.argv[0], shellv.argv);
        _exit(1);
    }

    /* Parent process - set non-blocking */
    flags = fcntl(pty->pty_master, F_GETFL, 0);
    fcntl(pty->pty_master, F_SETFL, flags | O_NONBLOCK);

    pty->pipe_source.abstract = pty->pty_master;
    pty->running = 1;

    return YETTY_OK(yetty_yplatform_pty, &pty->base);
}
