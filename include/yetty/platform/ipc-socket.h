#ifndef YETTY_YPLATFORM_IPC_SOCKET_H
#define YETTY_YPLATFORM_IPC_SOCKET_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Platform IPC socket abstraction.
 * - Linux/macOS: Unix domain sockets (AF_UNIX)
 * - Windows: Named pipes (\\.\pipe\...)
 */

/* Opaque handle for IPC socket */
typedef struct yetty_platform_socket *yetty_ipc_socket_t;

#define YETTY_IPC_SOCKET_INVALID NULL

/* Result types */
YETTY_YRESULT_DECLARE(yetty_ipc_socket, yetty_ipc_socket_t);

/* Maximum path length for socket/pipe */
#define YETTY_IPC_SOCKET_PATH_MAX 256

/*
 * Create a listening IPC socket at the given path.
 * - Linux/macOS: Creates Unix domain socket at path
 * - Windows: Creates named pipe at path (\\.\pipe\name)
 *
 * If path is NULL, generates default path:
 * - Linux: $XDG_RUNTIME_DIR/yetty/yetty-<pid>.sock
 * - Windows: \\.\pipe\yetty-<pid>
 *
 * Copies the actual path to path_out if not NULL (must be PATH_MAX bytes).
 */
struct yetty_ipc_socket_result yetty_platform_socket_listen(const char *path, char *path_out);

/*
 * Connect to an IPC socket at the given path.
 */
struct yetty_ipc_socket_result yetty_platform_socket_connect(const char *path);

/*
 * Close and destroy an IPC socket.
 * Safe to call with NULL.
 */
void yetty_platform_socket_close(yetty_ipc_socket_t sock);

/*
 * Accept a new connection on a listening socket.
 * Returns YETTY_IPC_SOCKET_INVALID if no connection pending (non-blocking).
 */
struct yetty_ipc_socket_result yetty_platform_socket_accept(yetty_ipc_socket_t sock);

/*
 * Send data on a connected socket.
 * Returns number of bytes sent, or 0 if would block.
 */
struct yetty_ycore_size_result yetty_platform_socket_send(yetty_ipc_socket_t sock, const void *data,
                                                     size_t len);

/*
 * Receive data from a connected socket.
 * Returns number of bytes received, or 0 if would block or EOF.
 */
struct yetty_ycore_size_result yetty_platform_socket_recv(yetty_ipc_socket_t sock, void *buf,
                                                     size_t max_len);

/*
 * Check if last operation would block.
 */
int yetty_platform_socket_would_block(void);

/*
 * Check if socket has data available to read.
 */
int yetty_platform_socket_has_data(yetty_ipc_socket_t sock);

/*
 * Get the file descriptor (Linux/macOS) or HANDLE (Windows) for polling.
 * On Windows, returns -1 (use has_data for polling).
 */
int yetty_platform_socket_get_fd(yetty_ipc_socket_t sock);

/*
 * Delete the socket file (Unix only, no-op on Windows).
 * Call before listen() to clean up stale sockets.
 */
void yetty_platform_socket_unlink(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_IPC_SOCKET_H */
