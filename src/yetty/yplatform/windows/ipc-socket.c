/* Windows named pipe implementation for RPC */

#include <yetty/platform/ipc-socket.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct yetty_platform_socket {
    HANDLE handle;
    int is_listener;
    char pipe_name[YETTY_IPC_SOCKET_PATH_MAX];
};

static void get_default_path(char *path_out)
{
    snprintf(path_out, YETTY_IPC_SOCKET_PATH_MAX, "\\\\.\\pipe\\yetty-%lu", GetCurrentProcessId());
}

struct yetty_ipc_socket_result yetty_platform_socket_listen(const char *path, char *path_out)
{
    struct yetty_platform_socket *sock;
    char default_path[YETTY_IPC_SOCKET_PATH_MAX];
    HANDLE pipe;

    if (!path) {
        get_default_path(default_path);
        path = default_path;
    }

    pipe = CreateNamedPipeA(path, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                            PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, NULL);

    if (pipe == INVALID_HANDLE_VALUE) {
        return YETTY_ERR(yetty_ipc_socket, "failed to create named pipe");
    }

    sock = calloc(1, sizeof(struct yetty_platform_socket));
    if (!sock) {
        CloseHandle(pipe);
        return YETTY_ERR(yetty_ipc_socket, "out of memory");
    }

    sock->handle = pipe;
    sock->is_listener = 1;
    strncpy(sock->pipe_name, path, YETTY_IPC_SOCKET_PATH_MAX - 1);

    if (path_out) {
        strncpy(path_out, path, YETTY_IPC_SOCKET_PATH_MAX - 1);
    }

    return YETTY_OK(yetty_ipc_socket, sock);
}

struct yetty_ipc_socket_result yetty_platform_socket_connect(const char *path)
{
    struct yetty_platform_socket *sock;
    HANDLE pipe;

    if (!path) {
        return YETTY_ERR(yetty_ipc_socket, "path is required");
    }

    pipe = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                       FILE_FLAG_OVERLAPPED, NULL);

    if (pipe == INVALID_HANDLE_VALUE) {
        return YETTY_ERR(yetty_ipc_socket, "failed to connect to pipe");
    }

    sock = calloc(1, sizeof(struct yetty_platform_socket));
    if (!sock) {
        CloseHandle(pipe);
        return YETTY_ERR(yetty_ipc_socket, "out of memory");
    }

    sock->handle = pipe;
    sock->is_listener = 0;

    return YETTY_OK(yetty_ipc_socket, sock);
}

void yetty_platform_socket_close(yetty_ipc_socket_t sock)
{
    if (!sock) {
        return;
    }

    if (sock->handle != INVALID_HANDLE_VALUE) {
        if (sock->is_listener) {
            DisconnectNamedPipe(sock->handle);
        }
        CloseHandle(sock->handle);
    }

    free(sock);
}

struct yetty_ipc_socket_result yetty_platform_socket_accept(yetty_ipc_socket_t sock)
{
    struct yetty_platform_socket *client;
    HANDLE new_pipe;

    if (!sock || !sock->is_listener) {
        return YETTY_ERR(yetty_ipc_socket, "not a listening socket");
    }

    /* Check for pending connection (non-blocking) */
    if (!ConnectNamedPipe(sock->handle, NULL)) {
        DWORD err = GetLastError();
        if (err == ERROR_PIPE_CONNECTED) {
            /* Client already connected */
        } else if (err == ERROR_IO_PENDING || err == ERROR_PIPE_LISTENING) {
            return YETTY_OK(yetty_ipc_socket, YETTY_IPC_SOCKET_INVALID);
        } else {
            return YETTY_ERR(yetty_ipc_socket, "ConnectNamedPipe failed");
        }
    }

    /* Move current pipe to client, create new listener */
    client = calloc(1, sizeof(struct yetty_platform_socket));
    if (!client) {
        return YETTY_ERR(yetty_ipc_socket, "out of memory");
    }

    client->handle = sock->handle;
    client->is_listener = 0;

    /* Create new pipe instance for next client */
    new_pipe = CreateNamedPipeA(sock->pipe_name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, NULL);

    if (new_pipe == INVALID_HANDLE_VALUE) {
        /* Restore and fail */
        sock->handle = client->handle;
        free(client);
        return YETTY_ERR(yetty_ipc_socket, "failed to create new pipe instance");
    }

    sock->handle = new_pipe;

    return YETTY_OK(yetty_ipc_socket, client);
}

struct yetty_ycore_size_result yetty_platform_socket_send(yetty_ipc_socket_t sock, const void *data,
                                                     size_t len)
{
    DWORD written = 0;

    if (!sock) {
        return YETTY_ERR(yetty_ycore_size, "invalid socket");
    }

    if (!WriteFile(sock->handle, data, (DWORD)len, &written, NULL)) {
        return YETTY_ERR(yetty_ycore_size, "WriteFile failed");
    }

    return YETTY_OK(yetty_ycore_size, (size_t)written);
}

struct yetty_ycore_size_result yetty_platform_socket_recv(yetty_ipc_socket_t sock, void *buf,
                                                     size_t max_len)
{
    DWORD bytes_read = 0;
    DWORD available = 0;

    if (!sock) {
        return YETTY_ERR(yetty_ycore_size, "invalid socket");
    }

    /* Check if data available (non-blocking) */
    if (!PeekNamedPipe(sock->handle, NULL, 0, NULL, &available, NULL) || available == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    if (!ReadFile(sock->handle, buf, (DWORD)max_len, &bytes_read, NULL)) {
        return YETTY_ERR(yetty_ycore_size, "ReadFile failed");
    }

    return YETTY_OK(yetty_ycore_size, (size_t)bytes_read);
}

int yetty_platform_socket_would_block(void)
{
    DWORD err = GetLastError();
    return err == ERROR_IO_PENDING || err == ERROR_NO_DATA;
}

int yetty_platform_socket_has_data(yetty_ipc_socket_t sock)
{
    DWORD available = 0;

    if (!sock || sock->handle == INVALID_HANDLE_VALUE) {
        return 0;
    }

    if (!PeekNamedPipe(sock->handle, NULL, 0, NULL, &available, NULL)) {
        return 0;
    }

    return available > 0;
}

int yetty_platform_socket_get_fd(yetty_ipc_socket_t sock)
{
    /* No fd on Windows, use has_data for polling */
    (void)sock;
    return -1;
}

void yetty_platform_socket_unlink(const char *path)
{
    /* No-op on Windows - named pipes are automatically cleaned up */
    (void)path;
}
