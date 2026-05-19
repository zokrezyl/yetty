/* libuv-event-loop.c - Event loop using libuv with uv_pipe_t for all pipes
 *
 * Works on both Unix and Windows:
 * - Input pipe (bridge): uv_pipe_t reads structured events from main thread
 * - PTY pipe: uv_pipe_t reads raw PTY output, feeds to the wire state machine
 * - Both use IOCP on Windows, epoll/kqueue on Unix — zero CPU when idle
 */

#include <yetty/yevent/event-loop.h>
#include <yetty/ycore/types.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/pty-pipe-source.h>
#include <yetty/ytrace/ytrace.h>
#include <uv.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <signal.h>
#endif

#define MAX_LISTENERS_PER_TIMER 16
#define MAX_LISTENERS_PER_TYPE 64
#define MAX_PTY_PIPES 16
#define MAX_TIMERS 64
#define MAX_TCP_SERVERS 4
#define MAX_TCP_CLIENTS 16
#define TCP_CLIENT_BUFFER_SIZE 65536

/* Per-connection write-request pool. tcp_send must own the bytes until the
 * write completes (uv_write is async), so we copy the caller's data into a
 * heap block trailing the uv_write_t. Hot paths (yvnc encode loop) issue
 * hundreds of small sends per frame, so we keep a freelist of fixed-size
 * blocks per connection instead of malloc+free per send. Payloads larger
 * than POOL_BUF_SIZE bypass the pool (one-shot malloc).
 *
 * Capacity is tuned for VNC: typical sends are 9..3000 bytes (tile header,
 * JPEG tile). Pool depth caps idle memory at MAX_POOL * POOL_BUF_SIZE per
 * connection. */
#define LIBUV_WRITE_POOL_BUF_SIZE 4096
#define LIBUV_WRITE_POOL_MAX_DEPTH 128

struct libuv_write_req;

/* TCP connection - used for both server connections and client connections */
struct yetty_yevent_conn {
    uv_tcp_t tcp;
    struct yetty_yplatform_libuv_event_loop *loop_impl;
    void *conn_ctx; /* connection-specific context from on_connect */
    struct yetty_yplatform_tcp_server_handle *server; /* NULL for client connections */
    struct yetty_yplatform_tcp_client_handle *client; /* NULL for server connections */
    int active;

    /* Singly-linked freelist of recycled write requests, all sized
     * POOL_BUF_SIZE. Bounded by LIBUV_WRITE_POOL_MAX_DEPTH; excess
     * completions free()'d. */
    struct libuv_write_req *write_pool_head;
    size_t write_pool_depth;
};

/* TCP server */
struct yetty_yplatform_tcp_server_handle {
    uv_tcp_t tcp;
    struct yetty_yplatform_libuv_event_loop *loop_impl;
    int id;
    char host[256];
    int port;
    int active;
    struct yetty_yevent_tcp_server_callbacks callbacks;
    struct yetty_yevent_conn *conns[MAX_TCP_CLIENTS];
    int conn_count;
};

/* TCP client (outbound connection) */
struct yetty_yplatform_tcp_client_handle {
    struct yetty_yevent_conn conn;
    uv_connect_t connect_req;
    uv_getaddrinfo_t resolve_req;
    struct yetty_yplatform_libuv_event_loop *loop_impl;
    int id;
    char host[256];
    int port;
    int active;
    struct yetty_yevent_tcp_client_callbacks callbacks;
};

struct yetty_yplatform_pty_pipe_handle {
    uv_pipe_t pipe;
    yetty_pipe_alloc_cb alloc_cb;
    yetty_pipe_read_cb read_cb;
    void *cb_ctx;
    int active;
};

struct yetty_yplatform_timer_handle {
    uv_timer_t timer;
    int id;
    int timeout_ms;
    int active;
    struct yetty_yevent_event_listener *listeners[MAX_LISTENERS_PER_TIMER];
    int listener_count;
};

struct yetty_yplatform_prioritized_listener {
    struct yetty_yevent_event_listener *listener;
    int priority;
};

/* Single-linked queue of (fn, arg) callbacks scheduled via post_to_loop. */
struct yetty_yplatform_post_node {
    void (*fn)(void *);
    void *arg;
    struct yetty_yplatform_post_node *next;
};

struct yetty_yplatform_libuv_event_loop {
    struct yetty_yevent_event_loop base;
    uv_loop_t *loop;

    struct yetty_yplatform_prioritized_listener listeners[YETTY_YCORE_COUNT]
                                                         [MAX_LISTENERS_PER_TYPE];
    int listener_counts[YETTY_YCORE_COUNT];

    struct yetty_yplatform_pty_pipe_handle pty_pipes[MAX_PTY_PIPES];
    int next_pty_pipe_id;

    struct yetty_yplatform_timer_handle timers[MAX_TIMERS];
    int next_timer_id;

    struct yetty_yplatform_tcp_server_handle tcp_servers[MAX_TCP_SERVERS];
    int next_tcp_server_id;

    struct yetty_yplatform_tcp_client_handle tcp_clients[MAX_TCP_CLIENTS];
    int next_tcp_client_id;

    struct yetty_ycore_xthread_event_pipe *platform_input_pipe;
    uv_pipe_t input_pipe;
    int input_pipe_active;

    uv_async_t render_async;
    int render_pending;

    /* Cross-thread post-to-loop queue (drained by on_post_async). */
    uv_async_t post_async;
    uv_mutex_t post_mutex;
    struct yetty_yplatform_post_node *post_head;
    struct yetty_yplatform_post_node *post_tail;

#ifndef _WIN32
    uv_signal_t sigint_handle;
    uv_signal_t sigterm_handle;
#endif

    /* Set by post_fatal_error from any thread / callback. The loop tears
     * itself down (uv_stop); libuv_start checks this on exit and returns
     * an error wrapping the stashed chain. msg is a string literal; cause
     * is the heap chain transferred from the caller. */
    int                       fatal_set;       /* 0/1, set under post_mutex */
    struct yetty_ycore_error  fatal_error;
};

/* Forward declarations */
static struct yetty_ycore_void_result libuv_destroy(struct yetty_yevent_event_loop *self);
static struct yetty_ycore_void_result libuv_start(struct yetty_yevent_event_loop *self);
static struct yetty_ycore_void_result libuv_stop(struct yetty_yevent_event_loop *self);
static struct yetty_ycore_void_result libuv_register_listener(
    struct yetty_yevent_event_loop *self, enum yetty_yevent_event_type type,
    struct yetty_yevent_event_listener *listener, int priority);
static struct yetty_ycore_void_result libuv_deregister_listener(
    struct yetty_yevent_event_loop *self, enum yetty_yevent_event_type type,
    struct yetty_yevent_event_listener *listener);
static struct yetty_ycore_int_result libuv_dispatch(struct yetty_yevent_event_loop *self,
                                                    const struct yetty_yui_event *event);
static struct yetty_ycore_void_result libuv_broadcast(struct yetty_yevent_event_loop *self,
                                                      const struct yetty_yui_event *event);
static struct yetty_yevent_pipe_id_result libuv_register_pty_pipe(
    struct yetty_yevent_event_loop *self, struct yetty_platform_pty_pipe_source *source,
    yetty_pipe_alloc_cb alloc_cb, yetty_pipe_read_cb read_cb, void *cb_ctx);
static struct yetty_ycore_void_result libuv_unregister_pty_pipe(
    struct yetty_yevent_event_loop *self, yetty_yevent_pipe_id id);
static struct yetty_yevent_timer_id_result libuv_create_timer(struct yetty_yevent_event_loop *self);
static struct yetty_ycore_void_result libuv_config_timer(struct yetty_yevent_event_loop *self,
                                                         yetty_yevent_timer_id id, int timeout_ms);
static struct yetty_ycore_void_result libuv_start_timer(struct yetty_yevent_event_loop *self,
                                                        yetty_yevent_timer_id id);
static struct yetty_ycore_void_result libuv_stop_timer(struct yetty_yevent_event_loop *self,
                                                       yetty_yevent_timer_id id);
static struct yetty_ycore_void_result libuv_destroy_timer(struct yetty_yevent_event_loop *self,
                                                          yetty_yevent_timer_id id);
static struct yetty_ycore_void_result libuv_register_timer_listener(
    struct yetty_yevent_event_loop *self, yetty_yevent_timer_id id,
    struct yetty_yevent_event_listener *listener);
static struct yetty_ycore_void_result libuv_deregister_timer_listener(
    struct yetty_yevent_event_loop *self, yetty_yevent_timer_id id,
    struct yetty_yevent_event_listener *listener);
static struct yetty_yevent_tcp_server_id_result libuv_create_tcp_server(
    struct yetty_yevent_event_loop *self, const char *host, int port,
    const struct yetty_yevent_tcp_server_callbacks *callbacks);
static struct yetty_ycore_void_result libuv_start_tcp_server(struct yetty_yevent_event_loop *self,
                                                             yetty_yevent_tcp_server_id id);
static struct yetty_ycore_void_result libuv_stop_tcp_server(struct yetty_yevent_event_loop *self,
                                                            yetty_yevent_tcp_server_id id);
static struct yetty_yevent_tcp_client_id_result libuv_create_tcp_client(
    struct yetty_yevent_event_loop *self, const char *host, int port,
    const struct yetty_yevent_tcp_client_callbacks *callbacks);
static struct yetty_ycore_void_result libuv_stop_tcp_client(struct yetty_yevent_event_loop *self,
                                                            yetty_yevent_tcp_client_id id);
static struct yetty_ycore_size_result libuv_tcp_send(struct yetty_yevent_conn *conn,
                                                     const void *data, size_t len);
static struct yetty_ycore_void_result libuv_tcp_close(struct yetty_yevent_conn *conn);
static void write_pool_drain(struct yetty_yevent_conn *conn);
static void libuv_request_render(struct yetty_yevent_event_loop *self);
static void libuv_post_to_loop(struct yetty_yevent_event_loop *self, void (*fn)(void *), void *arg);
static void libuv_post_fatal_error(struct yetty_yevent_event_loop *self,
                                   struct yetty_ycore_error error);

static const struct yetty_yevent_event_loop_ops libuv_ops = {
    .destroy = libuv_destroy,
    .start = libuv_start,
    .stop = libuv_stop,
    .register_listener = libuv_register_listener,
    .deregister_listener = libuv_deregister_listener,
    .dispatch = libuv_dispatch,
    .broadcast = libuv_broadcast,
    .register_pty_pipe = libuv_register_pty_pipe,
    .unregister_pty_pipe = libuv_unregister_pty_pipe,
    .create_timer = libuv_create_timer,
    .config_timer = libuv_config_timer,
    .start_timer = libuv_start_timer,
    .stop_timer = libuv_stop_timer,
    .destroy_timer = libuv_destroy_timer,
    .register_timer_listener = libuv_register_timer_listener,
    .deregister_timer_listener = libuv_deregister_timer_listener,
    .create_tcp_server = libuv_create_tcp_server,
    .start_tcp_server = libuv_start_tcp_server,
    .stop_tcp_server = libuv_stop_tcp_server,
    .create_tcp_client = libuv_create_tcp_client,
    .stop_tcp_client = libuv_stop_tcp_client,
    .tcp_send = libuv_tcp_send,
    .tcp_close = libuv_tcp_close,
    .request_render = libuv_request_render,
    .post_to_loop = libuv_post_to_loop,
    .post_fatal_error = libuv_post_fatal_error,
};

/* Callbacks */

#ifndef _WIN32
YETTY_EXTERNAL_CALLBACK
static void on_signal(uv_signal_t *handle, int signum)
{
    struct yetty_yplatform_libuv_event_loop *impl = handle->data;
    struct yetty_yui_event ev = {.type = YETTY_YCORE_SHUTDOWN};
    (void)signum;

    /* Route SIGINT/SIGTERM through the same SHUTDOWN handler that the
     * window-close callback uses, so terminals get a chance to mark
     * shutting_down=1 (skipping further GPU work) before the loop stops.
     * The SHUTDOWN listener (yetty_event_handler) calls uv_stop itself;
     * the explicit uv_stop below is a safety net in case no listener is
     * registered. */
    libuv_dispatch(&impl->base, &ev);
    uv_stop(impl->loop);
}
#endif

YETTY_EXTERNAL_CALLBACK
static void on_render_async(uv_async_t *handle)
{
    struct yetty_yplatform_libuv_event_loop *impl = handle->data;
    struct yetty_yui_event event = {0};

    ydebug("on_render_async: render_pending=%d", impl->render_pending);
    if (impl->render_pending) {
        impl->render_pending = 0;
        event.type = YETTY_YCORE_RENDER;
        libuv_dispatch(&impl->base, &event);
    }
}

/* Drain the post-to-loop queue. Runs on the loop thread; called whenever
 * post_async is signalled by libuv_post_to_loop (from any thread). */
static void on_post_async(uv_async_t *handle)
{
    struct yetty_yplatform_libuv_event_loop *impl = handle->data;
    struct yetty_yplatform_post_node *batch;

    /* Detach the whole list under the lock; invoke callbacks unlocked so
     * they can themselves call post_to_loop without deadlocking. */
    uv_mutex_lock(&impl->post_mutex);
    batch = impl->post_head;
    impl->post_head = NULL;
    impl->post_tail = NULL;
    uv_mutex_unlock(&impl->post_mutex);

    while (batch) {
        struct yetty_yplatform_post_node *node = batch;
        batch = node->next;
        ydebug("on_post_async: invoking fn=%p arg=%p", (void *)node->fn, node->arg);
        if (node->fn) {
            node->fn(node->arg);
        }
        free(node);
    }
}

/* Input pipe (bridge) callbacks — uv_pipe_t reads structured events */

static void on_input_pipe_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    (void)handle;
    (void)suggested_size;
    static char input_pipe_buf[4096];
    buf->base = input_pipe_buf;
    buf->len = sizeof(input_pipe_buf);
}

YETTY_EXTERNAL_CALLBACK
static void on_input_pipe_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    struct yetty_yplatform_libuv_event_loop *impl = stream->data;

    if (nread <= 0) {
        return;
    }

    size_t offset = 0;
    while (offset + sizeof(struct yetty_yui_event) <= (size_t)nread) {
        struct yetty_yui_event event;
        memcpy(&event, buf->base + offset, sizeof(event));
        libuv_dispatch(&impl->base, &event);
        offset += sizeof(struct yetty_yui_event);
    }
}

/* PTY pipe callbacks — delegate to caller-provided callbacks */

static void on_pty_pipe_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    struct yetty_yplatform_pty_pipe_handle *ph = handle->data;
    char *base = NULL;
    size_t len = 0;
    ph->alloc_cb(ph->cb_ctx, suggested_size, &base, &len);
    buf->base = base;
    buf->len = len;
}

static void on_pty_pipe_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    struct yetty_yplatform_pty_pipe_handle *ph = stream->data;
    ydebug("on_pty_pipe_read: nread=%zd buf_base=%p buf_len=%zu", nread,
           (void *)(buf ? buf->base : NULL), (size_t)(buf ? buf->len : 0));
    ph->read_cb(ph->cb_ctx, buf->base, (long)nread);
}

static void on_timer(uv_timer_t *handle)
{
    struct yetty_yplatform_timer_handle *th = handle->data;
    struct yetty_yui_event event = {0};

    event.type = YETTY_YCORE_TIMER;
    event.timer.timer_id = th->id;

    /* Snapshot the listener array so handlers can safely call
     * register_timer_listener / deregister_timer_listener (including
     * on themselves) without breaking our iteration. */
    struct yetty_yevent_event_listener *snapshot[MAX_LISTENERS_PER_TIMER];
    int n = th->listener_count;
    if (n > MAX_LISTENERS_PER_TIMER) n = MAX_LISTENERS_PER_TIMER;
    for (int i = 0; i < n; i++) {
        snapshot[i] = th->listeners[i];
    }
    for (int i = 0; i < n; i++) {
        if (snapshot[i] && snapshot[i]->handler) {
            snapshot[i]->handler(snapshot[i], &event);
        }
    }
}

/* Implementation */

static struct yetty_ycore_void_result libuv_destroy(struct yetty_yevent_event_loop *self)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);

#ifndef _WIN32
    uv_signal_stop(&impl->sigint_handle);
    uv_signal_stop(&impl->sigterm_handle);
    uv_close((uv_handle_t *)&impl->sigint_handle, NULL);
    uv_close((uv_handle_t *)&impl->sigterm_handle, NULL);
#endif
    uv_close((uv_handle_t *)&impl->render_async, NULL);
    uv_close((uv_handle_t *)&impl->post_async, NULL);

    if (impl->input_pipe_active) {
        uv_close((uv_handle_t *)&impl->input_pipe, NULL);
    }

    /* Drain any leftover post-queue nodes. */
    while (impl->post_head) {
        struct yetty_yplatform_post_node *node = impl->post_head;
        impl->post_head = node->next;
        free(node);
    }
    uv_mutex_destroy(&impl->post_mutex);

    free(impl);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result libuv_start(struct yetty_yevent_event_loop *self)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    int r = uv_run(impl->loop, UV_RUN_DEFAULT);

    /* Fatal error stashed during the run takes precedence over uv_run's
     * own return: external callbacks (libuv read/write, signal handlers)
     * have no Result to propagate, so they call post_fatal_error which
     * stops the loop and parks the error chain here for surfacing. */
    if (impl->fatal_set) {
        impl->fatal_set = 0;
        /* Synthesise a Result that carries the stashed chain. Build it
         * as if YETTY_ERR_3 were composing it: msg = the stashed top,
         * cause = the stashed chain head. */
        struct yetty_ycore_void_result out = {
            .ok = 0,
            .error = impl->fatal_error,
        };
        impl->fatal_error = (struct yetty_ycore_error){0};
        return out;
    }

    if (r < 0) {
        return YETTY_ERR(yetty_ycore_void, "uv_run failed");
    }

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result libuv_stop(struct yetty_yevent_event_loop *self)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    uv_stop(impl->loop);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result libuv_register_listener(
    struct yetty_yevent_event_loop *self, enum yetty_yevent_event_type type,
    struct yetty_yevent_event_listener *listener, int priority)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    int count, i, insert_pos;

    if (!listener || type >= YETTY_YCORE_COUNT) {
        return YETTY_ERR(yetty_ycore_void, "invalid listener or type");
    }

    count = impl->listener_counts[type];
    if (count >= MAX_LISTENERS_PER_TYPE) {
        return YETTY_ERR(yetty_ycore_void, "too many listeners");
    }

    insert_pos = count;
    for (i = 0; i < count; i++) {
        if (impl->listeners[type][i].priority < priority) {
            insert_pos = i;
            break;
        }
    }

    for (i = count; i > insert_pos; i--) {
        impl->listeners[type][i] = impl->listeners[type][i - 1];
    }

    impl->listeners[type][insert_pos].listener = listener;
    impl->listeners[type][insert_pos].priority = priority;
    impl->listener_counts[type]++;

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result libuv_deregister_listener(
    struct yetty_yevent_event_loop *self, enum yetty_yevent_event_type type,
    struct yetty_yevent_event_listener *listener)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    int count, i, j;

    if (type >= YETTY_YCORE_COUNT) {
        return YETTY_OK_VOID();
    }

    count = impl->listener_counts[type];
    for (i = 0; i < count; i++) {
        if (impl->listeners[type][i].listener == listener) {
            for (j = i; j < count - 1; j++) {
                impl->listeners[type][j] = impl->listeners[type][j + 1];
            }
            impl->listener_counts[type]--;
            break;
        }
    }

    return YETTY_OK_VOID();
}

static struct yetty_ycore_int_result libuv_dispatch(struct yetty_yevent_event_loop *self,
                                                    const struct yetty_yui_event *event)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    int count, i;

    if (event->type >= YETTY_YCORE_COUNT) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    count = impl->listener_counts[event->type];
    for (i = 0; i < count; i++) {
        struct yetty_yevent_event_listener *listener = impl->listeners[event->type][i].listener;
        struct yetty_ycore_int_result result = listener->handler(listener, event);
        if (YETTY_IS_ERR(result)) {
            return result;
        }
        if (result.value) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }

    return YETTY_OK(yetty_ycore_int, 0);
}

static struct yetty_ycore_void_result libuv_broadcast(struct yetty_yevent_event_loop *self,
                                                      const struct yetty_yui_event *event)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    int t, count, i;

    for (t = 0; t < YETTY_YCORE_COUNT; t++) {
        count = impl->listener_counts[t];
        for (i = 0; i < count; i++) {
            struct yetty_yevent_event_listener *listener = impl->listeners[t][i].listener;
            listener->handler(listener, event);
        }
    }

    return YETTY_OK_VOID();
}

/* PTY pipe — uv_pipe_t with uv_read_start, data feeds to the wire state machine */

static struct yetty_yevent_pipe_id_result libuv_register_pty_pipe(
    struct yetty_yevent_event_loop *self, struct yetty_platform_pty_pipe_source *source,
    yetty_pipe_alloc_cb alloc_cb, yetty_pipe_read_cb read_cb, void *cb_ctx)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    struct yetty_yplatform_pty_pipe_handle *ph;
    int id, r;

    if (!source || !alloc_cb || !read_cb) {
        return YETTY_ERR(yetty_yevent_pipe_id, "invalid source or callbacks");
    }

    id = impl->next_pty_pipe_id++;
    if (id >= MAX_PTY_PIPES) {
        return YETTY_ERR(yetty_yevent_pipe_id, "too many pty pipes");
    }

    ph = &impl->pty_pipes[id];
    memset(ph, 0, sizeof(*ph));
    ph->alloc_cb = alloc_cb;
    ph->read_cb = read_cb;
    ph->cb_ctx = cb_ctx;

    r = uv_pipe_init(impl->loop, &ph->pipe, 0);
    if (r != 0) {
        return YETTY_ERR(yetty_yevent_pipe_id, "uv_pipe_init failed");
    }

    r = uv_pipe_open(&ph->pipe, (uv_file)source->abstract);
    if (r != 0) {
        return YETTY_ERR(yetty_yevent_pipe_id, "uv_pipe_open failed");
    }

    ph->pipe.data = ph;

    r = uv_read_start((uv_stream_t *)&ph->pipe, on_pty_pipe_alloc, on_pty_pipe_read);
    if (r != 0) {
        return YETTY_ERR(yetty_yevent_pipe_id, "uv_read_start failed");
    }

    ph->active = 1;
    yinfo("libuv_register_pty_pipe: registered fd=%d as pipe_id=%d", (int)source->abstract, id);
    return YETTY_OK(yetty_yevent_pipe_id, id);
}

static struct yetty_ycore_void_result libuv_unregister_pty_pipe(
    struct yetty_yevent_event_loop *self, yetty_yevent_pipe_id id)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);

    if (id < 0 || id >= MAX_PTY_PIPES) {
        return YETTY_ERR(yetty_ycore_void, "invalid pipe id");
    }

    if (impl->pty_pipes[id].active) {
        uv_read_stop((uv_stream_t *)&impl->pty_pipes[id].pipe);
        uv_close((uv_handle_t *)&impl->pty_pipes[id].pipe, NULL);
        impl->pty_pipes[id].active = 0;
    }

    return YETTY_OK_VOID();
}

/* Timers */

static struct yetty_yevent_timer_id_result libuv_create_timer(struct yetty_yevent_event_loop *self)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    int id = impl->next_timer_id++;
    struct yetty_yplatform_timer_handle *th;

    if (id >= MAX_TIMERS) {
        return YETTY_ERR(yetty_yevent_timer_id, "too many timers");
    }

    th = &impl->timers[id];
    memset(th, 0, sizeof(*th));
    th->id = id;
    uv_timer_init(impl->loop, &th->timer);
    th->timer.data = th;

    return YETTY_OK(yetty_yevent_timer_id, id);
}

static struct yetty_ycore_void_result libuv_config_timer(struct yetty_yevent_event_loop *self,
                                                         yetty_yevent_timer_id id, int timeout_ms)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);

    if (id < 0 || id >= MAX_TIMERS) {
        return YETTY_ERR(yetty_ycore_void, "invalid timer id");
    }

    impl->timers[id].timeout_ms = timeout_ms;

    if (impl->timers[id].active) {
        uv_timer_start(&impl->timers[id].timer, on_timer, timeout_ms, timeout_ms);
    }

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result libuv_start_timer(struct yetty_yevent_event_loop *self,
                                                        yetty_yevent_timer_id id)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    struct yetty_yplatform_timer_handle *th;

    if (id < 0 || id >= MAX_TIMERS) {
        return YETTY_ERR(yetty_ycore_void, "invalid timer id");
    }

    th = &impl->timers[id];
    uv_timer_start(&th->timer, on_timer, th->timeout_ms, th->timeout_ms);
    th->active = 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result libuv_stop_timer(struct yetty_yevent_event_loop *self,
                                                       yetty_yevent_timer_id id)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);

    if (id < 0 || id >= MAX_TIMERS) {
        return YETTY_ERR(yetty_ycore_void, "invalid timer id");
    }

    uv_timer_stop(&impl->timers[id].timer);
    impl->timers[id].active = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result libuv_destroy_timer(struct yetty_yevent_event_loop *self,
                                                          yetty_yevent_timer_id id)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);

    if (id < 0 || id >= MAX_TIMERS) {
        return YETTY_ERR(yetty_ycore_void, "invalid timer id");
    }

    uv_timer_stop(&impl->timers[id].timer);
    uv_close((uv_handle_t *)&impl->timers[id].timer, NULL);
    impl->timers[id].active = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result libuv_register_timer_listener(
    struct yetty_yevent_event_loop *self, yetty_yevent_timer_id id,
    struct yetty_yevent_event_listener *listener)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    struct yetty_yplatform_timer_handle *th;

    if (id < 0 || id >= MAX_TIMERS || !listener) {
        return YETTY_ERR(yetty_ycore_void, "invalid timer id or listener");
    }

    th = &impl->timers[id];
    if (th->listener_count >= MAX_LISTENERS_PER_TIMER) {
        return YETTY_ERR(yetty_ycore_void, "too many timer listeners");
    }

    th->listeners[th->listener_count++] = listener;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result libuv_deregister_timer_listener(
    struct yetty_yevent_event_loop *self, yetty_yevent_timer_id id,
    struct yetty_yevent_event_listener *listener)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    struct yetty_yplatform_timer_handle *th;

    if (id < 0 || id >= MAX_TIMERS || !listener) {
        return YETTY_ERR(yetty_ycore_void, "invalid timer id or listener");
    }
    th = &impl->timers[id];
    /* Linear scan, shift-on-remove. Listener counts per timer are
     * small (≤ MAX_LISTENERS_PER_TIMER) so this is fine. */
    for (int i = 0; i < th->listener_count; i++) {
        if (th->listeners[i] == listener) {
            for (int j = i + 1; j < th->listener_count; j++) {
                th->listeners[j - 1] = th->listeners[j];
            }
            th->listener_count--;
            return YETTY_OK_VOID();
        }
    }
    /* Not registered — quiet no-op (caller may be over-eager on EOS). */
    return YETTY_OK_VOID();
}

static void libuv_request_render(struct yetty_yevent_event_loop *self)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    ydebug("libuv_request_render: setting render_pending=1");
    impl->render_pending = 1;
    uv_async_send(&impl->render_async);
}

/* Thread-safe: append (fn, arg) to the post queue and wake the loop. */
static void libuv_post_to_loop(struct yetty_yevent_event_loop *self, void (*fn)(void *), void *arg)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    struct yetty_yplatform_post_node *node;

    if (!fn) {
        return;
    }

    node = malloc(sizeof(struct yetty_yplatform_post_node));
    if (!node) {
        ywarn("libuv_post_to_loop: malloc failed, dropping callback");
        return;
    }
    node->fn = fn;
    node->arg = arg;
    node->next = NULL;

    uv_mutex_lock(&impl->post_mutex);
    if (impl->post_tail) {
        impl->post_tail->next = node;
    } else {
        impl->post_head = node;
    }
    impl->post_tail = node;
    uv_mutex_unlock(&impl->post_mutex);

    uv_async_send(&impl->post_async);
}

/* Trampoline for post_to_loop: call uv_stop on the loop thread. */
static void uv_stop_trampoline(void *arg)
{
    uv_stop((uv_loop_t *)arg);
}

/* post_fatal_error — stash, mark, stop the loop. Thread-safe (uses
 * post_mutex). MOVES the error chain into the loop; caller must not
 * destroy. Second and subsequent calls free their own chain. */
static void libuv_post_fatal_error(struct yetty_yevent_event_loop *self,
                                   struct yetty_ycore_error error)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);

    uv_mutex_lock(&impl->post_mutex);
    int already_set = impl->fatal_set;
    if (!already_set) {
        impl->fatal_error = error;
        impl->fatal_set = 1;
    }
    uv_mutex_unlock(&impl->post_mutex);

    if (already_set) {
        /* Lost the race — drop the duplicate. */
        yetty_ycore_error_destroy(error);
        return;
    }

    /* uv_stop is documented thread-unsafe; route through post_to_loop so
     * the actual uv_stop call lands on the loop thread. */
    libuv_post_to_loop(self, uv_stop_trampoline, impl->loop);
}

/* TCP Server & Client */

/* Alloc callback for server connections */
static void on_server_conn_alloc(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
    struct yetty_yevent_conn *conn = handle->data;
    struct yetty_yplatform_tcp_server_handle *server = conn->server;
    char *base = NULL;
    size_t len = 0;

    if (server->callbacks.on_alloc) {
        server->callbacks.on_alloc(conn->conn_ctx, suggested, &base, &len);
    }

    buf->base = base;
    buf->len = len;
}

/* Read callback for server connections */
static void on_server_conn_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    struct yetty_yevent_conn *conn = stream->data;
    struct yetty_yplatform_tcp_server_handle *server = conn->server;

    if (nread < 0) {
        ytrace("tcp_server: client disconnected");
        if (server->callbacks.on_disconnect) {
            server->callbacks.on_disconnect(conn->conn_ctx);
        }
        uv_close((uv_handle_t *)stream, NULL);
        conn->active = 0;
        write_pool_drain(conn);
        server->conn_count--;
        return;
    }

    if (nread == 0) {
        return;
    }

    if (server->callbacks.on_data) {
        server->callbacks.on_data(conn->conn_ctx, conn, buf->base, nread);
    }
}

/* New connection callback */
static void on_tcp_server_connection(uv_stream_t *server_stream, int status)
{
    struct yetty_yplatform_tcp_server_handle *server = server_stream->data;
    struct yetty_yevent_conn *conn;
    int i, r;

    if (status < 0) {
        yerror("tcp_server: connection error: %s", uv_strerror(status));
        return;
    }

    /* Find free connection slot */
    conn = NULL;
    for (i = 0; i < MAX_TCP_CLIENTS; i++) {
        if (server->conns[i] == NULL) {
            conn = calloc(1, sizeof(struct yetty_yevent_conn));
            if (!conn) {
                yerror("tcp_server: out of memory");
                return;
            }
            server->conns[i] = conn;
            break;
        } else if (!server->conns[i]->active) {
            conn = server->conns[i];
            break;
        }
    }

    if (!conn) {
        yerror("tcp_server: too many clients");
        uv_tcp_t reject;
        uv_tcp_init(server->loop_impl->loop, &reject);
        uv_accept(server_stream, (uv_stream_t *)&reject);
        uv_close((uv_handle_t *)&reject, NULL);
        return;
    }

    uv_tcp_init(server->loop_impl->loop, &conn->tcp);
    conn->tcp.data = conn;
    conn->server = server;
    conn->loop_impl = server->loop_impl;

    r = uv_accept(server_stream, (uv_stream_t *)&conn->tcp);
    if (r != 0) {
        yerror("tcp_server: accept failed: %s", uv_strerror(r));
        uv_close((uv_handle_t *)&conn->tcp, NULL);
        return;
    }

    conn->active = 1;
    server->conn_count++;

    /* Call on_connect to get connection context */
    if (server->callbacks.on_connect) {
        conn->conn_ctx = server->callbacks.on_connect(server->callbacks.ctx, conn);
    }

    ytrace("tcp_server: client connected (total: %d)", server->conn_count);

    r = uv_read_start((uv_stream_t *)&conn->tcp, on_server_conn_alloc, on_server_conn_read);
    if (r != 0) {
        yerror("tcp_server: read_start failed: %s", uv_strerror(r));
        if (server->callbacks.on_disconnect) {
            server->callbacks.on_disconnect(conn->conn_ctx);
        }
        uv_close((uv_handle_t *)&conn->tcp, NULL);
        conn->active = 0;
        server->conn_count--;
    }
}

static struct yetty_yevent_tcp_server_id_result libuv_create_tcp_server(
    struct yetty_yevent_event_loop *self, const char *host, int port,
    const struct yetty_yevent_tcp_server_callbacks *callbacks)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    int id = impl->next_tcp_server_id++;
    struct yetty_yplatform_tcp_server_handle *server;

    if (id >= MAX_TCP_SERVERS) {
        return YETTY_ERR(yetty_yevent_tcp_server_id, "too many tcp servers");
    }

    if (!callbacks) {
        return YETTY_ERR(yetty_yevent_tcp_server_id, "callbacks required");
    }

    server = &impl->tcp_servers[id];
    memset(server, 0, sizeof(*server));
    server->id = id;
    server->loop_impl = impl;
    server->callbacks = *callbacks;
    if (host) {
        strncpy(server->host, host, sizeof(server->host) - 1);
    }
    server->port = port;

    ytrace("tcp_server: created server id=%d host=%s port=%d", id, host, port);
    return YETTY_OK(yetty_yevent_tcp_server_id, id);
}

static struct yetty_ycore_void_result libuv_start_tcp_server(struct yetty_yevent_event_loop *self,
                                                             yetty_yevent_tcp_server_id id)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    struct yetty_yplatform_tcp_server_handle *server;
    struct sockaddr_in addr;
    int r;

    if (id < 0 || id >= MAX_TCP_SERVERS) {
        return YETTY_ERR(yetty_ycore_void, "invalid server id");
    }

    server = &impl->tcp_servers[id];

    if (server->active) {
        return YETTY_ERR(yetty_ycore_void, "server already running");
    }

    r = uv_tcp_init(impl->loop, &server->tcp);
    if (r != 0) {
        return YETTY_ERR(yetty_ycore_void, "uv_tcp_init failed");
    }

    server->tcp.data = server;

    r = uv_ip4_addr(server->host, server->port, &addr);
    if (r != 0) {
        yerror("tcp_server: invalid address %s:%d", server->host, server->port);
        return YETTY_ERR(yetty_ycore_void, "uv_ip4_addr failed");
    }

    r = uv_tcp_bind(&server->tcp, (const struct sockaddr *)&addr, 0);
    if (r != 0) {
        yerror("tcp_server: bind failed: %s", uv_strerror(r));
        return YETTY_ERR(yetty_ycore_void, "uv_tcp_bind failed");
    }

    r = uv_listen((uv_stream_t *)&server->tcp, 128, on_tcp_server_connection);
    if (r != 0) {
        yerror("tcp_server: listen failed: %s", uv_strerror(r));
        return YETTY_ERR(yetty_ycore_void, "uv_listen failed");
    }

    server->active = 1;
    ytrace("tcp_server: listening on %s:%d", server->host, server->port);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result libuv_stop_tcp_server(struct yetty_yevent_event_loop *self,
                                                            yetty_yevent_tcp_server_id id)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    struct yetty_yplatform_tcp_server_handle *server;

    if (id < 0 || id >= MAX_TCP_SERVERS) {
        return YETTY_ERR(yetty_ycore_void, "invalid server id");
    }

    server = &impl->tcp_servers[id];

    if (!server->active) {
        return YETTY_OK_VOID();
    }

    /* Close all connections */
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        if (server->conns[i] && server->conns[i]->active) {
            if (server->callbacks.on_disconnect) {
                server->callbacks.on_disconnect(server->conns[i]->conn_ctx);
            }
            uv_close((uv_handle_t *)&server->conns[i]->tcp, NULL);
            server->conns[i]->active = 0;
            write_pool_drain(server->conns[i]);
        }
    }
    server->conn_count = 0;

    uv_close((uv_handle_t *)&server->tcp, NULL);
    server->active = 0;

    ytrace("tcp_server: stopped server id=%d", id);
    return YETTY_OK_VOID();
}

/* TCP Client (outbound connection) */

static void on_client_alloc(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
    struct yetty_yevent_conn *conn = handle->data;
    struct yetty_yplatform_tcp_client_handle *client = conn->client;
    char *base = NULL;
    size_t len = 0;

    if (client->callbacks.on_alloc) {
        client->callbacks.on_alloc(client->callbacks.ctx, suggested, &base, &len);
    }

    buf->base = base;
    buf->len = len;
}

static void on_client_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    struct yetty_yevent_conn *conn = stream->data;
    struct yetty_yplatform_tcp_client_handle *client = conn->client;

    if (nread < 0) {
        ytrace("tcp_client: disconnected");
        if (client->callbacks.on_disconnect) {
            client->callbacks.on_disconnect(client->callbacks.ctx);
        }
        uv_close((uv_handle_t *)stream, NULL);
        conn->active = 0;
        write_pool_drain(conn);
        client->active = 0;
        return;
    }

    if (nread == 0) {
        return;
    }

    if (client->callbacks.on_data) {
        client->callbacks.on_data(client->callbacks.ctx, conn, buf->base, nread);
    }
}

static void on_client_connect(uv_connect_t *req, int status)
{
    struct yetty_yplatform_tcp_client_handle *client = req->data;
    struct yetty_yevent_conn *conn = &client->conn;

    if (status < 0) {
        yerror("tcp_client: connect failed: %s", uv_strerror(status));
        if (client->callbacks.on_connect_error) {
            client->callbacks.on_connect_error(client->callbacks.ctx, uv_strerror(status));
        }
        uv_close((uv_handle_t *)&conn->tcp, NULL);
        client->active = 0;
        return;
    }

    conn->active = 1;
    client->active = 1;

    ytrace("tcp_client: connected to %s:%d", client->host, client->port);

    if (client->callbacks.on_connect) {
        client->callbacks.on_connect(client->callbacks.ctx, conn);
    }

    int r = uv_read_start((uv_stream_t *)&conn->tcp, on_client_alloc, on_client_read);
    if (r != 0) {
        yerror("tcp_client: read_start failed: %s", uv_strerror(r));
        if (client->callbacks.on_disconnect) {
            client->callbacks.on_disconnect(client->callbacks.ctx);
        }
        uv_close((uv_handle_t *)&conn->tcp, NULL);
        conn->active = 0;
        client->active = 0;
    }
}

static void on_client_resolved(uv_getaddrinfo_t *req, int status, struct addrinfo *res)
{
    struct yetty_yplatform_tcp_client_handle *client = req->data;

    if (status < 0) {
        yerror("tcp_client: DNS resolution failed for %s: %s", client->host, uv_strerror(status));
        if (client->callbacks.on_connect_error) {
            client->callbacks.on_connect_error(client->callbacks.ctx, uv_strerror(status));
        }
        uv_freeaddrinfo(res);
        return;
    }

    /* Patch in the port — uv_getaddrinfo returns port 0. */
    if (res->ai_family == AF_INET) {
        ((struct sockaddr_in *)res->ai_addr)->sin_port = htons((uint16_t)client->port);
    } else if (res->ai_family == AF_INET6) {
        ((struct sockaddr_in6 *)res->ai_addr)->sin6_port = htons((uint16_t)client->port);
    }

    client->connect_req.data = client;
    int r = uv_tcp_connect(&client->connect_req, &client->conn.tcp, res->ai_addr,
                           on_client_connect);
    uv_freeaddrinfo(res);

    if (r != 0) {
        yerror("tcp_client: uv_tcp_connect failed: %s", uv_strerror(r));
        if (client->callbacks.on_connect_error) {
            client->callbacks.on_connect_error(client->callbacks.ctx, uv_strerror(r));
        }
        uv_close((uv_handle_t *)&client->conn.tcp, NULL);
    }
}

static struct yetty_yevent_tcp_client_id_result libuv_create_tcp_client(
    struct yetty_yevent_event_loop *self, const char *host, int port,
    const struct yetty_yevent_tcp_client_callbacks *callbacks)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    int id = impl->next_tcp_client_id++;
    struct yetty_yplatform_tcp_client_handle *client;
    int r;

    if (id >= MAX_TCP_CLIENTS) {
        return YETTY_ERR(yetty_yevent_tcp_client_id, "too many tcp clients");
    }
    if (!callbacks) {
        return YETTY_ERR(yetty_yevent_tcp_client_id, "callbacks required");
    }

    client = &impl->tcp_clients[id];
    memset(client, 0, sizeof(*client));
    client->id = id;
    client->loop_impl = impl;
    client->callbacks = *callbacks;
    if (host) {
        strncpy(client->host, host, sizeof(client->host) - 1);
    }
    client->port = port;

    client->conn.loop_impl = impl;
    client->conn.client = client;
    client->conn.tcp.data = &client->conn;

    r = uv_tcp_init(impl->loop, &client->conn.tcp);
    if (r != 0) {
        return YETTY_ERR(yetty_yevent_tcp_client_id, "uv_tcp_init failed");
    }

    /* Async DNS resolution — works for hostnames, IPs, and "localhost". */
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    client->resolve_req.data = client;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    r = uv_getaddrinfo(impl->loop, &client->resolve_req, on_client_resolved, host, port_str,
                       &hints);
    if (r != 0) {
        uv_close((uv_handle_t *)&client->conn.tcp, NULL);
        return YETTY_ERR(yetty_yevent_tcp_client_id, "uv_getaddrinfo failed");
    }

    ytrace("tcp_client: resolving %s:%d", host, port);
    return YETTY_OK(yetty_yevent_tcp_client_id, id);
}

static struct yetty_ycore_void_result libuv_stop_tcp_client(struct yetty_yevent_event_loop *self,
                                                            yetty_yevent_tcp_client_id id)
{
    struct yetty_yplatform_libuv_event_loop *impl =
        container_of(self, struct yetty_yplatform_libuv_event_loop, base);
    struct yetty_yplatform_tcp_client_handle *client;

    if (id < 0 || id >= MAX_TCP_CLIENTS) {
        return YETTY_ERR(yetty_ycore_void, "invalid client id");
    }

    client = &impl->tcp_clients[id];

    if (!client->active) {
        return YETTY_OK_VOID();
    }

    if (client->callbacks.on_disconnect) {
        client->callbacks.on_disconnect(client->callbacks.ctx);
    }

    uv_close((uv_handle_t *)&client->conn.tcp, NULL);
    client->conn.active = 0;
    write_pool_drain(&client->conn);
    client->active = 0;

    ytrace("tcp_client: stopped client id=%d", id);
    return YETTY_OK_VOID();
}

/* TCP send/close (works for both server and client connections) */

/* uv_write is asynchronous and holds the data pointer until the write
 * completes. We own the buffer for that whole window — caller can pass stack
 * locals, static scratch, or anything else and never has to think about
 * lifetime. The bug this fixes: callers were passing stack/static buffers
 * that got reused or went out of scope while writes were still queued; on
 * loopback writes complete fast enough that nothing notices, but over real
 * networks the wire gets garbage.
 *
 * Pool-backed: pooled blocks carry exactly POOL_BUF_SIZE of trailing data;
 * one-shot blocks carry exactly the payload size. `conn` is stashed in the
 * request so the completion callback can push back to the freelist. */
struct libuv_write_req {
    uv_write_t req;
    struct yetty_yevent_conn *conn;
    struct libuv_write_req *next; /* freelist link, valid only when pooled */
    int pooled;                   /* 1 = return to pool on completion, 0 = free */
    char data[];
};

static struct libuv_write_req *write_req_acquire(struct yetty_yevent_conn *conn, size_t len)
{
    if (len <= LIBUV_WRITE_POOL_BUF_SIZE && conn->write_pool_head) {
        struct libuv_write_req *wreq = conn->write_pool_head;
        conn->write_pool_head = wreq->next;
        conn->write_pool_depth--;
        wreq->next = NULL;
        wreq->pooled = 1;
        return wreq;
    }
    int pooled = len <= LIBUV_WRITE_POOL_BUF_SIZE;
    size_t data_size = pooled ? LIBUV_WRITE_POOL_BUF_SIZE : len;
    struct libuv_write_req *wreq = malloc(sizeof(*wreq) + data_size);
    if (!wreq) {
        return NULL;
    }
    wreq->conn = conn;
    wreq->next = NULL;
    wreq->pooled = pooled;
    return wreq;
}

static void write_req_release(struct libuv_write_req *wreq)
{
    struct yetty_yevent_conn *conn = wreq->conn;
    if (conn && conn->active && wreq->pooled &&
        conn->write_pool_depth < LIBUV_WRITE_POOL_MAX_DEPTH) {
        wreq->next = conn->write_pool_head;
        conn->write_pool_head = wreq;
        conn->write_pool_depth++;
        return;
    }
    free(wreq);
}

/* Drain the freelist when a connection goes away. Called from tcp_close
 * paths; safe to call repeatedly. */
static void write_pool_drain(struct yetty_yevent_conn *conn)
{
    if (!conn) {
        return;
    }
    struct libuv_write_req *cur = conn->write_pool_head;
    while (cur) {
        struct libuv_write_req *next = cur->next;
        free(cur);
        cur = next;
    }
    conn->write_pool_head = NULL;
    conn->write_pool_depth = 0;
}

static void on_tcp_write_done(uv_write_t *req, int status)
{
    (void)status;
    struct libuv_write_req *wreq = (struct libuv_write_req *)req;
    write_req_release(wreq);
}

static struct yetty_ycore_size_result libuv_tcp_send(struct yetty_yevent_conn *conn,
                                                     const void *data, size_t len)
{
    if (!conn || !conn->active) {
        return YETTY_ERR(yetty_ycore_size, "invalid connection");
    }

    struct libuv_write_req *wreq = write_req_acquire(conn, len);
    if (!wreq) {
        return YETTY_ERR(yetty_ycore_size, "out of memory");
    }
    memcpy(wreq->data, data, len);

    uv_buf_t buf = uv_buf_init(wreq->data, len);
    if (uv_write(&wreq->req, (uv_stream_t *)&conn->tcp, &buf, 1, on_tcp_write_done) != 0) {
        /* Outright failure — don't pool a never-submitted req, it'd skip the
         * completion path. */
        free(wreq);
        return YETTY_ERR(yetty_ycore_size, "uv_write failed");
    }

    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_void_result libuv_tcp_close(struct yetty_yevent_conn *conn)
{
    if (!conn) {
        return YETTY_ERR(yetty_ycore_void, "invalid connection");
    }

    if (conn->active) {
        /* Call disconnect callback */
        if (conn->server && conn->server->callbacks.on_disconnect) {
            conn->server->callbacks.on_disconnect(conn->conn_ctx);
        } else if (conn->client && conn->client->callbacks.on_disconnect) {
            conn->client->callbacks.on_disconnect(conn->client->callbacks.ctx);
        }

        uv_close((uv_handle_t *)&conn->tcp, NULL);
        conn->active = 0;
        write_pool_drain(conn);

        if (conn->server) {
            conn->server->conn_count--;
        }
    }

    return YETTY_OK_VOID();
}

struct yetty_ycore_event_loop_result yetty_ycore_event_loop_create(
    struct yetty_ycore_xthread_event_pipe *pipe)
{
    struct yetty_yplatform_libuv_event_loop *impl;

    impl = malloc(sizeof(struct yetty_yplatform_libuv_event_loop));
    if (!impl) {
        return YETTY_ERR(yetty_ycore_event_loop, "failed to allocate event loop");
    }

    memset(impl, 0, sizeof(*impl));
    impl->base.ops = &libuv_ops;
    impl->loop = uv_default_loop();
    impl->platform_input_pipe = pipe;

    /* Render async */
    impl->render_async.data = impl;
    uv_async_init(impl->loop, &impl->render_async, on_render_async);

    /* Post-to-loop machinery (used by the GPU poll thread to resume coros). */
    impl->post_async.data = impl;
    uv_async_init(impl->loop, &impl->post_async, on_post_async);
    uv_mutex_init(&impl->post_mutex);

#ifndef _WIN32
    /* Signal handlers (Unix only) */
    impl->sigint_handle.data = impl;
    impl->sigterm_handle.data = impl;
    uv_signal_init(impl->loop, &impl->sigint_handle);
    uv_signal_init(impl->loop, &impl->sigterm_handle);
    uv_signal_start(&impl->sigint_handle, on_signal, SIGINT);
    uv_signal_start(&impl->sigterm_handle, on_signal, SIGTERM);
#endif

    /* Input pipe via uv_pipe_t */
    if (pipe) {
        struct yetty_ycore_int_result fd_res = pipe->ops->read_fd(pipe);
        if (YETTY_IS_OK(fd_res) && fd_res.value >= 0) {
            uv_pipe_init(impl->loop, &impl->input_pipe, 0);
            impl->input_pipe.data = impl;
            uv_pipe_open(&impl->input_pipe, fd_res.value);
            uv_read_start((uv_stream_t *)&impl->input_pipe, on_input_pipe_alloc,
                          on_input_pipe_read);
            impl->input_pipe_active = 1;
        }
    }

    return YETTY_OK(yetty_ycore_event_loop, &impl->base);
}
