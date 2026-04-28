#ifndef YETTY_YPLATFORM_SOCKET_H
#define YETTY_YPLATFORM_SOCKET_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Socket file descriptor type */
typedef int yetty_socket_fd;

#define YETTY_SOCKET_INVALID (-1)

/* Result type for socket fd */
YETTY_YRESULT_DECLARE(yetty_socket_fd, yetty_socket_fd);

/* Initialize platform socket subsystem (call once at startup) */
int yetty_yplatform_socket_init(void);

/* Cleanup platform socket subsystem (call once at shutdown) */
void yetty_yplatform_socket_cleanup(void);

/* Create a TCP socket */
struct yetty_socket_fd_result yetty_yplatform_socket_create_tcp(void);

/* Close a socket */
void yetty_yplatform_socket_close(yetty_socket_fd fd);

/* Set socket to non-blocking mode */
struct yetty_ycore_void_result yetty_yplatform_socket_set_nonblocking(yetty_socket_fd fd);

/* Set TCP_NODELAY option */
struct yetty_ycore_void_result yetty_yplatform_socket_set_nodelay(yetty_socket_fd fd, int enable);

/* Set SO_REUSEADDR option */
struct yetty_ycore_void_result yetty_yplatform_socket_set_reuseaddr(yetty_socket_fd fd, int enable);

/* Bind socket to address */
struct yetty_ycore_void_result yetty_yplatform_socket_bind(yetty_socket_fd fd, uint16_t port);

/* Listen for connections */
struct yetty_ycore_void_result yetty_yplatform_socket_listen(yetty_socket_fd fd, int backlog);

/* Accept a connection (returns new fd or YETTY_SOCKET_INVALID on EAGAIN) */
struct yetty_socket_fd_result yetty_yplatform_socket_accept(yetty_socket_fd fd);

/* Connect to address (non-blocking, returns in-progress status) */
struct yetty_ycore_void_result yetty_yplatform_socket_connect(yetty_socket_fd fd, const char *host,
                                                              uint16_t port);

/* Check if connect completed (call after poll indicates writable) */
struct yetty_ycore_void_result yetty_yplatform_socket_connect_check(yetty_socket_fd fd);

/* Send data (non-blocking) */
struct yetty_ycore_size_result yetty_yplatform_socket_send(yetty_socket_fd fd, const void *data,
                                                           size_t len);

/* Receive data (non-blocking) */
struct yetty_ycore_size_result yetty_yplatform_socket_recv(yetty_socket_fd fd, void *buf,
                                                           size_t max_len);

/* Check if last operation would block */
int yetty_yplatform_socket_would_block(void);

/* Check if connect is in progress */
int yetty_yplatform_socket_connect_in_progress(void);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_SOCKET_H */
