/* RPC server implementation using event loop TCP server */

#include <yetty/yctl/rpc-server.h>
#include <yetty/ycore/result.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yevent/event.h>
#include <yetty/ytrace/ytrace.h>

#include <msgpack.h>
#include <stdlib.h>
#include <string.h>

/* Maximum number of handlers */
#define MAX_HANDLERS 64

/* Read buffer size */
#define READ_BUFFER_SIZE 65536

/* Response buffer size */
#define RESPONSE_BUFFER_SIZE 4096

/* Handler entry */
struct yetty_yctl_handler_entry {
    uint32_t channel;
    char method[64];
    yetty_rpc_handler_fn handler;
    void *userdata;
};

/* Per-connection context.
 *
 * read_buf is a streaming accumulator: each libuv read appends to
 * read_buf[buffered..]; the parse loop in rpc_on_data consumes whole
 * messages from the front and memmoves the trailing partial (if any)
 * back to offset 0. This is the streaming-correct model — the previous
 * "parse one message per read" code dropped extra messages whenever
 * the loop stalled (e.g. under --record's GPU encode load) and the
 * kernel coalesced multiple framed RPCs into one read. */
struct yetty_yctl_rpc_conn_ctx {
    struct yetty_yctl_server *server;
    char read_buf[READ_BUFFER_SIZE];
    size_t buffered; /* bytes carried over from previous read(s) */
    uint8_t response_buf[RESPONSE_BUFFER_SIZE];
};

/* RPC server */
struct yetty_yctl_server {
    struct yetty_yevent_event_loop *event_loop;
    yetty_yevent_tcp_server_id server_id;
    int port;
    int running;

    /* Handlers */
    struct yetty_yctl_handler_entry handlers[MAX_HANDLERS];
    size_t handler_count;

    /* Client count */
    size_t client_count;
};

static struct yetty_yctl_handler_entry *find_handler(struct yetty_yctl_server *server,
                                                     uint32_t channel, const char *method,
                                                     size_t method_len)
{
    for (size_t i = 0; i < server->handler_count; i++) {
        struct yetty_yctl_handler_entry *e = &server->handlers[i];
        if (e->channel == channel && strncmp(e->method, method, method_len) == 0 &&
            e->method[method_len] == '\0') {
            return e;
        }
    }
    return NULL;
}

/* Dispatch a single already-parsed message: find handler, invoke it,
 * write any response back. Owns msg.params (frees it on return). */
static void dispatch_message(struct yetty_yctl_rpc_conn_ctx *ctx, struct yetty_yevent_conn *conn,
                             struct yetty_yctl_message msg)
{
    struct yetty_yctl_server *server = ctx->server;
    struct yetty_yctl_handler_entry *handler;
    struct yetty_rpc_handler_result result;
    struct yetty_yctl_write_buffer wbuf;

    ytrace("yctl: received %s: channel=%u method=%.*s msgid=%u",
           msg.type == YETTY_YCTL_MSG_REQUEST        ? "request"
           : msg.type == YETTY_YCTL_MSG_NOTIFICATION ? "notification"
                                                     : "response",
           msg.channel, (int)msg.method_len, msg.method, msg.msgid);

    handler = find_handler(server, msg.channel, msg.method, msg.method_len);
    if (!handler) {
        ytrace("yctl: no handler for channel=%u method=%.*s", msg.channel, (int)msg.method_len,
               msg.method);

        if (msg.type == YETTY_YCTL_MSG_REQUEST) {
            yetty_yctl_write_buffer_init(&wbuf, ctx->response_buf, RESPONSE_BUFFER_SIZE);
            yetty_yctl_write_response_error(&wbuf, msg.msgid, "method not found");
            server->event_loop->ops->tcp_send(conn, wbuf.data, wbuf.len);
        }
        free((void *)msg.params);
        return;
    }

    result = handler->handler(&msg, handler->userdata);

    if (msg.type == YETTY_YCTL_MSG_REQUEST) {
        yetty_yctl_write_buffer_init(&wbuf, ctx->response_buf, RESPONSE_BUFFER_SIZE);

        if (result.ok) {
            if (result.value.data) {
                yetty_yctl_write_response_ok(&wbuf, msg.msgid, result.value.data, result.value.len);
            } else {
                yetty_yctl_write_response_bool(&wbuf, msg.msgid, result.value.bool_value);
            }
        } else {
            yetty_yctl_write_response_error(&wbuf, msg.msgid, result.error);
        }

        server->event_loop->ops->tcp_send(conn, wbuf.data, wbuf.len);
    }

    free((void *)msg.params);
}

/* TCP server callbacks */

static void *rpc_on_connect(void *ctx, struct yetty_yevent_conn *conn)
{
    struct yetty_yctl_server *server = ctx;
    struct yetty_yctl_rpc_conn_ctx *conn_ctx;

    (void)conn;

    conn_ctx = calloc(1, sizeof(struct yetty_yctl_rpc_conn_ctx));
    if (!conn_ctx) {
        ytrace("yctl: failed to allocate connection context");
        return NULL;
    }

    conn_ctx->server = server;
    server->client_count++;

    ytrace("yctl: client connected (total: %zu)", server->client_count);

    return conn_ctx;
}

static void rpc_on_alloc(void *conn_ctx_ptr, size_t suggested, char **buf, size_t *len)
{
    struct yetty_yctl_rpc_conn_ctx *ctx = conn_ctx_ptr;

    (void)suggested;

    /* Hand libuv the slot just past the carried-over partial. The new
     * read fills in starting at &read_buf[buffered]; the parse loop
     * then sees ctx->read_buf[0..buffered+nread]. */
    if (ctx && ctx->buffered < READ_BUFFER_SIZE) {
        *buf = ctx->read_buf + ctx->buffered;
        *len = READ_BUFFER_SIZE - ctx->buffered;
    } else {
        *buf = NULL;
        *len = 0;
    }
}

YETTY_EXTERNAL_CALLBACK
static void rpc_on_data(void *conn_ctx_ptr, struct yetty_yevent_conn *conn, const char *data,
                        long nread)
{
    struct yetty_yctl_rpc_conn_ctx *ctx = conn_ctx_ptr;
    size_t total;
    size_t off;

    /* libuv read into ctx->read_buf + ctx->buffered (see rpc_on_alloc),
     * so `data` is just an alias into our own buffer. We work directly
     * off ctx->read_buf to keep the leftover memmove simple. */
    (void)data;

    if (!ctx || nread <= 0) {
        return;
    }

    total = ctx->buffered + (size_t)nread;
    off = 0;

    /* Drain whole messages from the front of the buffer. The kernel
     * cheerfully coalesces N small RPCs into a single TCP read whenever
     * we stall the loop (recording, heavy GPU work) — drop one and we
     * lose typed characters. */
    while (off < total) {
        size_t consumed = 0;
        struct yetty_rpc_message_result pres =
            yetty_yctl_message_parse((const uint8_t *)ctx->read_buf + off, total - off, &consumed);

        if (YETTY_IS_ERR(pres)) {
            ytrace("yctl: parse error, dropping connection buffer: %s", pres.error.msg);
            ctx->buffered = 0;
            return;
        }
        if (consumed == 0) {
            /* Partial trailing message — wait for next read. */
            break;
        }

        dispatch_message(ctx, conn, pres.value);
        off += consumed;
    }

    /* Carry the unparsed tail to the front so the next read appends to it. */
    if (off < total) {
        size_t leftover = total - off;
        memmove(ctx->read_buf, ctx->read_buf + off, leftover);
        ctx->buffered = leftover;
    } else {
        ctx->buffered = 0;
    }
}

static void rpc_on_disconnect(void *conn_ctx_ptr)
{
    struct yetty_yctl_rpc_conn_ctx *ctx = conn_ctx_ptr;

    if (!ctx) {
        return;
    }

    if (ctx->server && ctx->server->client_count > 0) {
        ctx->server->client_count--;
    }

    ytrace("yctl: client disconnected (remaining: %zu)",
           ctx->server ? ctx->server->client_count : 0);

    free(ctx);
}

static struct yetty_ycore_void_result register_builtin_handlers(struct yetty_yctl_server *server);

struct yetty_rpc_server_ptr_result yetty_yctl_server_create(
    struct yetty_yevent_event_loop *event_loop)
{
    struct yetty_yctl_server *server;
    struct yetty_ycore_void_result res;

    if (!event_loop) {
        return YETTY_ERR(yetty_rpc_server_ptr, "event_loop is NULL");
    }

    server = calloc(1, sizeof(struct yetty_yctl_server));
    if (!server) {
        return YETTY_ERR(yetty_rpc_server_ptr, "out of memory");
    }

    server->event_loop = event_loop;
    server->server_id = -1;

    res = register_builtin_handlers(server);
    if (YETTY_IS_ERR(res)) {
        free(server);
        return YETTY_ERR(yetty_rpc_server_ptr, res.error.msg);
    }

    return YETTY_OK(yetty_rpc_server_ptr, server);
}

struct yetty_ycore_void_result yetty_yctl_server_destroy(struct yetty_yctl_server *server)
{
    if (!server) {
        return YETTY_ERR(yetty_ycore_void, "yetty_rpc_server_destroy: NULL server");
    }

    struct yetty_ycore_void_result stop_r = yetty_yctl_server_stop(server);
    free(server);

    if (YETTY_IS_ERR(stop_r)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_rpc_server_destroy: stop failed", stop_r);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yctl_server_start(struct yetty_yctl_server *server,
                                                       const char *host, int port)
{
    struct yetty_yevent_tcp_server_id_result id_res;
    struct yetty_ycore_void_result res;
    struct yetty_yevent_tcp_server_callbacks callbacks;

    if (!server) {
        return YETTY_ERR(yetty_ycore_void, "server is NULL");
    }

    if (server->running) {
        return YETTY_ERR(yetty_ycore_void, "server already running");
    }

    /* Set up callbacks */
    callbacks.ctx = server;
    callbacks.on_connect = rpc_on_connect;
    callbacks.on_alloc = rpc_on_alloc;
    callbacks.on_data = rpc_on_data;
    callbacks.on_disconnect = rpc_on_disconnect;

    /* Create TCP server with callbacks */
    id_res = server->event_loop->ops->create_tcp_server(server->event_loop, host, port, &callbacks);
    if (YETTY_IS_ERR(id_res)) {
        return YETTY_ERR(yetty_ycore_void, id_res.error.msg);
    }

    server->server_id = id_res.value;

    /* Start listening */
    res = server->event_loop->ops->start_tcp_server(server->event_loop, server->server_id);
    if (YETTY_IS_ERR(res)) {
        server->event_loop->ops->stop_tcp_server(server->event_loop, server->server_id);
        server->server_id = -1;
        return res;
    }

    server->port = port;
    server->running = 1;

    ytrace("yctl: server listening on %s:%d", host, port);

    return YETTY_OK_VOID();
}

int yetty_yctl_server_get_port(const struct yetty_yctl_server *server)
{
    if (!server || !server->running) {
        return 0;
    }
    return server->port;
}

struct yetty_ycore_void_result yetty_yctl_server_stop(struct yetty_yctl_server *server)
{
    if (!server) {
        return YETTY_OK_VOID();
    }

    if (server->server_id >= 0) {
        server->event_loop->ops->stop_tcp_server(server->event_loop, server->server_id);
        server->server_id = -1;
    }

    server->running = 0;
    server->client_count = 0;

    ytrace("yctl: server stopped");

    return YETTY_OK_VOID();
}

int yetty_yctl_server_is_running(const struct yetty_yctl_server *server)
{
    return server && server->running;
}

struct yetty_ycore_void_result yetty_yctl_server_register_handler(struct yetty_yctl_server *server,
                                                                  uint32_t channel,
                                                                  const char *method,
                                                                  yetty_rpc_handler_fn handler,
                                                                  void *userdata)
{
    struct yetty_yctl_handler_entry *entry;

    if (!server) {
        return YETTY_ERR(yetty_ycore_void, "server is NULL");
    }

    if (!method || !handler) {
        return YETTY_ERR(yetty_ycore_void, "invalid arguments");
    }

    if (server->handler_count >= MAX_HANDLERS) {
        return YETTY_ERR(yetty_ycore_void, "too many handlers");
    }

    /* Check for duplicate */
    if (find_handler(server, channel, method, strlen(method))) {
        return YETTY_ERR(yetty_ycore_void, "handler already registered");
    }

    entry = &server->handlers[server->handler_count++];
    entry->channel = channel;
    strncpy(entry->method, method, sizeof(entry->method) - 1);
    entry->handler = handler;
    entry->userdata = userdata;

    ytrace("yctl: registered handler: channel=%u method=%s", channel, method);

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yctl_server_unregister_handler(
    struct yetty_yctl_server *server, uint32_t channel, const char *method)
{
    struct yetty_yctl_handler_entry *entry;

    if (!server) {
        return YETTY_ERR(yetty_ycore_void, "server is NULL");
    }

    entry = find_handler(server, channel, method, strlen(method));
    if (!entry) {
        return YETTY_ERR(yetty_ycore_void, "handler not found");
    }

    /* Move last handler to this slot */
    size_t index = entry - server->handlers;
    if (index < server->handler_count - 1) {
        server->handlers[index] = server->handlers[server->handler_count - 1];
    }
    server->handler_count--;

    return YETTY_OK_VOID();
}

size_t yetty_yctl_server_client_count(const struct yetty_yctl_server *server)
{
    return server ? server->client_count : 0;
}

/*
 * Helper to get int from msgpack map by key
 */
static int get_map_int(const msgpack_object *map, const char *key, int def)
{
    if (map->type != MSGPACK_OBJECT_MAP) {
        return def;
    }
    size_t key_len = strlen(key);
    for (uint32_t i = 0; i < map->via.map.size; i++) {
        msgpack_object_kv *kv = &map->via.map.ptr[i];
        if (kv->key.type == MSGPACK_OBJECT_STR && kv->key.via.str.size == key_len &&
            memcmp(kv->key.via.str.ptr, key, key_len) == 0) {
            if (kv->val.type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
                return (int)kv->val.via.u64;
            }
            if (kv->val.type == MSGPACK_OBJECT_NEGATIVE_INTEGER) {
                return (int)kv->val.via.i64;
            }
        }
    }
    return def;
}

static float get_map_float(const msgpack_object *map, const char *key, float def)
{
    if (map->type != MSGPACK_OBJECT_MAP) {
        return def;
    }
    size_t key_len = strlen(key);
    for (uint32_t i = 0; i < map->via.map.size; i++) {
        msgpack_object_kv *kv = &map->via.map.ptr[i];
        if (kv->key.type == MSGPACK_OBJECT_STR && kv->key.via.str.size == key_len &&
            memcmp(kv->key.via.str.ptr, key, key_len) == 0) {
            if (kv->val.type == MSGPACK_OBJECT_FLOAT64) {
                return (float)kv->val.via.f64;
            }
            if (kv->val.type == MSGPACK_OBJECT_FLOAT32) {
                return kv->val.via.f64;
            }
            if (kv->val.type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
                return (float)kv->val.via.u64;
            }
            if (kv->val.type == MSGPACK_OBJECT_NEGATIVE_INTEGER) {
                return (float)kv->val.via.i64;
            }
        }
    }
    return def;
}

/*
 * Built-in EventLoop channel handlers
 */

static struct yetty_rpc_handler_result handle_key_down(const struct yetty_yctl_message *msg,
                                                       void *userdata)
{
    struct yetty_yctl_server *server = userdata;
    struct yetty_yui_event event = {0};
    msgpack_unpacked unpacked;
    msgpack_object *params;

    if (!server->event_loop) {
        return YETTY_YCTL_HANDLER_ERR("no event loop");
    }
    if (!msg->params || msg->params_len == 0) {
        return YETTY_YCTL_HANDLER_ERR("missing params");
    }

    msgpack_unpacked_init(&unpacked);
    if (msgpack_unpack_next(&unpacked, (const char *)msg->params, msg->params_len, NULL) !=
        MSGPACK_UNPACK_SUCCESS) {
        msgpack_unpacked_destroy(&unpacked);
        return YETTY_YCTL_HANDLER_ERR("invalid params");
    }
    params = &unpacked.data;

    event.type = YETTY_YCORE_KEY_DOWN;
    event.key.key = get_map_int(params, "key", 0);
    event.key.mods = get_map_int(params, "mods", 0);
    event.key.scancode = get_map_int(params, "scancode", 0);

    msgpack_unpacked_destroy(&unpacked);
    server->event_loop->ops->dispatch(server->event_loop, &event);
    return YETTY_YCTL_HANDLER_OK_BOOL(1);
}

static struct yetty_rpc_handler_result handle_key_up(const struct yetty_yctl_message *msg,
                                                     void *userdata)
{
    struct yetty_yctl_server *server = userdata;
    struct yetty_yui_event event = {0};
    msgpack_unpacked unpacked;
    msgpack_object *params;

    if (!server->event_loop) {
        return YETTY_YCTL_HANDLER_ERR("no event loop");
    }
    if (!msg->params || msg->params_len == 0) {
        return YETTY_YCTL_HANDLER_ERR("missing params");
    }

    msgpack_unpacked_init(&unpacked);
    if (msgpack_unpack_next(&unpacked, (const char *)msg->params, msg->params_len, NULL) !=
        MSGPACK_UNPACK_SUCCESS) {
        msgpack_unpacked_destroy(&unpacked);
        return YETTY_YCTL_HANDLER_ERR("invalid params");
    }
    params = &unpacked.data;

    event.type = YETTY_YCORE_KEY_UP;
    event.key.key = get_map_int(params, "key", 0);
    event.key.mods = get_map_int(params, "mods", 0);
    event.key.scancode = get_map_int(params, "scancode", 0);

    msgpack_unpacked_destroy(&unpacked);
    server->event_loop->ops->dispatch(server->event_loop, &event);
    return YETTY_YCTL_HANDLER_OK_BOOL(1);
}

static struct yetty_rpc_handler_result handle_char(const struct yetty_yctl_message *msg,
                                                   void *userdata)
{
    struct yetty_yctl_server *server = userdata;
    struct yetty_yui_event event = {0};
    msgpack_unpacked unpacked;
    msgpack_object *params;

    if (!server->event_loop) {
        return YETTY_YCTL_HANDLER_ERR("no event loop");
    }
    if (!msg->params || msg->params_len == 0) {
        return YETTY_YCTL_HANDLER_ERR("missing params");
    }

    msgpack_unpacked_init(&unpacked);
    if (msgpack_unpack_next(&unpacked, (const char *)msg->params, msg->params_len, NULL) !=
        MSGPACK_UNPACK_SUCCESS) {
        msgpack_unpacked_destroy(&unpacked);
        return YETTY_YCTL_HANDLER_ERR("invalid params");
    }
    params = &unpacked.data;

    event.type = YETTY_YCORE_CHAR;
    event.chr.codepoint = (uint32_t)get_map_int(params, "codepoint", 0);
    event.chr.mods = get_map_int(params, "mods", 0);

    ytrace("yctl: handle_char: codepoint=%u ('%c') mods=%d", event.chr.codepoint,
           event.chr.codepoint >= 32 && event.chr.codepoint < 127 ? (char)event.chr.codepoint : '?',
           event.chr.mods);

    msgpack_unpacked_destroy(&unpacked);
    server->event_loop->ops->dispatch(server->event_loop, &event);
    return YETTY_YCTL_HANDLER_OK_BOOL(1);
}

static struct yetty_rpc_handler_result handle_mouse_down(const struct yetty_yctl_message *msg,
                                                         void *userdata)
{
    struct yetty_yctl_server *server = userdata;
    struct yetty_yui_event event = {0};
    msgpack_unpacked unpacked;
    msgpack_object *params;

    if (!server->event_loop) {
        return YETTY_YCTL_HANDLER_ERR("no event loop");
    }
    if (!msg->params || msg->params_len == 0) {
        return YETTY_YCTL_HANDLER_ERR("missing params");
    }

    msgpack_unpacked_init(&unpacked);
    if (msgpack_unpack_next(&unpacked, (const char *)msg->params, msg->params_len, NULL) !=
        MSGPACK_UNPACK_SUCCESS) {
        msgpack_unpacked_destroy(&unpacked);
        return YETTY_YCTL_HANDLER_ERR("invalid params");
    }
    params = &unpacked.data;

    event.type = YETTY_YCORE_MOUSE_DOWN;
    event.mouse.x = get_map_float(params, "x", 0);
    event.mouse.y = get_map_float(params, "y", 0);
    event.mouse.button = get_map_int(params, "button", 0);

    msgpack_unpacked_destroy(&unpacked);
    server->event_loop->ops->dispatch(server->event_loop, &event);
    return YETTY_YCTL_HANDLER_OK_BOOL(1);
}

static struct yetty_rpc_handler_result handle_mouse_up(const struct yetty_yctl_message *msg,
                                                       void *userdata)
{
    struct yetty_yctl_server *server = userdata;
    struct yetty_yui_event event = {0};
    msgpack_unpacked unpacked;
    msgpack_object *params;

    if (!server->event_loop) {
        return YETTY_YCTL_HANDLER_ERR("no event loop");
    }
    if (!msg->params || msg->params_len == 0) {
        return YETTY_YCTL_HANDLER_ERR("missing params");
    }

    msgpack_unpacked_init(&unpacked);
    if (msgpack_unpack_next(&unpacked, (const char *)msg->params, msg->params_len, NULL) !=
        MSGPACK_UNPACK_SUCCESS) {
        msgpack_unpacked_destroy(&unpacked);
        return YETTY_YCTL_HANDLER_ERR("invalid params");
    }
    params = &unpacked.data;

    event.type = YETTY_YCORE_MOUSE_UP;
    event.mouse.x = get_map_float(params, "x", 0);
    event.mouse.y = get_map_float(params, "y", 0);
    event.mouse.button = get_map_int(params, "button", 0);

    msgpack_unpacked_destroy(&unpacked);
    server->event_loop->ops->dispatch(server->event_loop, &event);
    return YETTY_YCTL_HANDLER_OK_BOOL(1);
}

static struct yetty_rpc_handler_result handle_mouse_move(const struct yetty_yctl_message *msg,
                                                         void *userdata)
{
    struct yetty_yctl_server *server = userdata;
    struct yetty_yui_event event = {0};
    msgpack_unpacked unpacked;
    msgpack_object *params;

    if (!server->event_loop) {
        return YETTY_YCTL_HANDLER_ERR("no event loop");
    }
    if (!msg->params || msg->params_len == 0) {
        return YETTY_YCTL_HANDLER_ERR("missing params");
    }

    msgpack_unpacked_init(&unpacked);
    if (msgpack_unpack_next(&unpacked, (const char *)msg->params, msg->params_len, NULL) !=
        MSGPACK_UNPACK_SUCCESS) {
        msgpack_unpacked_destroy(&unpacked);
        return YETTY_YCTL_HANDLER_ERR("invalid params");
    }
    params = &unpacked.data;

    event.type = YETTY_YCORE_MOUSE_MOVE;
    event.mouse.x = get_map_float(params, "x", 0);
    event.mouse.y = get_map_float(params, "y", 0);

    msgpack_unpacked_destroy(&unpacked);
    server->event_loop->ops->dispatch(server->event_loop, &event);
    return YETTY_YCTL_HANDLER_OK_BOOL(1);
}

static struct yetty_rpc_handler_result handle_mouse_scroll(const struct yetty_yctl_message *msg,
                                                           void *userdata)
{
    struct yetty_yctl_server *server = userdata;
    struct yetty_yui_event event = {0};
    msgpack_unpacked unpacked;
    msgpack_object *params;

    if (!server->event_loop) {
        return YETTY_YCTL_HANDLER_ERR("no event loop");
    }
    if (!msg->params || msg->params_len == 0) {
        return YETTY_YCTL_HANDLER_ERR("missing params");
    }

    msgpack_unpacked_init(&unpacked);
    if (msgpack_unpack_next(&unpacked, (const char *)msg->params, msg->params_len, NULL) !=
        MSGPACK_UNPACK_SUCCESS) {
        msgpack_unpacked_destroy(&unpacked);
        return YETTY_YCTL_HANDLER_ERR("invalid params");
    }
    params = &unpacked.data;

    event.type = YETTY_YCORE_MOUSE_SCROLL;
    event.mouse_scroll.x = get_map_float(params, "x", 0);
    event.mouse_scroll.y = get_map_float(params, "y", 0);
    event.mouse_scroll.dx = get_map_float(params, "dx", 0);
    event.mouse_scroll.dy = get_map_float(params, "dy", 0);
    event.mouse_scroll.mods = get_map_int(params, "mods", 0);

    msgpack_unpacked_destroy(&unpacked);
    server->event_loop->ops->dispatch(server->event_loop, &event);
    return YETTY_YCTL_HANDLER_OK_BOOL(1);
}

/* Notification: tear yetty down. Dispatches the same SHUTDOWN event the
 * SIGINT/SIGTERM signal handler uses (see libuv-event-loop.c on_signal),
 * which the top-level listener handles by stopping the event loop —
 * ending recording cleanly and exiting the process. No params. */
static struct yetty_rpc_handler_result handle_shutdown(const struct yetty_yctl_message *msg,
                                                       void *userdata)
{
    struct yetty_yctl_server *server = userdata;
    struct yetty_yui_event event = {.type = YETTY_YCORE_SHUTDOWN};

    (void)msg;

    if (!server->event_loop) {
        return YETTY_YCTL_HANDLER_ERR("no event loop");
    }

    server->event_loop->ops->dispatch(server->event_loop, &event);
    return YETTY_YCTL_HANDLER_OK_BOOL(1);
}

static struct yetty_rpc_handler_result handle_resize(const struct yetty_yctl_message *msg,
                                                     void *userdata)
{
    struct yetty_yctl_server *server = userdata;
    struct yetty_yui_event event = {0};
    msgpack_unpacked unpacked;
    msgpack_object *params;

    if (!server->event_loop) {
        return YETTY_YCTL_HANDLER_ERR("no event loop");
    }
    if (!msg->params || msg->params_len == 0) {
        return YETTY_YCTL_HANDLER_ERR("missing params");
    }

    msgpack_unpacked_init(&unpacked);
    if (msgpack_unpack_next(&unpacked, (const char *)msg->params, msg->params_len, NULL) !=
        MSGPACK_UNPACK_SUCCESS) {
        msgpack_unpacked_destroy(&unpacked);
        return YETTY_YCTL_HANDLER_ERR("invalid params");
    }
    params = &unpacked.data;

    event.type = YETTY_YCORE_RESIZE;
    event.resize.width = get_map_float(params, "width", 0);
    event.resize.height = get_map_float(params, "height", 0);

    msgpack_unpacked_destroy(&unpacked);
    server->event_loop->ops->dispatch(server->event_loop, &event);
    return YETTY_YCTL_HANDLER_OK_BOOL(1);
}

static struct yetty_ycore_void_result register_builtin_handlers(struct yetty_yctl_server *server)
{
    struct yetty_ycore_void_result res;

    if (!server) {
        return YETTY_ERR(yetty_ycore_void, "server is NULL");
    }

#define REG(method, handler)                                                                       \
    res = yetty_yctl_server_register_handler(server, YETTY_YCTL_CHANNEL_EVENT_LOOP, method,        \
                                             handler, server);                                     \
    if (YETTY_IS_ERR(res))                                                                         \
        return res;

    REG("key_down", handle_key_down);
    REG("key_up", handle_key_up);
    REG("char", handle_char);
    REG("mouse_down", handle_mouse_down);
    REG("mouse_up", handle_mouse_up);
    REG("mouse_move", handle_mouse_move);
    REG("mouse_scroll", handle_mouse_scroll);
    REG("resize", handle_resize);
    REG("shutdown", handle_shutdown);

#undef REG

    ytrace("yctl: registered builtin handlers");
    return YETTY_OK_VOID();
}
