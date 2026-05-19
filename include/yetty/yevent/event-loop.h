#ifndef YETTY_YEVENT_EVENT_LOOP_H
#define YETTY_YEVENT_EVENT_LOOP_H

#include <yetty/yevent/event.h>
#include <yetty/ycore/result.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int yetty_yevent_pipe_id;
typedef int yetty_yevent_timer_id;
typedef int yetty_yevent_tcp_server_id;
typedef int yetty_yevent_tcp_client_id;

/* Result types for this module */
YETTY_YRESULT_DECLARE(yetty_yevent_pipe_id, yetty_yevent_pipe_id);
YETTY_YRESULT_DECLARE(yetty_yevent_timer_id, yetty_yevent_timer_id);
YETTY_YRESULT_DECLARE(yetty_yevent_tcp_server_id, yetty_yevent_tcp_server_id);
YETTY_YRESULT_DECLARE(yetty_yevent_tcp_client_id, yetty_yevent_tcp_client_id);

/* TCP connection handle (opaque, passed to callbacks) */
struct yetty_yevent_conn;

/* TCP server listener callbacks */
struct yetty_yevent_tcp_server_callbacks {
    void *ctx;
    /* New client connected, returns connection-specific context */
    void *(*on_connect)(void *ctx, struct yetty_yevent_conn *conn);
    /* Allocate buffer for reading */
    void (*on_alloc)(void *conn_ctx, size_t suggested, char **buf, size_t *len);
    /* Data received */
    void (*on_data)(void *conn_ctx, struct yetty_yevent_conn *conn, const char *data, long nread);
    /* Client disconnected */
    void (*on_disconnect)(void *conn_ctx);
};

/* TCP client callbacks */
struct yetty_yevent_tcp_client_callbacks {
    void *ctx;
    /* Connected to server */
    void (*on_connect)(void *ctx, struct yetty_yevent_conn *conn);
    /* Connection failed */
    void (*on_connect_error)(void *ctx, const char *error);
    /* Allocate buffer for reading */
    void (*on_alloc)(void *ctx, size_t suggested, char **buf, size_t *len);
    /* Data received */
    void (*on_data)(void *ctx, struct yetty_yevent_conn *conn, const char *data, long nread);
    /* Disconnected */
    void (*on_disconnect)(void *ctx);
};

struct yetty_yevent_event_loop;
struct yetty_yevent_event_listener;
struct yetty_platform_pty_pipe_source;

/* Pipe alloc callback — called by event loop to get a buffer for reading */
typedef void (*yetty_pipe_alloc_cb)(void *ctx, size_t suggested_size, char **buf, size_t *buflen);

/* Pipe read callback — called by event loop when data arrives.
 * nread > 0: data available in buf/nread.
 * nread < 0: error or EOF. */
typedef void (*yetty_pipe_read_cb)(void *ctx, const char *buf, long nread);

/* Event listener callback - returns int (1=handled, 0=not) or error */
typedef struct yetty_ycore_int_result (*yetty_ycore_event_handler)(
    struct yetty_yevent_event_listener *listener, const struct yetty_yui_event *event);

/* Event listener - embed as first member in your listener struct */
struct yetty_yevent_event_listener {
    yetty_ycore_event_handler handler;
};

/* Event loop ops */
struct yetty_yevent_event_loop_ops {
    struct yetty_ycore_void_result (*destroy)(struct yetty_yevent_event_loop *self);

    struct yetty_ycore_void_result (*start)(struct yetty_yevent_event_loop *self);
    struct yetty_ycore_void_result (*stop)(struct yetty_yevent_event_loop *self);

    struct yetty_ycore_void_result (*register_listener)(
        struct yetty_yevent_event_loop *self, enum yetty_yevent_event_type type,
        struct yetty_yevent_event_listener *listener, int priority);
    struct yetty_ycore_void_result (*deregister_listener)(
        struct yetty_yevent_event_loop *self, enum yetty_yevent_event_type type,
        struct yetty_yevent_event_listener *listener);

    struct yetty_ycore_int_result (*dispatch)(struct yetty_yevent_event_loop *self,
                                              const struct yetty_yui_event *event);
    struct yetty_ycore_void_result (*broadcast)(struct yetty_yevent_event_loop *self,
                                                const struct yetty_yui_event *event);

    /* PTY pipe — uv_pipe_t with uv_read_start, caller provides callbacks */
    struct yetty_yevent_pipe_id_result (*register_pty_pipe)(
        struct yetty_yevent_event_loop *self, struct yetty_platform_pty_pipe_source *source,
        yetty_pipe_alloc_cb alloc_cb, yetty_pipe_read_cb read_cb, void *cb_ctx);
    struct yetty_ycore_void_result (*unregister_pty_pipe)(struct yetty_yevent_event_loop *self,
                                                          yetty_yevent_pipe_id id);

    /* Timer management */
    struct yetty_yevent_timer_id_result (*create_timer)(struct yetty_yevent_event_loop *self);
    struct yetty_ycore_void_result (*config_timer)(struct yetty_yevent_event_loop *self,
                                                   yetty_yevent_timer_id id, int timeout_ms);
    struct yetty_ycore_void_result (*start_timer)(struct yetty_yevent_event_loop *self,
                                                  yetty_yevent_timer_id id);
    struct yetty_ycore_void_result (*stop_timer)(struct yetty_yevent_event_loop *self,
                                                 yetty_yevent_timer_id id);
    struct yetty_ycore_void_result (*destroy_timer)(struct yetty_yevent_event_loop *self,
                                                    yetty_yevent_timer_id id);
    struct yetty_ycore_void_result (*register_timer_listener)(
        struct yetty_yevent_event_loop *self, yetty_yevent_timer_id id,
        struct yetty_yevent_event_listener *listener);
    /* Remove a previously-registered timer listener. Safe to call from
     * inside the listener's own handler (the dispatcher snapshots the
     * listener pointer before invoking). No-op if the listener wasn't
     * registered on this timer. */
    struct yetty_ycore_void_result (*deregister_timer_listener)(
        struct yetty_yevent_event_loop *self, yetty_yevent_timer_id id,
        struct yetty_yevent_event_listener *listener);

    /* TCP server */
    struct yetty_yevent_tcp_server_id_result (*create_tcp_server)(
        struct yetty_yevent_event_loop *self, const char *host, int port,
        const struct yetty_yevent_tcp_server_callbacks *callbacks);
    struct yetty_ycore_void_result (*start_tcp_server)(struct yetty_yevent_event_loop *self,
                                                       yetty_yevent_tcp_server_id id);
    struct yetty_ycore_void_result (*stop_tcp_server)(struct yetty_yevent_event_loop *self,
                                                      yetty_yevent_tcp_server_id id);

    /* TCP client */
    struct yetty_yevent_tcp_client_id_result (*create_tcp_client)(
        struct yetty_yevent_event_loop *self, const char *host, int port,
        const struct yetty_yevent_tcp_client_callbacks *callbacks);
    struct yetty_ycore_void_result (*stop_tcp_client)(struct yetty_yevent_event_loop *self,
                                                      yetty_yevent_tcp_client_id id);

    /* TCP connection operations (works for both server and client connections) */
    struct yetty_ycore_size_result (*tcp_send)(struct yetty_yevent_conn *conn, const void *data,
                                               size_t len);
    struct yetty_ycore_void_result (*tcp_close)(struct yetty_yevent_conn *conn);

    void (*request_render)(struct yetty_yevent_event_loop *self);

    /* Schedule fn(arg) to run on the loop thread. Thread-safe — may be
     * called from any thread, including the loop thread itself. The order
     * of dispatch matches submission order. Used by the GPU poll thread to
     * resume coroutines on the loop after wgpu callbacks fire. */
    void (*post_to_loop)(struct yetty_yevent_event_loop *self, void (*fn)(void *), void *arg);

    /* Stash a fatal error and tear the loop down. Used by external-callback
     * boundaries (libuv read/write completions, signal handlers, …) that
     * have no Result to propagate — the loop stops, `start()` returns the
     * stashed error wrapped, and the chain reaches main.
     *
     * MOVES ownership of the error: caller must NOT destroy it. Only the
     * FIRST call sticks; subsequent ones free their own chain. No-op if
     * the loop isn't running. */
    void (*post_fatal_error)(struct yetty_yevent_event_loop *self,
                             struct yetty_ycore_error error);
};

/* Event loop base */
struct yetty_yevent_event_loop {
    const struct yetty_yevent_event_loop_ops *ops;
};

/* Event loop creation - platform_input_pipe can be NULL */
struct yetty_ycore_xthread_event_pipe;
YETTY_YRESULT_DECLARE(yetty_ycore_event_loop, struct yetty_yevent_event_loop *);
struct yetty_ycore_event_loop_result yetty_ycore_event_loop_create(
    struct yetty_ycore_xthread_event_pipe *pipe);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YEVENT_EVENT_LOOP_H */
