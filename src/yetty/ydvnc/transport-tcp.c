/*
 * transport-tcp.c — plain TCP transport via the platform event loop.
 */

#include "transport.h"

#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/event-loop.h>
#include <yetty/ytrace/ytrace.h>

#define RECV_BUF_INITIAL (64u * 1024u)

struct tcp_transport {
    struct yetty_ydvnc_transport base;
    struct yetty_yplatform_event_loop *event_loop;
    yetty_ycore_tcp_client_id client_id;
    struct yetty_ycore_conn *conn;

    struct yetty_ydvnc_transport_callbacks cbs;
    int connected;

    /* recv-side scratch buffer — the event loop's tcp_client allocator
     * fills from here. The transport doesn't parse — it forwards every
     * received chunk verbatim to the rfb-client via on_data. */
    uint8_t *recv_buf;
    size_t recv_buf_cap;
};

/*===========================================================================
 * Event-loop callback adapters
 *===========================================================================*/

static void cb_on_connect(void *ctx, struct yetty_ycore_conn *conn)
{
    struct tcp_transport *self = ctx;
    self->conn = conn;
    self->connected = 1;
    if (self->cbs.on_connect) {
        self->cbs.on_connect(self->cbs.ctx);
    }
}

static void cb_on_connect_error(void *ctx, const char *error)
{
    struct tcp_transport *self = ctx;
    self->connected = 0;
    self->conn = NULL;
    self->client_id = -1;
    if (self->cbs.on_connect_error) {
        self->cbs.on_connect_error(self->cbs.ctx, error ? error : "(null)");
    }
}

static void cb_on_alloc(void *ctx, size_t suggested, char **buf, size_t *len)
{
    struct tcp_transport *self = ctx;
    if (suggested == 0) {
        suggested = 4096;
    }
    if (suggested > self->recv_buf_cap) {
        size_t new_cap = suggested;
        if (new_cap < RECV_BUF_INITIAL) {
            new_cap = RECV_BUF_INITIAL;
        }
        uint8_t *p = realloc(self->recv_buf, new_cap);
        if (p) {
            self->recv_buf = p;
            self->recv_buf_cap = new_cap;
        }
    }
    *buf = (char *)self->recv_buf;
    *len = self->recv_buf_cap;
}

YETTY_EXTERNAL_CALLBACK
static void cb_on_data(void *ctx, struct yetty_ycore_conn *conn, const char *data, long nread)
{
    struct tcp_transport *self = ctx;
    (void)conn;
    if (nread <= 0) {
        return;
    }
    if (self->cbs.on_data) {
        self->cbs.on_data(self->cbs.ctx, (const uint8_t *)data, (size_t)nread);
    }
}

static void cb_on_disconnect(void *ctx)
{
    struct tcp_transport *self = ctx;
    self->connected = 0;
    self->conn = NULL;
    self->client_id = -1;
    if (self->cbs.on_disconnect) {
        self->cbs.on_disconnect(self->cbs.ctx);
    }
}

/*===========================================================================
 * vtable ops
 *===========================================================================*/

static struct yetty_ycore_void_result tcp_op_connect(
    struct yetty_ydvnc_transport *base, const char *host, uint16_t port,
    const struct yetty_ydvnc_transport_callbacks *cbs)
{
    struct tcp_transport *self = (struct tcp_transport *)base;
    if (self->connected || self->client_id >= 0) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc tcp transport: already connected");
    }
    if (!host || !cbs) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc tcp transport: null host or callbacks");
    }

    self->cbs = *cbs;

    struct yetty_ycore_client_callbacks loop_cbs = {
        .ctx = self,
        .on_connect = cb_on_connect,
        .on_connect_error = cb_on_connect_error,
        .on_alloc = cb_on_alloc,
        .on_data = cb_on_data,
        .on_disconnect = cb_on_disconnect,
    };

    struct yetty_ycore_tcp_client_id_result id_res =
        self->event_loop->ops->create_tcp_client(self->event_loop, host, (int)port, &loop_cbs);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "ydvnc tcp transport: create_tcp_client failed");

    self->client_id = id_res.value;
    yinfo("ydvnc tcp: connecting to %s:%u (id=%d)", host, port, (int)self->client_id);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result tcp_op_send(struct yetty_ydvnc_transport *base,
                                                  const void *data, size_t nbytes)
{
    struct tcp_transport *self = (struct tcp_transport *)base;
    if (!self->connected || !self->conn) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc tcp transport: not connected");
    }
    struct yetty_ycore_size_result sr = self->event_loop->ops->tcp_send(self->conn, data, nbytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ydvnc tcp transport: send failed");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result tcp_op_disconnect(struct yetty_ydvnc_transport *base)
{
    struct tcp_transport *self = (struct tcp_transport *)base;
    if (self->client_id >= 0) {
        struct yetty_ycore_void_result r =
            self->event_loop->ops->stop_tcp_client(self->event_loop, self->client_id);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        self->client_id = -1;
    }
    self->connected = 0;
    self->conn = NULL;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result tcp_op_destroy(struct yetty_ydvnc_transport *base)
{
    struct tcp_transport *self = (struct tcp_transport *)base;
    if (!self) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result r = tcp_op_disconnect(base);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
    free(self->recv_buf);
    free(self);
    return YETTY_OK_VOID();
}

static const struct yetty_ydvnc_transport_ops tcp_ops = {
    .connect = tcp_op_connect,
    .send = tcp_op_send,
    .disconnect = tcp_op_disconnect,
    .destroy = tcp_op_destroy,
};

struct yetty_ydvnc_transport_ptr_result yetty_ydvnc_transport_tcp_create(
    struct yetty_yplatform_event_loop *event_loop)
{
    if (!event_loop) {
        return YETTY_ERR(yetty_ydvnc_transport_ptr, "ydvnc tcp transport: event_loop is NULL");
    }
    struct tcp_transport *self = calloc(1, sizeof(struct tcp_transport));
    if (!self) {
        return YETTY_ERR(yetty_ydvnc_transport_ptr, "ydvnc tcp transport: alloc failed");
    }
    self->base.ops = &tcp_ops;
    self->event_loop = event_loop;
    self->client_id = -1;
    return YETTY_OK(yetty_ydvnc_transport_ptr, &self->base);
}
