/*
 * control.c — external msgpack-RPC control server for yai.
 *
 * A localhost TCP server (enabled with --rpc <port> / YAI_RPC_PORT) that
 * lets an outside client inject commands and query state, mirroring the
 * approach of yetty's yctl control server. It reuses yetty's msgpack-RPC
 * framing layer (yetty/yctl/rpc-message.h) for the wire format:
 *
 *   Request:      [0, msgid, channel, method, params]
 *   Response:     [1, msgid, error, result]
 *   Notification: [2, channel, method, params]
 *
 * Methods:
 *   run [line]   inject `line` as if typed + Enter (slash command, shell
 *                escape, or a message — queued if a turn is in flight).
 *   status       return a map: engine, model, session, waiting, queued.
 *
 * The server rides yai's own libuv loop (app->loop); handlers run on that
 * single thread, so they touch app state the same way the keyboard path
 * does — no locking needed.
 */
#include "app.h"

#include <yetty/yctl/rpc-message.h>
#include <yetty/ycore/result.h>

#include <msgpack.h>
#include <stdio.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <uv.h>

/* One accepted client connection. Connections are tracked in a list off
 * the server so they can all be closed at teardown. */
struct yai_control_conn {
    uv_tcp_t handle;
    struct yai_control *control;
    struct yai_control_conn *next;
    uint8_t *buf; /* accumulates bytes for streaming-safe framing */
    size_t len;
    size_t cap;
};

struct yai_control {
    uv_tcp_t server;
    struct yai_app *app;
    struct yai_control_conn *conns; /* open connections (intrusive list) */
};

/* An in-flight uv_write: the serialized bytes must outlive the async
 * write, so they ride along and are freed in the write callback. */
struct yai_control_write {
    uv_write_t req;
    uint8_t *data;
};

YETTY_EXTERNAL_CALLBACK
static void control_write_done(uv_write_t *req, int status)
{
    (void)status;
    struct yai_control_write *write = (struct yai_control_write *)req;
    free(write->data);
    free(write);
}

/* Send `len` bytes on the connection (best-effort: a dead peer just drops
 * the response). */
static void control_send(struct yai_control_conn *conn, const uint8_t *bytes, size_t len)
{
    struct yai_control_write *write = calloc(1, sizeof(*write));
    if (!write) {
        return;
    }
    write->data = malloc(len ? len : 1);
    if (!write->data) {
        free(write);
        return;
    }
    memcpy(write->data, bytes, len);
    uv_buf_t buf = uv_buf_init((char *)write->data, (unsigned int)len);
    if (uv_write(&write->req, (uv_stream_t *)&conn->handle, &buf, 1, control_write_done) != 0) {
        free(write->data);
        free(write);
    }
}

/* Pack and send a success response carrying the given msgpack `result`. */
static void control_respond_ok(struct yai_control_conn *conn, uint32_t msgid,
                               const uint8_t *result, size_t result_len)
{
    uint8_t storage[2048];
    struct yetty_yctl_write_buffer wbuf;
    yetty_yctl_write_buffer_init(&wbuf, storage, sizeof(storage));
    struct yetty_ycore_void_result write_res =
        yetty_yctl_write_response_ok(&wbuf, msgid, result, result_len);
    if (YETTY_IS_ERR(write_res)) {
        yetty_ycore_error_destroy(write_res.error);
        return;
    }
    control_send(conn, wbuf.data, wbuf.len);
}

static void control_respond_error(struct yai_control_conn *conn, uint32_t msgid, const char *message)
{
    uint8_t storage[512];
    struct yetty_yctl_write_buffer wbuf;
    yetty_yctl_write_buffer_init(&wbuf, storage, sizeof(storage));
    struct yetty_ycore_void_result write_res =
        yetty_yctl_write_response_error(&wbuf, msgid, message);
    if (YETTY_IS_ERR(write_res)) {
        yetty_ycore_error_destroy(write_res.error);
        return;
    }
    control_send(conn, wbuf.data, wbuf.len);
}

/* Pack a string key/value pair onto a map. */
static void control_pack_kv(msgpack_packer *packer, const char *key, const char *value)
{
    size_t key_len = strlen(key);
    msgpack_pack_str(packer, key_len);
    msgpack_pack_str_body(packer, key, key_len);
    size_t value_len = strlen(value);
    msgpack_pack_str(packer, value_len);
    msgpack_pack_str_body(packer, value, value_len);
}

/* Build and send the status map: engine, model, session, waiting, queued. */
static void control_method_status(struct yai_control_conn *conn, uint32_t msgid)
{
    struct yai_app *app = conn->control->app;
    const char *model = getenv("YAI_MODEL");
    if (!model || !model[0]) {
        model = "default";
    }
    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);
    msgpack_packer packer;
    msgpack_packer_init(&packer, &sbuf, msgpack_sbuffer_write);
    msgpack_pack_map(&packer, 5);
    control_pack_kv(&packer, "engine", app->engine_name);
    control_pack_kv(&packer, "model", model);
    control_pack_kv(&packer, "session", app->session_id[0] ? app->session_id : "");
    /* waiting */
    msgpack_pack_str(&packer, 7);
    msgpack_pack_str_body(&packer, "waiting", 7);
    if (app->waiting) {
        msgpack_pack_true(&packer);
    } else {
        msgpack_pack_false(&packer);
    }
    /* queued */
    msgpack_pack_str(&packer, 6);
    msgpack_pack_str_body(&packer, "queued", 6);
    msgpack_pack_int(&packer, app->queue_len);
    control_respond_ok(conn, msgid, (const uint8_t *)sbuf.data, sbuf.size);
    msgpack_sbuffer_destroy(&sbuf);
}

/* Decode params (a msgpack array) and inject its first string element as a
 * command line. */
static void control_method_run(struct yai_control_conn *conn, uint32_t msgid, const uint8_t *params,
                               size_t params_len)
{
    msgpack_unpacked unpacked;
    msgpack_unpacked_init(&unpacked);
    size_t offset = 0;
    int have_line = 0;
    char line[8192];
    size_t line_len = 0;
    if (params && params_len &&
        msgpack_unpack_next(&unpacked, (const char *)params, params_len, &offset) ==
            MSGPACK_UNPACK_SUCCESS &&
        unpacked.data.type == MSGPACK_OBJECT_ARRAY && unpacked.data.via.array.size >= 1) {
        msgpack_object *arg = &unpacked.data.via.array.ptr[0];
        if (arg->type == MSGPACK_OBJECT_STR) {
            line_len = arg->via.str.size;
            if (line_len >= sizeof(line)) {
                line_len = sizeof(line) - 1;
            }
            memcpy(line, arg->via.str.ptr, line_len);
            have_line = 1;
        }
    }
    msgpack_unpacked_destroy(&unpacked);

    if (!have_line) {
        control_respond_error(conn, msgid, "run: expected params [string]");
        return;
    }
    struct yetty_ycore_void_result inject_res =
        yai_control_inject(conn->control->app, line, line_len);
    if (YETTY_IS_ERR(inject_res)) {
        /* Surface to the operator too, then answer the client. */
        yai_report_error(conn->control->app, "control run", inject_res);
        control_respond_error(conn, msgid, "run: injection failed");
        return;
    }
    uint8_t storage[64];
    struct yetty_yctl_write_buffer wbuf;
    yetty_yctl_write_buffer_init(&wbuf, storage, sizeof(storage));
    struct yetty_ycore_void_result write_res = yetty_yctl_write_response_bool(&wbuf, msgid, 1);
    if (YETTY_IS_ERR(write_res)) {
        yetty_ycore_error_destroy(write_res.error);
        return;
    }
    control_send(conn, wbuf.data, wbuf.len);
}

static int method_is(const struct yetty_yctl_message *msg, const char *name)
{
    size_t name_len = strlen(name);
    return msg->method_len == name_len && memcmp(msg->method, name, name_len) == 0;
}

static void control_dispatch(struct yai_control_conn *conn, const struct yetty_yctl_message *msg)
{
    /* Notifications carry no msgid and expect no reply. */
    int reply = (msg->type == YETTY_YCTL_MSG_REQUEST);
    if (method_is(msg, "run")) {
        if (reply) {
            control_method_run(conn, msg->msgid, msg->params, msg->params_len);
        } else {
            char line[8192];
            /* Best-effort fire-and-forget for a notification. */
            msgpack_unpacked unpacked;
            msgpack_unpacked_init(&unpacked);
            size_t offset = 0;
            if (msg->params && msg->params_len &&
                msgpack_unpack_next(&unpacked, (const char *)msg->params, msg->params_len,
                                    &offset) == MSGPACK_UNPACK_SUCCESS &&
                unpacked.data.type == MSGPACK_OBJECT_ARRAY && unpacked.data.via.array.size >= 1 &&
                unpacked.data.via.array.ptr[0].type == MSGPACK_OBJECT_STR) {
                msgpack_object *arg = &unpacked.data.via.array.ptr[0];
                size_t line_len = arg->via.str.size;
                if (line_len >= sizeof(line)) {
                    line_len = sizeof(line) - 1;
                }
                memcpy(line, arg->via.str.ptr, line_len);
                yai_report_error(conn->control->app, "control run",
                                 yai_control_inject(conn->control->app, line, line_len));
            }
            msgpack_unpacked_destroy(&unpacked);
        }
        return;
    }
    if (method_is(msg, "status")) {
        if (reply) {
            control_method_status(conn, msg->msgid);
        }
        return;
    }
    if (reply) {
        control_respond_error(conn, msg->msgid, "method not found");
    }
}

YETTY_EXTERNAL_CALLBACK
static void control_conn_closed(uv_handle_t *handle)
{
    struct yai_control_conn *conn = handle->data;
    /* Unlink from the server's connection list. */
    struct yai_control_conn **link = &conn->control->conns;
    while (*link) {
        if (*link == conn) {
            *link = conn->next;
            break;
        }
        link = &(*link)->next;
    }
    free(conn->buf);
    free(conn);
}

YETTY_EXTERNAL_CALLBACK
static void control_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    (void)handle;
    buf->base = malloc(suggested_size ? suggested_size : 1);
    buf->len = buf->base ? suggested_size : 0;
}

YETTY_EXTERNAL_CALLBACK
static void control_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    struct yai_control_conn *conn = stream->data;
    if (nread < 0) {
        free(buf->base);
        if (!uv_is_closing((uv_handle_t *)stream)) {
            uv_close((uv_handle_t *)stream, control_conn_closed);
        }
        return;
    }
    if (nread == 0) {
        free(buf->base);
        return;
    }
    /* Append to the connection's accumulation buffer. */
    if (conn->len + (size_t)nread > conn->cap) {
        size_t new_cap = conn->cap ? conn->cap * 2 : 4096;
        while (new_cap < conn->len + (size_t)nread) {
            new_cap *= 2;
        }
        uint8_t *grown = realloc(conn->buf, new_cap);
        if (!grown) {
            free(buf->base);
            uv_close((uv_handle_t *)stream, control_conn_closed);
            return;
        }
        conn->buf = grown;
        conn->cap = new_cap;
    }
    memcpy(conn->buf + conn->len, buf->base, (size_t)nread);
    conn->len += (size_t)nread;
    free(buf->base);

    /* Parse every complete message; keep the trailing partial bytes. */
    size_t offset = 0;
    while (offset < conn->len) {
        size_t consumed = 0;
        struct yetty_rpc_message_result parse_res =
            yetty_yctl_message_parse(conn->buf + offset, conn->len - offset, &consumed);
        if (YETTY_IS_ERR(parse_res)) {
            yetty_ycore_error_destroy(parse_res.error);
            uv_close((uv_handle_t *)stream, control_conn_closed);
            return;
        }
        if (consumed == 0) {
            break; /* partial message — wait for more bytes */
        }
        control_dispatch(conn, &parse_res.value);
        free((void *)parse_res.value.params);
        offset += consumed;
    }
    if (offset > 0) {
        memmove(conn->buf, conn->buf + offset, conn->len - offset);
        conn->len -= offset;
    }
}

YETTY_EXTERNAL_CALLBACK
static void control_on_connection(uv_stream_t *server_stream, int status)
{
    if (status < 0) {
        return;
    }
    struct yai_control *control = server_stream->data;
    struct yai_control_conn *conn = calloc(1, sizeof(*conn));
    if (!conn) {
        return;
    }
    conn->control = control;
    if (uv_tcp_init(&control->app->loop, &conn->handle) != 0) {
        free(conn);
        return;
    }
    conn->handle.data = conn;
    if (uv_accept(server_stream, (uv_stream_t *)&conn->handle) != 0) {
        uv_close((uv_handle_t *)&conn->handle, control_conn_closed);
        return;
    }
    conn->next = control->conns;
    control->conns = conn;
    if (uv_read_start((uv_stream_t *)&conn->handle, control_alloc, control_read) != 0) {
        uv_close((uv_handle_t *)&conn->handle, control_conn_closed);
    }
}

YETTY_EXTERNAL_CALLBACK
static void control_server_closed(uv_handle_t *handle)
{
    struct yai_control *control = handle->data;
    free(control);
}

struct yetty_ycore_void_result yai_control_start(struct yai_app *app, int port)
{
    if (port <= 0 || port > 65535) {
        return YETTY_ERR(yetty_ycore_void, "yai_control_start: port out of range");
    }
    struct yai_control *control = calloc(1, sizeof(*control));
    if (!control) {
        return YETTY_ERR(yetty_ycore_void, "yai_control_start: calloc failed");
    }
    control->app = app;
    if (uv_tcp_init(&app->loop, &control->server) != 0) {
        free(control);
        return YETTY_ERR(yetty_ycore_void, "yai_control_start: uv_tcp_init failed");
    }
    control->server.data = control;
    struct sockaddr_in addr;
    if (uv_ip4_addr("127.0.0.1", port, &addr) != 0) {
        uv_close((uv_handle_t *)&control->server, control_server_closed);
        return YETTY_ERR(yetty_ycore_void, "yai_control_start: uv_ip4_addr failed");
    }
    if (uv_tcp_bind(&control->server, (const struct sockaddr *)&addr, 0) != 0) {
        uv_close((uv_handle_t *)&control->server, control_server_closed);
        return YETTY_ERR(yetty_ycore_void, "yai_control_start: bind failed (port in use?)");
    }
    if (uv_listen((uv_stream_t *)&control->server, 16, control_on_connection) != 0) {
        uv_close((uv_handle_t *)&control->server, control_server_closed);
        return YETTY_ERR(yetty_ycore_void, "yai_control_start: listen failed");
    }
    app->control = control;
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Client mode — one yai driving another over its --rpc control server.
 *
 * A blocking, one-shot client (no libuv): connect, send one request, print
 * the response, exit. This is what `yai --connect <port> <method>
 * [args...]` runs, so a yai is itself the client of another running yai.
 *---------------------------------------------------------------------------*/

static int control_write_all(int fd, const uint8_t *bytes, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t wrote = write(fd, bytes + sent, len - sent);
        if (wrote < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent += (size_t)wrote;
    }
    return 0;
}

int yai_control_client_main(const char *host, int port, const char *method,
                            const char *const *args, int arg_count)
{
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "yai --connect: port out of range\n");
        return 2;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "yai --connect: socket failed\n");
        return 1;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "yai --connect: bad host '%s'\n", host);
        close(fd);
        return 2;
    }
    /* Don't hang forever on a wedged peer. */
    struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "yai --connect: cannot reach %s:%d (is the other yai started with --rpc?)\n",
                host, port);
        close(fd);
        return 1;
    }

    /* Pack the request: [0, msgid, channel, method, [args...]]. */
    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);
    msgpack_packer packer;
    msgpack_packer_init(&packer, &sbuf, msgpack_sbuffer_write);
    msgpack_pack_array(&packer, 5);
    msgpack_pack_int(&packer, 0); /* request */
    msgpack_pack_int(&packer, 1); /* msgid */
    msgpack_pack_int(&packer, 0); /* channel */
    size_t method_len = strlen(method);
    msgpack_pack_str(&packer, method_len);
    msgpack_pack_str_body(&packer, method, method_len);
    msgpack_pack_array(&packer, (size_t)arg_count);
    for (int index = 0; index < arg_count; index++) {
        size_t arg_len = strlen(args[index]);
        msgpack_pack_str(&packer, arg_len);
        msgpack_pack_str_body(&packer, args[index], arg_len);
    }
    int send_status = control_write_all(fd, (const uint8_t *)sbuf.data, sbuf.size);
    msgpack_sbuffer_destroy(&sbuf);
    if (send_status != 0) {
        fprintf(stderr, "yai --connect: send failed\n");
        close(fd);
        return 1;
    }

    /* Read until one full response message is buffered. */
    uint8_t response[65536];
    size_t total = 0;
    msgpack_unpacked unpacked;
    msgpack_unpacked_init(&unpacked);
    int parsed = 0;
    while (total < sizeof(response)) {
        ssize_t got = recv(fd, response + total, sizeof(response) - total, 0);
        if (got <= 0) {
            break;
        }
        total += (size_t)got;
        size_t offset = 0;
        if (msgpack_unpack_next(&unpacked, (const char *)response, total, &offset) ==
            MSGPACK_UNPACK_SUCCESS) {
            parsed = 1;
            break;
        }
    }
    close(fd);
    if (!parsed) {
        fprintf(stderr, "yai --connect: no/!invalid response\n");
        msgpack_unpacked_destroy(&unpacked);
        return 1;
    }

    /* Response shape: [1, msgid, error, result]. */
    int exit_code = 0;
    msgpack_object obj = unpacked.data;
    if (obj.type == MSGPACK_OBJECT_ARRAY && obj.via.array.size >= 4) {
        msgpack_object *error = &obj.via.array.ptr[2];
        msgpack_object *result = &obj.via.array.ptr[3];
        if (error->type != MSGPACK_OBJECT_NIL) {
            fprintf(stderr, "error: ");
            msgpack_object_print(stderr, *error);
            fprintf(stderr, "\n");
            exit_code = 1;
        } else if (result->type != MSGPACK_OBJECT_NIL) {
            msgpack_object_print(stdout, *result);
            putchar('\n');
        }
    } else {
        fprintf(stderr, "yai --connect: unexpected response shape\n");
        exit_code = 1;
    }
    msgpack_unpacked_destroy(&unpacked);
    return exit_code;
}

void yai_control_stop(struct yai_app *app)
{
    struct yai_control *control = app->control;
    if (!control) {
        return;
    }
    app->control = NULL;
    /* Close every open client connection, then the listener. */
    struct yai_control_conn *conn = control->conns;
    while (conn) {
        struct yai_control_conn *next = conn->next;
        if (!uv_is_closing((uv_handle_t *)&conn->handle)) {
            uv_close((uv_handle_t *)&conn->handle, control_conn_closed);
        }
        conn = next;
    }
    if (!uv_is_closing((uv_handle_t *)&control->server)) {
        uv_close((uv_handle_t *)&control->server, control_server_closed);
    } else {
        free(control);
    }
}
