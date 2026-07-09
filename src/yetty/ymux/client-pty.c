/*
 * ymux client PTY — bridges the local terminal stack to a remote ymux server
 * pane. A self-pipe delivers decoded PTY bytes to the terminal (so the whole
 * existing yterminal/ymux_pane/yvterm render path is reused verbatim); a TCP
 * connection carries msgpack-RPC to/from the server. See client-pty.h.
 */
#include <yetty/ymux/client-pty.h>

#include <msgpack.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/ytrace/ytrace.h>

enum {
    YMUX_CLIENT_POLL_PERIOD_MS = 16, /* ~60 Hz drain of the server output log */
};

struct yetty_ymux_client_pty {
    struct yetty_platform_pty base;
    struct yetty_platform_pty_pipe_source pipe_source;

    struct yetty_yevent_event_loop *loop;               /* borrowed */
    struct yetty_ycore_xthread_event_pipe *output_pipe; /* owned; terminal-side reads */

    char *host; /* owned */
    int port;
    int pane;

    yetty_yevent_tcp_client_id tcp_id;
    int have_tcp_id;
    struct yetty_yevent_conn *conn; /* set on connect; NULL otherwise */
    int connected;
    int attached; /* first attach response seen → offset valid */

    uint32_t cols;
    uint32_t rows;
    uint64_t offset; /* absolute poll cursor into the server output log */

    msgpack_unpacker unpacker; /* streaming decode of server responses */
    uint32_t next_msgid;

    yetty_yevent_timer_id poll_timer;
    int have_timer;
    struct yetty_yevent_event_listener timer_listener;

    char read_buf[65536]; /* tcp on_alloc scratch */
};

/*===========================================================================
 * Request sending
 *=========================================================================*/

static void pack_request_header(msgpack_packer *packer, uint32_t msgid, const char *method)
{
    msgpack_pack_array(packer, 5);
    msgpack_pack_int(packer, 0); /* request */
    msgpack_pack_uint32(packer, msgid);
    msgpack_pack_int(packer, 0); /* channel EVENT_LOOP */
    size_t method_len = strlen(method);
    msgpack_pack_str(packer, method_len);
    msgpack_pack_str_body(packer, method, method_len);
}

static void client_send(struct yetty_ymux_client_pty *pty, const msgpack_sbuffer *buf)
{
    if (!pty->connected || !pty->conn) {
        return;
    }
    struct yetty_ycore_size_result sr = pty->loop->ops->tcp_send(pty->conn, buf->data, buf->size);
    if (YETTY_IS_ERR(sr)) {
        ydebug("ymux client: tcp_send failed: %s", sr.error.msg);
        yetty_ycore_error_destroy(sr.error);
    }
}

static void pack_str_kv(msgpack_packer *packer, const char *key, uint32_t value)
{
    size_t key_len = strlen(key);
    msgpack_pack_str(packer, key_len);
    msgpack_pack_str_body(packer, key, key_len);
    msgpack_pack_uint32(packer, value);
}

static void client_send_attach(struct yetty_ymux_client_pty *pty)
{
    msgpack_sbuffer buf;
    msgpack_sbuffer_init(&buf);
    msgpack_packer packer;
    msgpack_packer_init(&packer, &buf, msgpack_sbuffer_write);
    pack_request_header(&packer, ++pty->next_msgid, "ymux-attach");
    msgpack_pack_map(&packer, 3);
    pack_str_kv(&packer, "pane", (uint32_t)pty->pane);
    pack_str_kv(&packer, "cols", pty->cols);
    pack_str_kv(&packer, "rows", pty->rows);
    client_send(pty, &buf);
    msgpack_sbuffer_destroy(&buf);
}

static void client_send_poll(struct yetty_ymux_client_pty *pty)
{
    msgpack_sbuffer buf;
    msgpack_sbuffer_init(&buf);
    msgpack_packer packer;
    msgpack_packer_init(&packer, &buf, msgpack_sbuffer_write);
    pack_request_header(&packer, ++pty->next_msgid, "ymux-poll");
    msgpack_pack_map(&packer, 2);
    pack_str_kv(&packer, "pane", (uint32_t)pty->pane);
    msgpack_pack_str(&packer, 5);
    msgpack_pack_str_body(&packer, "since", 5);
    msgpack_pack_uint64(&packer, pty->offset);
    client_send(pty, &buf);
    msgpack_sbuffer_destroy(&buf);
}

static void client_send_resize(struct yetty_ymux_client_pty *pty)
{
    msgpack_sbuffer buf;
    msgpack_sbuffer_init(&buf);
    msgpack_packer packer;
    msgpack_packer_init(&packer, &buf, msgpack_sbuffer_write);
    pack_request_header(&packer, ++pty->next_msgid, "ymux-resize");
    msgpack_pack_map(&packer, 3);
    pack_str_kv(&packer, "pane", (uint32_t)pty->pane);
    pack_str_kv(&packer, "cols", pty->cols);
    pack_str_kv(&packer, "rows", pty->rows);
    client_send(pty, &buf);
    msgpack_sbuffer_destroy(&buf);
}

/*===========================================================================
 * Response handling
 *=========================================================================*/

/* Pull the retained-log bytes out of a poll/attach result map: update the
 * offset cursor and (for poll) push the data into the terminal-side pipe. */
static void process_result(struct yetty_ymux_client_pty *pty, const msgpack_object *result)
{
    if (result->type != MSGPACK_OBJECT_MAP) {
        return;
    }
    const uint8_t *data = NULL;
    size_t data_len = 0;
    int have_offset = 0;
    uint64_t offset = pty->offset;

    for (uint32_t i = 0; i < result->via.map.size; i++) {
        const msgpack_object_kv *kv = &result->via.map.ptr[i];
        if (kv->key.type != MSGPACK_OBJECT_STR) {
            continue;
        }
        if (kv->key.via.str.size == 6 && memcmp(kv->key.via.str.ptr, "offset", 6) == 0 &&
            kv->val.type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
            offset = kv->val.via.u64;
            have_offset = 1;
        } else if (kv->key.via.str.size == 4 && memcmp(kv->key.via.str.ptr, "data", 4) == 0 &&
                   kv->val.type == MSGPACK_OBJECT_BIN) {
            data = (const uint8_t *)kv->val.via.bin.ptr;
            data_len = kv->val.via.bin.size;
        }
    }

    if (data && data_len > 0) {
        struct yetty_ycore_size_result wr =
            pty->output_pipe->ops->write(pty->output_pipe, (const char *)data, data_len);
        if (YETTY_IS_ERR(wr)) {
            ywarn("ymux client: output pipe write failed: %s", wr.error.msg);
            yetty_ycore_error_destroy(wr.error);
        } else if (wr.value < data_len) {
            /* A short write would desync the byte stream. Poll chunks are far
             * smaller than the pipe buffer, so this should not happen; warn
             * loudly if it ever does rather than silently corrupt the model. */
            ywarn("ymux client: short pipe write (%zu/%zu) — model stream may desync", wr.value,
                  data_len);
        }
    }

    if (have_offset) {
        pty->offset = offset;
    }
    /* The first response we ever get back is the attach ack. */
    pty->attached = 1;
}

YETTY_EXTERNAL_CALLBACK
static void client_on_data(void *ctx, struct yetty_yevent_conn *conn, const char *data, long nread)
{
    (void)conn;
    struct yetty_ymux_client_pty *pty = ctx;
    if (nread <= 0) {
        return;
    }
    if (msgpack_unpacker_buffer_capacity(&pty->unpacker) < (size_t)nread) {
        if (!msgpack_unpacker_reserve_buffer(&pty->unpacker, (size_t)nread)) {
            ywarn("ymux client: unpacker reserve failed, dropping %ld bytes", nread);
            return;
        }
    }
    memcpy(msgpack_unpacker_buffer(&pty->unpacker), data, (size_t)nread);
    msgpack_unpacker_buffer_consumed(&pty->unpacker, (size_t)nread);

    msgpack_unpacked message;
    msgpack_unpacked_init(&message);
    msgpack_unpack_return ret;
    while ((ret = msgpack_unpacker_next(&pty->unpacker, &message)) == MSGPACK_UNPACK_SUCCESS) {
        const msgpack_object *obj = &message.data;
        /* Response frame: [1, msgid, error, result]. */
        if (obj->type == MSGPACK_OBJECT_ARRAY && obj->via.array.size >= 4 &&
            obj->via.array.ptr[0].type == MSGPACK_OBJECT_POSITIVE_INTEGER &&
            obj->via.array.ptr[0].via.u64 == 1) {
            const msgpack_object *error = &obj->via.array.ptr[2];
            const msgpack_object *result = &obj->via.array.ptr[3];
            if (error->type == MSGPACK_OBJECT_NIL) {
                process_result(pty, result);
            } else {
                ydebug("ymux client: server returned an error response");
            }
        }
    }
    msgpack_unpacked_destroy(&message);
    if (ret == MSGPACK_UNPACK_PARSE_ERROR) {
        ywarn("ymux client: msgpack parse error on server stream");
    }
}

static void client_on_alloc(void *ctx, size_t suggested, char **buf, size_t *len)
{
    (void)suggested;
    struct yetty_ymux_client_pty *pty = ctx;
    *buf = pty->read_buf;
    *len = sizeof(pty->read_buf);
}

YETTY_EXTERNAL_CALLBACK
static void client_on_connect(void *ctx, struct yetty_yevent_conn *conn)
{
    struct yetty_ymux_client_pty *pty = ctx;
    pty->conn = conn;
    pty->connected = 1;
    yinfo("ymux client: connected to %s:%d, attaching pane %d", pty->host, pty->port, pty->pane);
    client_send_attach(pty);
}

static void client_on_connect_error(void *ctx, const char *error)
{
    struct yetty_ymux_client_pty *pty = ctx;
    yerror("ymux client: connect to %s:%d failed: %s", pty->host, pty->port,
           error ? error : "(unknown)");
    pty->connected = 0;
    pty->conn = NULL;
}

static void client_on_disconnect(void *ctx)
{
    struct yetty_ymux_client_pty *pty = ctx;
    ydebug("ymux client: disconnected from server");
    pty->connected = 0;
    pty->conn = NULL;
}

/* Poll timer tick — request the next slice of server output. */
YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_int_result client_on_poll_tick(
    struct yetty_yevent_event_listener *listener, const struct yetty_yui_event *event)
{
    (void)event;
    struct yetty_ymux_client_pty *pty =
        (struct yetty_ymux_client_pty *)((char *)listener -
                                         offsetof(struct yetty_ymux_client_pty, timer_listener));
    if (pty->connected && pty->attached) {
        client_send_poll(pty);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

/*===========================================================================
 * PTY ops
 *=========================================================================*/

static struct yetty_ycore_size_result client_pty_read(struct yetty_platform_pty *self, char *buf,
                                                      size_t size)
{
    /* The terminal reads decoded bytes off the pipe fd via register_pty_pipe;
     * this sync path is unused. */
    (void)self;
    (void)buf;
    (void)size;
    return YETTY_OK(yetty_ycore_size, 0);
}

static struct yetty_ycore_size_result client_pty_write(struct yetty_platform_pty *self,
                                                       const char *data, size_t len)
{
    struct yetty_ymux_client_pty *pty = (struct yetty_ymux_client_pty *)self;
    if (len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    if (!pty->connected) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    msgpack_sbuffer buf;
    msgpack_sbuffer_init(&buf);
    msgpack_packer packer;
    msgpack_packer_init(&packer, &buf, msgpack_sbuffer_write);
    pack_request_header(&packer, ++pty->next_msgid, "ymux-input");
    msgpack_pack_map(&packer, 2);
    pack_str_kv(&packer, "pane", (uint32_t)pty->pane);
    msgpack_pack_str(&packer, 4);
    msgpack_pack_str_body(&packer, "data", 4);
    msgpack_pack_bin(&packer, len);
    msgpack_pack_bin_body(&packer, data, len);
    client_send(pty, &buf);
    msgpack_sbuffer_destroy(&buf);
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_void_result client_pty_resize(struct yetty_platform_pty *self,
                                                        uint32_t cols, uint32_t rows,
                                                        uint32_t pixel_w, uint32_t pixel_h)
{
    (void)pixel_w;
    (void)pixel_h;
    struct yetty_ymux_client_pty *pty = (struct yetty_ymux_client_pty *)self;
    if (cols == 0 || rows == 0) {
        return YETTY_OK_VOID();
    }
    pty->cols = cols;
    pty->rows = rows;
    if (pty->connected && pty->attached) {
        client_send_resize(pty);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result client_pty_stop(struct yetty_platform_pty *self)
{
    struct yetty_ymux_client_pty *pty = (struct yetty_ymux_client_pty *)self;
    if (pty->have_timer) {
        if (pty->loop->ops->deregister_timer_listener) {
            pty->loop->ops->deregister_timer_listener(pty->loop, pty->poll_timer,
                                                      &pty->timer_listener);
        }
        pty->loop->ops->stop_timer(pty->loop, pty->poll_timer);
        pty->loop->ops->destroy_timer(pty->loop, pty->poll_timer);
        pty->have_timer = 0;
    }
    if (pty->have_tcp_id) {
        struct yetty_ycore_void_result cr = pty->loop->ops->stop_tcp_client(pty->loop, pty->tcp_id);
        if (YETTY_IS_ERR(cr)) {
            yetty_ycore_error_destroy(cr.error);
        }
        pty->have_tcp_id = 0;
    }
    pty->connected = 0;
    pty->conn = NULL;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result client_pty_destroy(struct yetty_platform_pty *self)
{
    struct yetty_ymux_client_pty *pty = (struct yetty_ymux_client_pty *)self;
    struct yetty_ycore_void_result stop_res = client_pty_stop(self);
    if (YETTY_IS_ERR(stop_res)) {
        yetty_ycore_error_destroy(stop_res.error);
    }
    if (pty->output_pipe) {
        pty->output_pipe->ops->destroy(pty->output_pipe);
        pty->output_pipe = NULL;
    }
    msgpack_unpacker_destroy(&pty->unpacker);
    free(pty->host);
    free(pty);
    return YETTY_OK_VOID();
}

static struct yetty_platform_pty_pipe_source *client_pty_pipe_source(
    struct yetty_platform_pty *self)
{
    struct yetty_ymux_client_pty *pty = (struct yetty_ymux_client_pty *)self;
    return &pty->pipe_source;
}

static int client_pty_child_alive(struct yetty_platform_pty *self)
{
    struct yetty_ymux_client_pty *pty = (struct yetty_ymux_client_pty *)self;
    /* Alive while the transport is up (or still trying to connect). */
    return pty->connected ? 1 : 1;
}

static const struct yetty_platform_pty_ops client_pty_ops = {
    .destroy = client_pty_destroy,
    .read = client_pty_read,
    .write = client_pty_write,
    .resize = client_pty_resize,
    .stop = client_pty_stop,
    .pipe_source = client_pty_pipe_source,
    .child_alive = client_pty_child_alive,
};

/*===========================================================================
 * Construction
 *=========================================================================*/

struct yetty_yplatform_pty_ptr_result yetty_ymux_client_pty_create(
    const char *host, int port, int pane, struct yetty_yevent_event_loop *event_loop)
{
    if (!host || !event_loop) {
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ymux client pty: host/event_loop required");
    }

    struct yetty_ymux_client_pty *pty = calloc(1, sizeof(struct yetty_ymux_client_pty));
    if (!pty) {
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ymux client pty: alloc failed");
    }
    pty->base.ops = &client_pty_ops;
    pty->loop = event_loop;
    pty->host = strdup(host);
    pty->port = port;
    pty->pane = pane;
    pty->cols = 80;
    pty->rows = 24;
    pty->pipe_source.abstract = (uintptr_t)-1;
    msgpack_unpacker_init(&pty->unpacker, 65536);

    if (!pty->host) {
        msgpack_unpacker_destroy(&pty->unpacker);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ymux client pty: host strdup failed");
    }

    /* Decoded-output self-pipe — the terminal reads its fd like a real PTY. */
    struct yetty_yplatform_input_pipe_result pipe_res = yetty_platform_input_pipe_create();
    if (YETTY_IS_ERR(pipe_res)) {
        msgpack_unpacker_destroy(&pty->unpacker);
        free(pty->host);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ymux client pty: output pipe create failed",
                         pipe_res);
    }
    pty->output_pipe = pipe_res.value;

    struct yetty_ycore_int_result fd_res = pty->output_pipe->ops->read_fd(pty->output_pipe);
    if (YETTY_IS_ERR(fd_res)) {
        pty->output_pipe->ops->destroy(pty->output_pipe);
        msgpack_unpacker_destroy(&pty->unpacker);
        free(pty->host);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ymux client pty: pipe read fd failed", fd_res);
    }
    pty->pipe_source.abstract = (uintptr_t)fd_res.value;

    /* Poll timer — drains the server's output log at a steady cadence. */
    struct yetty_yevent_timer_id_result timer_res = event_loop->ops->create_timer(event_loop);
    if (YETTY_IS_ERR(timer_res)) {
        pty->output_pipe->ops->destroy(pty->output_pipe);
        msgpack_unpacker_destroy(&pty->unpacker);
        free(pty->host);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ymux client pty: create_timer failed",
                         timer_res);
    }
    pty->poll_timer = timer_res.value;
    pty->have_timer = 1;
    pty->timer_listener.handler = client_on_poll_tick;
    event_loop->ops->config_timer(event_loop, pty->poll_timer, YMUX_CLIENT_POLL_PERIOD_MS);
    struct yetty_ycore_void_result reg_res =
        event_loop->ops->register_timer_listener(event_loop, pty->poll_timer, &pty->timer_listener);
    if (YETTY_IS_ERR(reg_res)) {
        yetty_ycore_error_destroy(reg_res.error);
    }
    event_loop->ops->start_timer(event_loop, pty->poll_timer);

    /* Open the control connection — attach happens in on_connect. */
    struct yetty_yevent_tcp_client_callbacks callbacks = {
        .ctx = pty,
        .on_connect = client_on_connect,
        .on_connect_error = client_on_connect_error,
        .on_alloc = client_on_alloc,
        .on_data = client_on_data,
        .on_disconnect = client_on_disconnect,
    };
    struct yetty_yevent_tcp_client_id_result tcp_res =
        event_loop->ops->create_tcp_client(event_loop, host, port, &callbacks);
    if (YETTY_IS_ERR(tcp_res)) {
        struct yetty_ycore_void_result d = client_pty_destroy(&pty->base);
        if (YETTY_IS_ERR(d)) {
            yetty_ycore_error_destroy(d.error);
        }
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ymux client pty: create_tcp_client failed",
                         tcp_res);
    }
    pty->tcp_id = tcp_res.value;
    pty->have_tcp_id = 1;

    yinfo("ymux client pty: created for %s:%d pane %d (async connect in flight)", host, port, pane);
    return YETTY_OK(yetty_yplatform_pty_ptr, &pty->base);
}

/*===========================================================================
 * Factory
 *=========================================================================*/

struct yetty_ymux_client_pty_factory {
    struct yetty_yplatform_pty_factory base;
    char *host;
    int port;
    int pane;
};

static void client_factory_destroy(struct yetty_yplatform_pty_factory *self)
{
    struct yetty_ymux_client_pty_factory *factory = (struct yetty_ymux_client_pty_factory *)self;
    free(factory->host);
    free(factory);
}

static struct yetty_yplatform_pty_ptr_result client_factory_create_pty(
    struct yetty_yplatform_pty_factory *self, struct yetty_yevent_event_loop *event_loop)
{
    struct yetty_ymux_client_pty_factory *factory = (struct yetty_ymux_client_pty_factory *)self;
    return yetty_ymux_client_pty_create(factory->host, factory->port, factory->pane, event_loop);
}

static const struct yetty_yplatform_pty_factory_ops client_factory_ops = {
    .destroy = client_factory_destroy,
    .create_pty = client_factory_create_pty,
};

struct yetty_yplatform_pty_factory_ptr_result yetty_ymux_client_pty_factory_create(const char *host,
                                                                                   int port,
                                                                                   int pane)
{
    struct yetty_ymux_client_pty_factory *factory =
        calloc(1, sizeof(struct yetty_ymux_client_pty_factory));
    if (!factory) {
        return YETTY_ERR(yetty_yplatform_pty_factory_ptr, "ymux client factory: alloc failed");
    }
    factory->base.ops = &client_factory_ops;
    factory->host = strdup(host ? host : "127.0.0.1");
    factory->port = port;
    factory->pane = pane;
    if (!factory->host) {
        free(factory);
        return YETTY_ERR(yetty_yplatform_pty_factory_ptr, "ymux client factory: host dup failed");
    }
    return YETTY_OK(yetty_yplatform_pty_factory_ptr, &factory->base);
}
