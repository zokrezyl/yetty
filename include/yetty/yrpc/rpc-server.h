#ifndef YETTY_YRPC_RPC_SERVER_H
#define YETTY_YRPC_RPC_SERVER_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/yrpc/rpc-message.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yrpc_server;
struct yetty_yplatform_event_loop;

/* Result types */
YETTY_YRESULT_DECLARE(yetty_rpc_server_ptr, struct yetty_yrpc_server *);

/*
 * Handler result: success with optional response data, or error message.
 * For requests, handlers must provide a response.
 * For notifications, response is ignored.
 */
struct yetty_rpc_handler_result {
    int ok;
    union {
        struct {
            const uint8_t *data; /* NULL for bool response */
            size_t len;
            int bool_value; /* used when data is NULL */
        } value;
        const char *error;
    };
};

/* Create success result with msgpack data */
#define YETTY_YRPC_HANDLER_OK_DATA(d, l)                                                           \
    ((struct yetty_rpc_handler_result){.ok = 1, .value = {.data = (d), .len = (l)}})

/* Create success result with bool */
#define YETTY_YRPC_HANDLER_OK_BOOL(v)                                                              \
    ((struct yetty_rpc_handler_result){.ok = 1,                                                    \
                                       .value = {.data = NULL, .len = 0, .bool_value = (v)}})

/* Create error result */
#define YETTY_YRPC_HANDLER_ERR(msg) ((struct yetty_rpc_handler_result){.ok = 0, .error = (msg)})

/*
 * RPC handler function type.
 * Receives parsed message and userdata, returns handler result.
 */
typedef struct yetty_rpc_handler_result (*yetty_rpc_handler_fn)(const struct yetty_yrpc_message *msg,
                                                                void *userdata);

/*
 * Create RPC server with event loop for dispatching events.
 * Does not start listening until yetty_rpc_server_start() is called.
 * Registers built-in handlers for EventLoop channel automatically.
 */
struct yetty_rpc_server_ptr_result yetty_yrpc_server_create(
    struct yetty_yplatform_event_loop *event_loop);

/*
 * Destroy RPC server.
 * Stops server if running, closes all connections.
 */
struct yetty_ycore_void_result yetty_yrpc_server_destroy(struct yetty_yrpc_server *server);

/*
 * Start RPC server on TCP socket.
 * Host is the bind address (e.g., "127.0.0.1" or "0.0.0.0").
 * Port is the TCP port number.
 */
struct yetty_ycore_void_result yetty_yrpc_server_start(struct yetty_yrpc_server *server,
                                                      const char *host, int port);

/*
 * Get the port the server is listening on.
 * Returns 0 if server is not running.
 */
int yetty_yrpc_server_get_port(const struct yetty_yrpc_server *server);

/*
 * Stop RPC server.
 * Closes listening socket and all client connections.
 */
struct yetty_ycore_void_result yetty_yrpc_server_stop(struct yetty_yrpc_server *server);

/*
 * Check if server is running.
 */
int yetty_yrpc_server_is_running(const struct yetty_yrpc_server *server);

/*
 * Register a handler for a specific channel and method.
 * Only one handler per (channel, method) pair.
 * Returns error if handler already registered.
 */
struct yetty_ycore_void_result yetty_yrpc_server_register_handler(struct yetty_yrpc_server *server,
                                                                 uint32_t channel,
                                                                 const char *method,
                                                                 yetty_rpc_handler_fn handler,
                                                                 void *userdata);

/*
 * Unregister a handler.
 */
struct yetty_ycore_void_result yetty_yrpc_server_unregister_handler(struct yetty_yrpc_server *server,
                                                                   uint32_t channel,
                                                                   const char *method);

/*
 * Get the number of connected clients.
 */
size_t yetty_yrpc_server_client_count(const struct yetty_yrpc_server *server);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRPC_RPC_SERVER_H */
