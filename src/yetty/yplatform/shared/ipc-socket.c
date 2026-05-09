/* Unix domain socket implementation for Linux/macOS */

#include <yetty/platform/ipc-socket.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

struct yetty_platform_socket {
    int fd;
    int is_listener;
};

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int make_socket_dir(const char *path)
{
    char dir[YETTY_IPC_SOCKET_PATH_MAX];
    char *last_slash;

    if (strlen(path) >= sizeof(dir)) {
        return -1;
    }

    strcpy(dir, path);
    last_slash = strrchr(dir, '/');
    if (!last_slash) {
        return 0; /* no directory component */
    }

    *last_slash = '\0';
    if (mkdir(dir, 0700) < 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

static void get_default_path(char *path_out)
{
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir) {
        runtime_dir = "/tmp";
    }

    snprintf(path_out, YETTY_IPC_SOCKET_PATH_MAX, "%s/yetty/yetty-%d.sock", runtime_dir,
             (int)getpid());
}

struct yetty_ipc_socket_result yetty_platform_socket_listen(const char *path, char *path_out)
{
    struct yetty_platform_socket *sock;
    struct sockaddr_un addr;
    char default_path[YETTY_IPC_SOCKET_PATH_MAX];
    int fd;

    if (!path) {
        get_default_path(default_path);
        path = default_path;
    }

    if (strlen(path) >= sizeof(addr.sun_path)) {
        return YETTY_ERR(yetty_ipc_socket, "socket path too long");
    }

    /* Create socket directory if needed */
    if (make_socket_dir(path) < 0) {
        return YETTY_ERR(yetty_ipc_socket, "failed to create socket directory");
    }

    /* Remove existing socket file */
    unlink(path);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return YETTY_ERR(yetty_ipc_socket, "failed to create socket");
    }

    if (set_nonblocking(fd) < 0) {
        close(fd);
        return YETTY_ERR(yetty_ipc_socket, "failed to set non-blocking");
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return YETTY_ERR(yetty_ipc_socket, "failed to bind socket");
    }

    if (listen(fd, 8) < 0) {
        close(fd);
        unlink(path);
        return YETTY_ERR(yetty_ipc_socket, "failed to listen");
    }

    sock = calloc(1, sizeof(struct yetty_platform_socket));
    if (!sock) {
        close(fd);
        unlink(path);
        return YETTY_ERR(yetty_ipc_socket, "out of memory");
    }

    sock->fd = fd;
    sock->is_listener = 1;

    if (path_out) {
        strncpy(path_out, path, YETTY_IPC_SOCKET_PATH_MAX - 1);
    }

    return YETTY_OK(yetty_ipc_socket, sock);
}

struct yetty_ipc_socket_result yetty_platform_socket_connect(const char *path)
{
    struct yetty_platform_socket *sock;
    struct sockaddr_un addr;
    int fd;

    if (!path) {
        return YETTY_ERR(yetty_ipc_socket, "path is required");
    }

    if (strlen(path) >= sizeof(addr.sun_path)) {
        return YETTY_ERR(yetty_ipc_socket, "socket path too long");
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return YETTY_ERR(yetty_ipc_socket, "failed to create socket");
    }

    if (set_nonblocking(fd) < 0) {
        close(fd);
        return YETTY_ERR(yetty_ipc_socket, "failed to set non-blocking");
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
            close(fd);
            return YETTY_ERR(yetty_ipc_socket, "failed to connect");
        }
    }

    sock = calloc(1, sizeof(struct yetty_platform_socket));
    if (!sock) {
        close(fd);
        return YETTY_ERR(yetty_ipc_socket, "out of memory");
    }

    sock->fd = fd;
    sock->is_listener = 0;

    return YETTY_OK(yetty_ipc_socket, sock);
}

void yetty_platform_socket_close(yetty_ipc_socket_t sock)
{
    if (!sock) {
        return;
    }

    if (sock->fd >= 0) {
        close(sock->fd);
    }

    free(sock);
}

struct yetty_ipc_socket_result yetty_platform_socket_accept(yetty_ipc_socket_t sock)
{
    struct yetty_platform_socket *client;
    int client_fd;

    if (!sock || !sock->is_listener) {
        return YETTY_ERR(yetty_ipc_socket, "not a listening socket");
    }

    client_fd = accept(sock->fd, NULL, NULL);
    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return YETTY_OK(yetty_ipc_socket, YETTY_IPC_SOCKET_INVALID);
        }
        return YETTY_ERR(yetty_ipc_socket, "accept failed");
    }

    if (set_nonblocking(client_fd) < 0) {
        close(client_fd);
        return YETTY_ERR(yetty_ipc_socket, "failed to set non-blocking");
    }

    client = calloc(1, sizeof(struct yetty_platform_socket));
    if (!client) {
        close(client_fd);
        return YETTY_ERR(yetty_ipc_socket, "out of memory");
    }

    client->fd = client_fd;
    client->is_listener = 0;

    return YETTY_OK(yetty_ipc_socket, client);
}

struct yetty_ycore_size_result yetty_platform_socket_send(yetty_ipc_socket_t sock, const void *data,
                                                          size_t len)
{
    ssize_t sent;

    if (!sock) {
        return YETTY_ERR(yetty_ycore_size, "invalid socket");
    }

    sent = send(sock->fd, data, len, MSG_NOSIGNAL);
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return YETTY_OK(yetty_ycore_size, 0);
        }
        return YETTY_ERR(yetty_ycore_size, "send failed");
    }

    return YETTY_OK(yetty_ycore_size, (size_t)sent);
}

struct yetty_ycore_size_result yetty_platform_socket_recv(yetty_ipc_socket_t sock, void *buf,
                                                          size_t max_len)
{
    ssize_t received;

    if (!sock) {
        return YETTY_ERR(yetty_ycore_size, "invalid socket");
    }

    received = recv(sock->fd, buf, max_len, 0);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return YETTY_OK(yetty_ycore_size, 0);
        }
        return YETTY_ERR(yetty_ycore_size, "recv failed");
    }

    return YETTY_OK(yetty_ycore_size, (size_t)received);
}

int yetty_platform_socket_would_block(void)
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

int yetty_platform_socket_has_data(yetty_ipc_socket_t sock)
{
    fd_set readfds;
    struct timeval tv = {0, 0};

    if (!sock || sock->fd < 0) {
        return 0;
    }

    FD_ZERO(&readfds);
    FD_SET(sock->fd, &readfds);

    return select(sock->fd + 1, &readfds, NULL, NULL, &tv) > 0;
}

int yetty_platform_socket_get_fd(yetty_ipc_socket_t sock)
{
    if (!sock) {
        return -1;
    }
    return sock->fd;
}

void yetty_platform_socket_unlink(const char *path)
{
    if (path) {
        unlink(path);
    }
}
