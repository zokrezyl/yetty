#include <yetty/platform/socket.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

static int g_winsock_initialized = 0;

int yetty_yplatform_socket_init(void)
{
    if (g_winsock_initialized) {
        return 1;
    }
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return 0;
    }
    g_winsock_initialized = 1;
    return 1;
}

void yetty_yplatform_socket_cleanup(void)
{
    if (g_winsock_initialized) {
        WSACleanup();
        g_winsock_initialized = 0;
    }
}

struct yetty_socket_fd_result yetty_yplatform_socket_create_tcp(void)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return YETTY_ERR(yetty_socket_fd, "socket() failed");
    }
    return YETTY_OK(yetty_socket_fd, (yetty_socket_fd)s);
}

void yetty_yplatform_socket_close(yetty_socket_fd fd)
{
    if (fd != YETTY_SOCKET_INVALID) {
        closesocket((SOCKET)fd);
    }
}

struct yetty_ycore_void_result yetty_yplatform_socket_set_nonblocking(yetty_socket_fd fd)
{
    u_long mode = 1;
    if (ioctlsocket((SOCKET)fd, FIONBIO, &mode) != 0) {
        return YETTY_ERR(yetty_ycore_void, "ioctlsocket(FIONBIO) failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yplatform_socket_set_nodelay(yetty_socket_fd fd, int enable)
{
    BOOL val = enable ? TRUE : FALSE;
    if (setsockopt((SOCKET)fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&val, sizeof(val)) != 0) {
        return YETTY_ERR(yetty_ycore_void, "setsockopt(TCP_NODELAY) failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yplatform_socket_set_reuseaddr(yetty_socket_fd fd, int enable)
{
    BOOL val = enable ? TRUE : FALSE;
    if (setsockopt((SOCKET)fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&val, sizeof(val)) != 0) {
        return YETTY_ERR(yetty_ycore_void, "setsockopt(SO_REUSEADDR) failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yplatform_socket_bind(yetty_socket_fd fd, uint16_t port)
{
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind((SOCKET)fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        return YETTY_ERR(yetty_ycore_void, "bind() failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yplatform_socket_listen(yetty_socket_fd fd, int backlog)
{
    if (listen((SOCKET)fd, backlog) != 0) {
        return YETTY_ERR(yetty_ycore_void, "listen() failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_socket_fd_result yetty_yplatform_socket_accept(yetty_socket_fd fd)
{
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);
    SOCKET client = accept((SOCKET)fd, (struct sockaddr *)&addr, &addrlen);
    if (client == INVALID_SOCKET) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            return YETTY_OK(yetty_socket_fd, YETTY_SOCKET_INVALID);
        }
        return YETTY_ERR(yetty_socket_fd, "accept() failed");
    }
    return YETTY_OK(yetty_socket_fd, (yetty_socket_fd)client);
}

struct yetty_ycore_void_result yetty_yplatform_socket_connect(yetty_socket_fd fd, const char *host,
                                                              uint16_t port)
{
    struct addrinfo hints = {0};
    struct addrinfo *result;
    char port_str[8];

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    _snprintf_s(port_str, sizeof(port_str), _TRUNCATE, "%u", port);
    int err = getaddrinfo(host, port_str, &hints, &result);
    if (err != 0) {
        return YETTY_ERR(yetty_ycore_void, "getaddrinfo() failed");
    }

    int ret = connect((SOCKET)fd, result->ai_addr, (int)result->ai_addrlen);
    freeaddrinfo(result);

    if (ret != 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
        return YETTY_ERR(yetty_ycore_void, "connect() failed");
    }

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yplatform_socket_connect_check(yetty_socket_fd fd)
{
    int err = 0;
    int len = sizeof(err);
    if (getsockopt((SOCKET)fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len) != 0) {
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
    int sent = send((SOCKET)fd, (const char *)data, (int)len, 0);
    if (sent < 0) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            return YETTY_OK(yetty_ycore_size, 0);
        }
        return YETTY_ERR(yetty_ycore_size, "send() failed");
    }
    return YETTY_OK(yetty_ycore_size, (size_t)sent);
}

struct yetty_ycore_size_result yetty_yplatform_socket_recv(yetty_socket_fd fd, void *buf,
                                                           size_t max_len)
{
    int received = recv((SOCKET)fd, (char *)buf, (int)max_len, 0);
    if (received < 0) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            return YETTY_OK(yetty_ycore_size, 0);
        }
        return YETTY_ERR(yetty_ycore_size, "recv() failed");
    }
    return YETTY_OK(yetty_ycore_size, (size_t)received);
}

int yetty_yplatform_socket_would_block(void)
{
    return WSAGetLastError() == WSAEWOULDBLOCK;
}

int yetty_yplatform_socket_connect_in_progress(void)
{
    return WSAGetLastError() == WSAEWOULDBLOCK;
}
