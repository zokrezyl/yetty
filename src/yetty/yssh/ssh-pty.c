/* SSH PTY - forkpty wrapper around the bundled openssh client.
 *
 * Builds an argv for the bundled ssh binary and forks a PTY child via
 * forkpty(). The openssh process handles everything: ~/.ssh/config,
 * ProxyJump, IdentityFile, ControlMaster, host key verification, etc.
 *
 * Config keys read from yconfig:
 *   ssh/host              (string, required)
 *   ssh/port              (int,    default 22 — omitted from argv means openssh default)
 *   ssh/username          (string, optional — openssh falls back to ~/.ssh/config)
 *   ssh/private-key-path  (string, optional)
 */

#include <yetty/yssh/ssh-pty.h>

#include <yetty/yplatform/pty.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include "ssh-binary-locator.h"

#include <fcntl.h>
#include <stdio.h>
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

struct yetty_yssh_ssh_pty {
    struct yetty_platform_pty base;
    struct yetty_platform_pty_pipe_source pipe_source;
    int pty_master;
    pid_t child_pid;
    uint32_t cols;
    uint32_t rows;
    int running;
};

static struct yetty_ycore_void_result ssh_pty_destroy(struct yetty_platform_pty *self);
static struct yetty_ycore_size_result ssh_pty_read(struct yetty_platform_pty *self, char *buf,
                                                   size_t max_len);
static struct yetty_ycore_size_result ssh_pty_write(struct yetty_platform_pty *self,
                                                    const char *data, size_t len);
static struct yetty_ycore_void_result ssh_pty_resize(struct yetty_platform_pty *self,
                                                     uint32_t cols, uint32_t rows);
static struct yetty_ycore_void_result ssh_pty_stop(struct yetty_platform_pty *self);
static struct yetty_platform_pty_pipe_source *ssh_pty_pipe_source(struct yetty_platform_pty *self);

static const struct yetty_platform_pty_ops ssh_pty_ops = {
    .destroy    = ssh_pty_destroy,
    .read       = ssh_pty_read,
    .write      = ssh_pty_write,
    .resize     = ssh_pty_resize,
    .stop       = ssh_pty_stop,
    .pipe_source = ssh_pty_pipe_source,
};

static struct yetty_ycore_void_result ssh_pty_destroy(struct yetty_platform_pty *self)
{
    struct yetty_yssh_ssh_pty *pty = (struct yetty_yssh_ssh_pty *)self;
    struct yetty_ycore_void_result stop_r = ssh_pty_stop(self);
    free(pty);
    if (YETTY_IS_ERR(stop_r)) {
        return YETTY_ERR(yetty_ycore_void, "ssh_pty_destroy: stop failed", stop_r);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_size_result ssh_pty_read(struct yetty_platform_pty *self, char *buf,
                                                   size_t max_len)
{
    struct yetty_yssh_ssh_pty *pty = (struct yetty_yssh_ssh_pty *)self;
    ssize_t n = read(pty->pty_master, buf, max_len);
    if (n < 0) {
        return YETTY_ERR(yetty_ycore_size, "read from ssh pty failed");
    }
    return YETTY_OK(yetty_ycore_size, (size_t)n);
}

static struct yetty_ycore_size_result ssh_pty_write(struct yetty_platform_pty *self,
                                                    const char *data, size_t len)
{
    struct yetty_yssh_ssh_pty *pty = (struct yetty_yssh_ssh_pty *)self;
    if (len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    ssize_t n = write(pty->pty_master, data, len);
    if (n < 0) {
        return YETTY_ERR(yetty_ycore_size, "write to ssh pty failed");
    }
    return YETTY_OK(yetty_ycore_size, (size_t)n);
}

static struct yetty_ycore_void_result ssh_pty_resize(struct yetty_platform_pty *self,
                                                     uint32_t cols, uint32_t rows)
{
    struct yetty_yssh_ssh_pty *pty = (struct yetty_yssh_ssh_pty *)self;
    struct winsize ws;

    pty->cols = cols;
    pty->rows = rows;

    if (pty->pty_master < 0) {
        return YETTY_OK_VOID();
    }

    ws.ws_col    = (unsigned short)cols;
    ws.ws_row    = (unsigned short)rows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    if (ioctl(pty->pty_master, TIOCSWINSZ, &ws) < 0) {
        return YETTY_ERR(yetty_ycore_void, "ssh pty resize: TIOCSWINSZ failed");
    }
    return YETTY_OK_VOID();
}

static int ssh_pty_wait_ms(pid_t pid, int *status, int timeout_ms)
{
    int waited = 0;
    while (waited < timeout_ms) {
        pid_t r = waitpid(pid, status, WNOHANG);
        if (r == pid) {
            return 1;
        }
        if (r < 0) {
            return -1;
        }
        usleep(10 * 1000);
        waited += 10;
    }
    return 0;
}

static struct yetty_ycore_void_result ssh_pty_stop(struct yetty_platform_pty *self)
{
    struct yetty_yssh_ssh_pty *pty = (struct yetty_yssh_ssh_pty *)self;
    int status;

    if (!pty->running) {
        return YETTY_OK_VOID();
    }
    pty->running = 0;

    if (pty->pty_master >= 0) {
        close(pty->pty_master);
        pty->pty_master = -1;
    }

    if (pty->child_pid > 0) {
        kill(pty->child_pid, SIGHUP);
        if (ssh_pty_wait_ms(pty->child_pid, &status, 200) <= 0) {
            kill(pty->child_pid, SIGTERM);
            if (ssh_pty_wait_ms(pty->child_pid, &status, 200) <= 0) {
                kill(pty->child_pid, SIGKILL);
                waitpid(pty->child_pid, &status, 0);
            }
        }
        pty->child_pid = -1;
    }

    return YETTY_OK_VOID();
}

static struct yetty_platform_pty_pipe_source *ssh_pty_pipe_source(struct yetty_platform_pty *self)
{
    struct yetty_yssh_ssh_pty *pty = (struct yetty_yssh_ssh_pty *)self;
    return &pty->pipe_source;
}

struct yetty_yplatform_pty_ptr_result yetty_yssh_ssh_pty_create(struct yetty_yconfig_config *config)
{
    const char *host;
    int port_i;
    const char *username;
    const char *key_path;

    if (!config || !config->ops) {
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: missing yconfig");
    }

    host     = config->ops->get_string(config, "ssh/host", "");
    port_i   = config->ops->get_int(config, "ssh/port", 22);
    username = config->ops->get_string(config, "ssh/username", "");
    key_path = config->ops->get_string(config, "ssh/private-key-path", "");

    if (!host || !host[0]) {
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: ssh/host is required");
    }

    struct yetty_yssh_ssh_pty *pty = calloc(1, sizeof(*pty));
    if (!pty) {
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: alloc failed");
    }

    pty->base.ops    = &ssh_pty_ops;
    pty->pty_master  = -1;
    pty->child_pid   = -1;
    pty->cols        = 80;
    pty->rows        = 24;

    /* Build argv: ssh [-p port] [-i key] [user@]host */
    const char *ssh_bin = yssh_resolve_ssh_binary();
    const char *argv[16];
    char port_str[16];
    char user_host[512];
    int argc = 0;

    argv[argc++] = ssh_bin;

    if (port_i != 22) {
        snprintf(port_str, sizeof(port_str), "%d", port_i);
        argv[argc++] = "-p";
        argv[argc++] = port_str;
    }

    if (key_path && key_path[0]) {
        argv[argc++] = "-i";
        argv[argc++] = key_path;
    }

    if (username && username[0]) {
        snprintf(user_host, sizeof(user_host), "%s@%s", username, host);
        argv[argc++] = user_host;
    } else {
        argv[argc++] = host;
    }
    argv[argc] = NULL;

    struct winsize ws;
    ws.ws_col    = (unsigned short)pty->cols;
    ws.ws_row    = (unsigned short)pty->rows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    pty->child_pid = forkpty(&pty->pty_master, NULL, NULL, &ws);
    if (pty->child_pid < 0) {
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: forkpty failed");
    }

    if (pty->child_pid == 0) {
        for (int fd = 3; fd < 1024; fd++) {
            close(fd);
        }
        execv(ssh_bin, (char *const *)argv);
        _exit(127);
    }

    int flags = fcntl(pty->pty_master, F_GETFL, 0);
    fcntl(pty->pty_master, F_SETFL, flags | O_NONBLOCK);

    pty->pipe_source.abstract = (uintptr_t)pty->pty_master;
    pty->running = 1;

    yinfo("ssh: forked %s (pid %d)", ssh_bin, (int)pty->child_pid);
    return YETTY_OK(yetty_yplatform_pty_ptr, &pty->base);
}
