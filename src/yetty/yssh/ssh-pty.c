/* SSH PTY - libssh2 as PTY backend
 *
 * Provides a PTY interface over an SSH channel using libssh2.
 * Connection parameters are resolved from ~/.ssh/config first;
 * yconfig values override any config-file defaults.
 *
 * Config keys read from yconfig:
 *   ssh/host              (string, required — also used as config-file lookup key)
 *   ssh/port              (int,    default from ~/.ssh/config or 22)
 *   ssh/username          (string, default from ~/.ssh/config)
 *   ssh/password          (string, optional)
 *   ssh/private-key-path  (string, optional — overrides IdentityFile)
 *   ssh/private-key-passphrase (string, optional)
 *   ssh/term-type         (string, default "xterm-256color")
 *
 * Threading model:
 *   - libssh2 session is used in non-blocking mode
 *   - A dedicated reader thread poll()s the socket, reads decrypted bytes
 *     from the channel, and writes them to an internal pipe whose read-end
 *     is exposed via the PTY pipe_source
 *   - The main thread performs writes/resizes on the same session
 *   - libssh2 is not thread-safe per-session; a mutex serializes every call
 */

#include <yetty/yssh/ssh-pty.h>

#include <yetty/yplatform/pty.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include "openssh-config-parser.h"

#include <libssh2.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SSH_PTY_READ_BUF        4096
#define SSH_PTY_POLL_TIMEOUT_MS 500

struct yetty_yssh_ssh_pty {
    struct yetty_platform_pty base;
    struct yetty_platform_pty_pipe_source pipe_source;

    int socket;

    char *host;      /* actual connect host (post config resolution) */
    char *host_arg;  /* original user-supplied host arg */
    uint16_t port;
    char *username;
    char *password;
    char *private_key_path;
    char *private_key_passphrase;
    char *term_type;

    LIBSSH2_SESSION *session;
    LIBSSH2_CHANNEL *channel;
    int libssh2_initialized;

    pthread_mutex_t session_mutex;

    int output_pipe[2];

    pthread_t reader_thread;
    int reader_started;
    int running;

    uint32_t cols;
    uint32_t rows;
};

static struct yetty_ycore_void_result ssh_pty_destroy(struct yetty_platform_pty *self);
static struct yetty_ycore_size_result ssh_pty_read(struct yetty_platform_pty *self, char *buf,
                                                   size_t max_len);
static struct yetty_ycore_size_result ssh_pty_write(struct yetty_platform_pty *self,
                                                    const char *data, size_t len);
static struct yetty_ycore_void_result ssh_pty_resize(struct yetty_platform_pty *self, uint32_t cols,
                                                     uint32_t rows);
static struct yetty_ycore_void_result ssh_pty_stop(struct yetty_platform_pty *self);
static struct yetty_platform_pty_pipe_source *ssh_pty_pipe_source(struct yetty_platform_pty *self);

static const struct yetty_platform_pty_ops ssh_pty_ops = {
    .destroy     = ssh_pty_destroy,
    .read        = ssh_pty_read,
    .write       = ssh_pty_write,
    .resize      = ssh_pty_resize,
    .stop        = ssh_pty_stop,
    .pipe_source = ssh_pty_pipe_source,
};

static char *dup_str_or_null(const char *s)
{
    if (!s || !s[0])
        return NULL;
    return strdup(s);
}

static int ssh_pty_tcp_connect(const char *host, uint16_t port)
{
    struct addrinfo hints, *res, *rp;
    char port_str[16];
    int sock = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_str, sizeof(port_str), "%u", port);

    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0) {
        yerror("ssh: getaddrinfo failed: %s", gai_strerror(err));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0)
            continue;
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);

    if (sock < 0) {
        yerror("ssh: failed to connect to %s:%u", host, port);
        return -1;
    }

    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    yinfo("ssh: TCP connected to %s:%u", host, port);
    return sock;
}

static int ssh_pty_wait_socket(struct yetty_yssh_ssh_pty *pty)
{
    struct pollfd pfd;
    int dir = libssh2_session_block_directions(pty->session);

    pfd.fd     = pty->socket;
    pfd.events = 0;
    if (dir & LIBSSH2_SESSION_BLOCK_INBOUND)
        pfd.events |= POLLIN;
    if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
        pfd.events |= POLLOUT;
    if (pfd.events == 0)
        pfd.events = POLLIN;

    int rc = poll(&pfd, 1, SSH_PTY_POLL_TIMEOUT_MS);
    if (rc < 0 && errno != EINTR)
        return -1;
    return 0;
}

#define SSH_RUN_NB(pty, call)                                                                      \
    ({                                                                                             \
        int _rc;                                                                                   \
        do {                                                                                       \
            _rc = (call);                                                                          \
            if (_rc == LIBSSH2_ERROR_EAGAIN) {                                                     \
                if (ssh_pty_wait_socket(pty) < 0) {                                                \
                    _rc = -1;                                                                      \
                    break;                                                                         \
                }                                                                                  \
            }                                                                                      \
        } while (_rc == LIBSSH2_ERROR_EAGAIN);                                                     \
        _rc;                                                                                       \
    })

static int ssh_pty_handshake(struct yetty_yssh_ssh_pty *pty)
{
    int rc = SSH_RUN_NB(pty, libssh2_session_handshake(pty->session, pty->socket));
    if (rc != 0) {
        char *msg = NULL;
        libssh2_session_last_error(pty->session, &msg, NULL, 0);
        yerror("ssh: handshake failed: %s", msg ? msg : "unknown");
        return -1;
    }
    yinfo("ssh: handshake complete");
    return 0;
}

static int ssh_pty_authenticate(struct yetty_yssh_ssh_pty *pty)
{
    int rc;

    if (pty->private_key_path) {
        rc = SSH_RUN_NB(pty,
                        libssh2_userauth_publickey_fromfile(pty->session, pty->username, NULL,
                                                            pty->private_key_path,
                                                            pty->private_key_passphrase));
        if (rc == 0) {
            yinfo("ssh: authenticated with public key");
            return 0;
        }
        ydebug("ssh: public key auth failed (rc=%d), trying password", rc);
    }

    if (pty->password) {
        rc = SSH_RUN_NB(pty,
                        libssh2_userauth_password(pty->session, pty->username, pty->password));
        if (rc == 0) {
            yinfo("ssh: authenticated with password");
            return 0;
        }
        ydebug("ssh: password auth failed (rc=%d)", rc);
    }

    yerror("ssh: authentication failed for user %s", pty->username);
    return -1;
}

static int ssh_pty_open_channel(struct yetty_yssh_ssh_pty *pty)
{
    while (1) {
        pty->channel = libssh2_channel_open_session(pty->session);
        if (pty->channel)
            break;
        if (libssh2_session_last_errno(pty->session) != LIBSSH2_ERROR_EAGAIN) {
            yerror("ssh: failed to open channel");
            return -1;
        }
        if (ssh_pty_wait_socket(pty) < 0)
            return -1;
    }

    int rc = SSH_RUN_NB(pty,
                        libssh2_channel_request_pty_ex(pty->channel, pty->term_type,
                                                       (unsigned int)strlen(pty->term_type), NULL,
                                                       0, (int)pty->cols, (int)pty->rows, 0, 0));
    if (rc != 0) {
        yerror("ssh: failed to request pty (rc=%d)", rc);
        return -1;
    }

    rc = SSH_RUN_NB(pty, libssh2_channel_shell(pty->channel));
    if (rc != 0) {
        yerror("ssh: failed to start shell (rc=%d)", rc);
        return -1;
    }

    yinfo("ssh: shell started");
    return 0;
}

static void *ssh_pty_reader_thread(void *arg)
{
    struct yetty_yssh_ssh_pty *pty = arg;
    char buf[SSH_PTY_READ_BUF];

    yinfo("ssh_reader: started");

    while (pty->running) {
        struct pollfd pfd = {.fd = pty->socket, .events = POLLIN};
        int pr = poll(&pfd, 1, SSH_PTY_POLL_TIMEOUT_MS);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (!pty->running)
            break;
        if (pr == 0)
            continue;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            yinfo("ssh_reader: socket closed");
            break;
        }

        pthread_mutex_lock(&pty->session_mutex);
        int channel_eof = 0;
        ssize_t total = 0;
        for (;;) {
            ssize_t n = libssh2_channel_read(pty->channel, buf, sizeof(buf));
            if (n == LIBSSH2_ERROR_EAGAIN)
                break;
            if (n <= 0) {
                if (libssh2_channel_eof(pty->channel))
                    channel_eof = 1;
                break;
            }
            ssize_t w = write(pty->output_pipe[1], buf, (size_t)n);
            (void)w;
            total += n;
        }
        pthread_mutex_unlock(&pty->session_mutex);

        if (total > 0)
            ytrace("ssh_reader: forwarded %zd bytes", total);
        if (channel_eof) {
            yinfo("ssh_reader: channel EOF");
            break;
        }
    }

    pty->running = 0;
    yinfo("ssh_reader: exiting");
    return NULL;
}

static struct yetty_ycore_void_result ssh_pty_destroy(struct yetty_platform_pty *self)
{
    struct yetty_yssh_ssh_pty *pty = (struct yetty_yssh_ssh_pty *)self;

    struct yetty_ycore_void_result stop_r = ssh_pty_stop(self);

    pthread_mutex_lock(&pty->session_mutex);
    if (pty->channel) {
        libssh2_channel_free(pty->channel);
        pty->channel = NULL;
    }
    if (pty->session) {
        libssh2_session_disconnect(pty->session, "bye");
        libssh2_session_free(pty->session);
        pty->session = NULL;
    }
    pthread_mutex_unlock(&pty->session_mutex);

    if (pty->socket >= 0) {
        close(pty->socket);
        pty->socket = -1;
    }
    if (pty->output_pipe[0] >= 0)
        close(pty->output_pipe[0]);
    if (pty->output_pipe[1] >= 0)
        close(pty->output_pipe[1]);

    if (pty->libssh2_initialized)
        libssh2_exit();

    pthread_mutex_destroy(&pty->session_mutex);

    free(pty->host);
    free(pty->host_arg);
    free(pty->username);
    if (pty->password) {
        memset(pty->password, 0, strlen(pty->password));
        free(pty->password);
    }
    free(pty->private_key_path);
    if (pty->private_key_passphrase) {
        memset(pty->private_key_passphrase, 0, strlen(pty->private_key_passphrase));
        free(pty->private_key_passphrase);
    }
    free(pty->term_type);
    free(pty);

    if (YETTY_IS_ERR(stop_r))
        return YETTY_ERR(yetty_ycore_void, "ssh_pty_destroy: stop failed", stop_r);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_size_result ssh_pty_read(struct yetty_platform_pty *self, char *buf,
                                                   size_t max_len)
{
    struct yetty_yssh_ssh_pty *pty = (struct yetty_yssh_ssh_pty *)self;
    if (max_len == 0)
        return YETTY_OK(yetty_ycore_size, 0);
    ssize_t n = read(pty->output_pipe[0], buf, max_len);
    if (n < 0)
        n = 0;
    return YETTY_OK(yetty_ycore_size, (size_t)n);
}

static struct yetty_ycore_size_result ssh_pty_write(struct yetty_platform_pty *self,
                                                    const char *data, size_t len)
{
    struct yetty_yssh_ssh_pty *pty = (struct yetty_yssh_ssh_pty *)self;
    if (!pty->running || !pty->channel || len == 0)
        return YETTY_OK(yetty_ycore_size, 0);

    size_t written = 0;
    pthread_mutex_lock(&pty->session_mutex);
    while (written < len) {
        ssize_t rc = libssh2_channel_write(pty->channel, data + written, len - written);
        if (rc == LIBSSH2_ERROR_EAGAIN)
            break;
        if (rc < 0) {
            yerror("ssh: write error rc=%zd", rc);
            break;
        }
        written += (size_t)rc;
    }
    pthread_mutex_unlock(&pty->session_mutex);

    return YETTY_OK(yetty_ycore_size, written);
}

static struct yetty_ycore_void_result ssh_pty_resize(struct yetty_platform_pty *self, uint32_t cols,
                                                     uint32_t rows)
{
    struct yetty_yssh_ssh_pty *pty = (struct yetty_yssh_ssh_pty *)self;
    pty->cols = cols;
    pty->rows = rows;

    if (!pty->channel)
        return YETTY_OK_VOID();

    pthread_mutex_lock(&pty->session_mutex);
    int rc = libssh2_channel_request_pty_size(pty->channel, (int)cols, (int)rows);
    pthread_mutex_unlock(&pty->session_mutex);

    if (rc != 0 && rc != LIBSSH2_ERROR_EAGAIN)
        ywarn("ssh: failed to send window size: rc=%d", rc);

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ssh_pty_stop(struct yetty_platform_pty *self)
{
    struct yetty_yssh_ssh_pty *pty = (struct yetty_yssh_ssh_pty *)self;

    if (!pty->running && !pty->reader_started)
        return YETTY_OK_VOID();

    pty->running = 0;
    if (pty->socket >= 0)
        shutdown(pty->socket, SHUT_RDWR);

    if (pty->reader_started) {
        pthread_join(pty->reader_thread, NULL);
        pty->reader_started = 0;
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
    if (!config || !config->ops)
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: missing yconfig");

    const char *host_arg = config->ops->get_string(config, "ssh/host", "");
    if (!host_arg || !host_arg[0])
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: ssh/host is required");

    /* Resolve ~/.ssh/config for host_arg; yconfig values override. */
    struct yetty_yssh_openssh_config *ssh_cfg = yetty_yssh_openssh_config_resolve(host_arg);
    if (!ssh_cfg)
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: config resolution failed");

    /* Effective connection parameters: yconfig wins over ssh_config. */
    const char *connect_host = ssh_cfg->hostname ? ssh_cfg->hostname : host_arg;

    int port_cfg = config->ops->get_int(config, "ssh/port", 0);
    int port = port_cfg > 0 ? port_cfg : (ssh_cfg->port > 0 ? ssh_cfg->port : 22);

    const char *username_cfg = config->ops->get_string(config, "ssh/username", "");
    const char *username = (username_cfg && username_cfg[0]) ? username_cfg
                                                              : (ssh_cfg->user ? ssh_cfg->user : "");

    const char *key_path_cfg = config->ops->get_string(config, "ssh/private-key-path", "");
    const char *key_path =
        (key_path_cfg && key_path_cfg[0])
            ? key_path_cfg
            : (ssh_cfg->num_identity_files > 0 ? ssh_cfg->identity_files[0] : "");

    const char *password  = config->ops->get_string(config, "ssh/password", "");
    const char *key_pass  = config->ops->get_string(config, "ssh/private-key-passphrase", "");
    const char *term_type = config->ops->get_string(config, "ssh/term-type", "xterm-256color");

    if (!username || !username[0]) {
        yetty_yssh_openssh_config_free(ssh_cfg);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: no username (set ssh/username or User in ~/.ssh/config)");
    }
    if (port <= 0 || port > 65535) {
        yetty_yssh_openssh_config_free(ssh_cfg);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: invalid port");
    }

    struct yetty_yssh_ssh_pty *pty = calloc(1, sizeof(*pty));
    if (!pty) {
        yetty_yssh_openssh_config_free(ssh_cfg);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: alloc failed");
    }

    pty->base.ops    = &ssh_pty_ops;
    pty->socket      = -1;
    pty->output_pipe[0] = -1;
    pty->output_pipe[1] = -1;
    pty->cols        = 80;
    pty->rows        = 24;
    pty->port        = (uint16_t)port;

    if (pthread_mutex_init(&pty->session_mutex, NULL) != 0) {
        free(pty);
        yetty_yssh_openssh_config_free(ssh_cfg);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: mutex init failed");
    }

    pty->host                 = strdup(connect_host);
    pty->host_arg             = strdup(host_arg);
    pty->username             = strdup(username);
    pty->password             = dup_str_or_null(password);
    pty->private_key_path     = dup_str_or_null(key_path);
    pty->private_key_passphrase = dup_str_or_null(key_pass);
    pty->term_type            = strdup(term_type ? term_type : "xterm-256color");

    yetty_yssh_openssh_config_free(ssh_cfg);

    if (!pty->host || !pty->host_arg || !pty->username || !pty->term_type) {
        ssh_pty_destroy(&pty->base);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: string alloc failed");
    }
    if (!pty->password && !pty->private_key_path) {
        ssh_pty_destroy(&pty->base);
        return YETTY_ERR(yetty_yplatform_pty_ptr,
                         "ssh: no credentials (set ssh/password, ssh/private-key-path, or IdentityFile in ~/.ssh/config)");
    }

    if (pipe(pty->output_pipe) < 0) {
        ssh_pty_destroy(&pty->base);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: pipe failed");
    }
    fcntl(pty->output_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(pty->output_pipe[1], F_SETFL, O_NONBLOCK);
    pty->pipe_source.abstract = (uintptr_t)pty->output_pipe[0];

    if (libssh2_init(0) != 0) {
        ssh_pty_destroy(&pty->base);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: libssh2_init failed");
    }
    pty->libssh2_initialized = 1;

    pty->socket = ssh_pty_tcp_connect(pty->host, pty->port);
    if (pty->socket < 0) {
        ssh_pty_destroy(&pty->base);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: TCP connect failed");
    }

    pty->session = libssh2_session_init();
    if (!pty->session) {
        ssh_pty_destroy(&pty->base);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: session init failed");
    }
    libssh2_session_set_blocking(pty->session, 0);

    if (ssh_pty_handshake(pty) < 0) {
        ssh_pty_destroy(&pty->base);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: handshake failed");
    }
    if (ssh_pty_authenticate(pty) < 0) {
        ssh_pty_destroy(&pty->base);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: authentication failed");
    }
    if (ssh_pty_open_channel(pty) < 0) {
        ssh_pty_destroy(&pty->base);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: channel setup failed");
    }

    int flags = fcntl(pty->socket, F_GETFL, 0);
    fcntl(pty->socket, F_SETFL, flags | O_NONBLOCK);

    pty->running = 1;
    if (pthread_create(&pty->reader_thread, NULL, ssh_pty_reader_thread, pty) != 0) {
        pty->running = 0;
        ssh_pty_destroy(&pty->base);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh: reader thread failed");
    }
    pty->reader_started = 1;

    yinfo("ssh: PTY ready (%s@%s:%u via %s)", pty->username, pty->host, pty->port, pty->host_arg);
    return YETTY_OK(yetty_yplatform_pty_ptr, &pty->base);
}
