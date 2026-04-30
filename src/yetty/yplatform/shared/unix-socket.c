#include <yetty/platform/socket.h>

#include <stdio.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

int yetty_platform_socket_init(void)
{
    return 1;
}

void yetty_platform_socket_cleanup(void)
{
}

struct yetty_socket_fd_result yetty_platform_socket_create_tcp(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return YETTY_ERR(yetty_socket_fd, "socket() failed");
    }
    return YETTY_OK(yetty_socket_fd, fd);
}

void yetty_yplatform_socket_close(yetty_socket_fd fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

struct yetty_ycore_void_result yetty_platform_socket_set_nonblocking(yetty_socket_fd fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return YETTY_ERR(yetty_ycore_void, "fcntl(F_GETFL) failed");
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return YETTY_ERR(yetty_ycore_void, "fcntl(F_SETFL) failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_platform_socket_set_nodelay(yetty_socket_fd fd, int enable)
{
    int val = enable ? 1 : 0;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) < 0) {
        return YETTY_ERR(yetty_ycore_void, "setsockopt(TCP_NODELAY) failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_platform_socket_set_reuseaddr(yetty_socket_fd fd, int enable)
{
    int val = enable ? 1 : 0;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
        return YETTY_ERR(yetty_ycore_void, "setsockopt(SO_REUSEADDR) failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_platform_socket_bind(yetty_socket_fd fd, uint16_t port)
{
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        return YETTY_ERR(yetty_ycore_void, "bind() failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yplatform_socket_listen(yetty_socket_fd fd, int backlog)
{
    if (listen(fd, backlog) < 0) {
        return YETTY_ERR(yetty_ycore_void, "listen() failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_socket_fd_result yetty_yplatform_socket_accept(yetty_socket_fd fd)
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int client_fd = accept(fd, (struct sockaddr *)&addr, &addrlen);
    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return YETTY_OK(yetty_socket_fd, YETTY_SOCKET_INVALID);
        }
        return YETTY_ERR(yetty_socket_fd, "accept() failed");
    }
    return YETTY_OK(yetty_socket_fd, client_fd);
}

struct yetty_ycore_void_result yetty_yplatform_socket_connect(yetty_socket_fd fd, const char *host,
                                                              uint16_t port)
{
    struct addrinfo hints = {0};
    struct addrinfo *result;
    char port_str[8];

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port_str, sizeof(port_str), "%u", port);
    int err = getaddrinfo(host, port_str, &hints, &result);
    if (err != 0) {
        return YETTY_ERR(yetty_ycore_void, "getaddrinfo() failed");
    }

    int ret = connect(fd, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);

    if (ret < 0 && errno != EINPROGRESS) {
        return YETTY_ERR(yetty_ycore_void, "connect() failed");
    }

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_platform_socket_connect_check(yetty_socket_fd fd)
{
    int err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
        return YETTY_ERR(yetty_ycore_void, "getsockopt(SO_ERROR) failed");
    }
    if (err != 0) {
        return YETTY_ERR(yetty_ycore_void, "connect failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_size_result yetty_yplatform_socket_send(yetty_socket_fd fd, const void *data,
                                                           size_t len)
{
    ssize_t sent = send(fd, data, len, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return YETTY_OK(yetty_ycore_size, 0);
        }
        return YETTY_ERR(yetty_ycore_size, "send() failed");
    }
    return YETTY_OK(yetty_ycore_size, (size_t)sent);
}

struct yetty_ycore_size_result yetty_yplatform_socket_recv(yetty_socket_fd fd, void *buf,
                                                           size_t max_len)
{
    ssize_t received = recv(fd, buf, max_len, MSG_DONTWAIT);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return YETTY_OK(yetty_ycore_size, 0);
        }
        return YETTY_ERR(yetty_ycore_size, "recv() failed");
    }
    return YETTY_OK(yetty_ycore_size, (size_t)received);
}

int yetty_yplatform_socket_would_block(void)
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

int yetty_platform_socket_connect_in_progress(void)
{
    return errno == EINPROGRESS;
}
