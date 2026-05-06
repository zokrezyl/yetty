/* tcp-transport.c — TCP backend for yetty_yconn_transport.
 *
 * Thin shim over the event loop's create_tcp_client / tcp_send /
 * tcp_close. Used by the desktop / iOS / Android telnet PTY factory
 * so the same telnet protocol code (ytelnet/telnet-pty.c) can run
 * on top of either real TCP (this file) or postMessage-into-slirp
 * (yplatform/webasm/iframe-transport.c).
 */

#include <yetty/ycore/tcp-transport.h>
#include <yetty/ycore/conn-transport.h>
#include <yetty/ycore/event-loop.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

#include <stdlib.h>
#include <string.h>

struct tcp_transport {
    struct yetty_yconn_transport base;

    char *host;
    uint16_t port;
    struct yetty_yplatform_event_loop *event_loop;

    yetty_ycore_tcp_client_id tcp_client_id;
    int tcp_client_active;

    /* Stored copy of the caller's callbacks — handed to
     * create_tcp_client by reference, so the storage must outlive
     * the open() call. */
    struct yetty_ycore_client_callbacks cb;
};

static int tcp_transport_open(struct yetty_yconn_transport *self,
                              const struct yetty_ycore_client_callbacks *cb)
{
    struct tcp_transport *t = (struct tcp_transport *)self;

    if (!cb) {
        return -1;
    }
    if (t->tcp_client_active) {
        ywarn("tcp_transport: open called twice on the same transport");
        return -1;
    }

    t->cb = *cb;

    struct yetty_ycore_tcp_client_id_result res =
        t->event_loop->ops->create_tcp_client(t->event_loop, t->host,
                                              (int)t->port, &t->cb);
    if (!res.ok) {
        return -1;
    }
    t->tcp_client_id = res.value;
    t->tcp_client_active = 1;
    return 0;
}

static struct yetty_ycore_size_result tcp_transport_send(
    struct yetty_yconn_transport *self, struct yetty_ycore_conn *conn,
    const void *data, size_t len)
{
    struct tcp_transport *t = (struct tcp_transport *)self;
    return t->event_loop->ops->tcp_send(conn, data, len);
}

static struct yetty_ycore_void_result tcp_transport_close(
    struct yetty_yconn_transport *self, struct yetty_ycore_conn *conn)
{
    struct tcp_transport *t = (struct tcp_transport *)self;
    (void)conn;
    /* Stopping the client tears down both the connect-in-flight and
     * the established conn — matches the existing telnet_pty_stop
     * semantics. */
    if (t->tcp_client_active) {
        t->event_loop->ops->stop_tcp_client(t->event_loop, t->tcp_client_id);
        t->tcp_client_active = 0;
    }
    return YETTY_OK_VOID();
}

static void tcp_transport_destroy(struct yetty_yconn_transport *self)
{
    struct tcp_transport *t = (struct tcp_transport *)self;
    if (!t) {
        return;
    }
    if (t->tcp_client_active) {
        t->event_loop->ops->stop_tcp_client(t->event_loop, t->tcp_client_id);
        t->tcp_client_active = 0;
    }
    free(t->host);
    free(t);
}

static const struct yetty_yconn_transport_ops tcp_transport_ops = {
    .open    = tcp_transport_open,
    .send    = tcp_transport_send,
    .close   = tcp_transport_close,
    .destroy = tcp_transport_destroy,
};

struct yetty_yconn_transport *yetty_ycore_tcp_transport_create(
    const char *host, uint16_t port,
    struct yetty_yplatform_event_loop *event_loop)
{
    if (!host || !event_loop || !event_loop->ops) {
        return NULL;
    }
    struct tcp_transport *t = calloc(1, sizeof(*t));
    if (!t) {
        return NULL;
    }
    t->base.ops = &tcp_transport_ops;
    t->host = strdup(host);
    t->port = port;
    t->event_loop = event_loop;
    if (!t->host) {
        free(t);
        return NULL;
    }
    return &t->base;
}
