/* websocket-transport.c — webasm-only yetty_ytransport_conn_transport
 * over a browser WebSocket, via emscripten/websocket.h.
 *
 * Lifecycle mirrors the other transports (tcp-transport,
 * iframe-transport): create(url) → open(callbacks) kicks off the
 * async connect → emscripten fires onopen/onmessage/onerror/onclose
 * on the main thread → we forward to the caller's
 * yetty_yevent_tcp_client_callbacks. send() maps 1:1 to a binary
 * WebSocket message.
 *
 * No routing table is needed (unlike iframe-transport): the
 * emscripten websocket callbacks carry a userData pointer, so each
 * socket dispatches straight to its owning transport.
 *
 * The conn handle passed to on_connect is the transport itself,
 * cast — callers treat struct yetty_yevent_conn as opaque and only
 * ever hand it back to this transport's own send()/close(), which
 * ignore it. This avoids defining a second concrete
 * struct yetty_yevent_conn in the webasm build (iframe-transport.c
 * already defines one).
 */

#include <yetty/ytransport/conn-transport.h>
#include <yetty/ytransport/websocket-transport.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

#include <emscripten/emscripten.h>
#include <emscripten/websocket.h>

#include <stdlib.h>
#include <string.h>

struct websocket_transport {
    struct yetty_ytransport_conn_transport base;

    char *url;                     /* owned copy of the ws:// / wss:// URL */
    EMSCRIPTEN_WEBSOCKET_T socket; /* 0 until open() */
    int opened;                    /* open() succeeded */
    int connected;                 /* onopen fired */

    /* Caller-supplied callbacks. Stored verbatim; .ctx is opaque to us. */
    struct yetty_yevent_tcp_client_callbacks cb;
};

/* The opaque conn handle is the transport itself — see file comment. */
static struct yetty_yevent_conn *websocket_transport_conn(struct websocket_transport *transport)
{
    return (struct yetty_yevent_conn *)transport;
}

/* ---- emscripten websocket callbacks ------------------------------
 * Signatures are dictated by emscripten/websocket.h; they run on the
 * browser main thread (this build is single-threaded), same context
 * the rest of yetty runs in. */

YETTY_EXTERNAL_CALLBACK
static EM_BOOL websocket_transport_on_open(int event_type,
                                           const EmscriptenWebSocketOpenEvent *event,
                                           void *user_data)
{
    (void)event_type;
    (void)event;
    struct websocket_transport *transport = user_data;

    transport->connected = 1;
    yinfo("websocket-transport: connected to %s", transport->url);
    if (transport->cb.on_connect) {
        transport->cb.on_connect(transport->cb.ctx, websocket_transport_conn(transport));
    }
    return EM_TRUE;
}

YETTY_EXTERNAL_CALLBACK
static EM_BOOL websocket_transport_on_message(int event_type,
                                              const EmscriptenWebSocketMessageEvent *event,
                                              void *user_data)
{
    (void)event_type;
    struct websocket_transport *transport = user_data;

    if (!transport->connected || event->numBytes == 0) {
        return EM_TRUE;
    }
    /* Text frames are passed through as bytes too — the PTY layers on
     * top treat everything as a byte payload. (Our servers only send
     * binary frames; this is belt-and-braces for foreign bridges.) */
    if (transport->cb.on_alloc) {
        /* Same API-consistency call the iframe transport makes: the
         * supplied buffer is ignored, we hand on_data our own pointer. */
        char *buf = NULL;
        size_t len = 0;
        transport->cb.on_alloc(transport->cb.ctx, (size_t)event->numBytes, &buf, &len);
        (void)buf;
        (void)len;
    }
    if (transport->cb.on_data) {
        transport->cb.on_data(transport->cb.ctx, websocket_transport_conn(transport),
                              (const char *)event->data, (long)event->numBytes);
    }
    return EM_TRUE;
}

YETTY_EXTERNAL_CALLBACK
static EM_BOOL websocket_transport_on_error(int event_type,
                                            const EmscriptenWebSocketErrorEvent *event,
                                            void *user_data)
{
    (void)event_type;
    (void)event;
    struct websocket_transport *transport = user_data;

    /* The browser deliberately hides error detail (security); all we
     * know is that the socket failed. Before onopen this is a connect
     * failure; after, the matching onclose follows and fires
     * on_disconnect. */
    if (!transport->connected) {
        yerror("websocket-transport: connect to %s failed", transport->url);
        if (transport->cb.on_connect_error) {
            transport->cb.on_connect_error(transport->cb.ctx, "websocket connect failed");
        }
    } else {
        ywarn("websocket-transport: socket error on %s", transport->url);
    }
    return EM_TRUE;
}

YETTY_EXTERNAL_CALLBACK
static EM_BOOL websocket_transport_on_close(int event_type,
                                            const EmscriptenWebSocketCloseEvent *event,
                                            void *user_data)
{
    (void)event_type;
    struct websocket_transport *transport = user_data;

    yinfo("websocket-transport: closed (code=%d clean=%d) %s", (int)event->code,
          (int)event->wasClean, transport->url);
    if (!transport->connected) {
        return EM_TRUE;
    }
    transport->connected = 0;
    if (transport->cb.on_disconnect) {
        transport->cb.on_disconnect(transport->cb.ctx);
    }
    return EM_TRUE;
}

/* ---- Transport ops ---------------------------------------------- */

static int websocket_transport_open(struct yetty_ytransport_conn_transport *self,
                                    const struct yetty_yevent_tcp_client_callbacks *cb)
{
    struct websocket_transport *transport = (struct websocket_transport *)self;

    if (!cb) {
        return -1;
    }
    if (transport->opened) {
        ywarn("websocket-transport: open() called twice (%s)", transport->url);
        return -1;
    }
    if (!emscripten_websocket_is_supported()) {
        yerror("websocket-transport: WebSocket not supported in this environment");
        return -1;
    }

    transport->cb = *cb;

    EmscriptenWebSocketCreateAttributes attributes;
    emscripten_websocket_init_create_attributes(&attributes);
    attributes.url = transport->url;

    EMSCRIPTEN_WEBSOCKET_T socket = emscripten_websocket_new(&attributes);
    if (socket <= 0) {
        yerror("websocket-transport: emscripten_websocket_new(%s) failed: %d", transport->url,
               (int)socket);
        return -1;
    }
    transport->socket = socket;

    emscripten_websocket_set_onopen_callback(socket, transport, websocket_transport_on_open);
    emscripten_websocket_set_onmessage_callback(socket, transport, websocket_transport_on_message);
    emscripten_websocket_set_onerror_callback(socket, transport, websocket_transport_on_error);
    emscripten_websocket_set_onclose_callback(socket, transport, websocket_transport_on_close);

    transport->opened = 1;
    yinfo("websocket-transport: connecting to %s", transport->url);
    return 0;
}

static struct yetty_ycore_size_result websocket_transport_send(
    struct yetty_ytransport_conn_transport *self, struct yetty_yevent_conn *conn, const void *data,
    size_t len)
{
    struct websocket_transport *transport = (struct websocket_transport *)self;
    (void)conn;

    if (!transport->connected) {
        return YETTY_ERR(yetty_ycore_size, "websocket-transport: send before connected");
    }
    if (len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    EMSCRIPTEN_RESULT send_result =
        emscripten_websocket_send_binary(transport->socket, (void *)data, (uint32_t)len);
    if (send_result != EMSCRIPTEN_RESULT_SUCCESS) {
        return YETTY_ERR(yetty_ycore_size, "websocket-transport: send failed");
    }
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_void_result websocket_transport_close(
    struct yetty_ytransport_conn_transport *self, struct yetty_yevent_conn *conn)
{
    struct websocket_transport *transport = (struct websocket_transport *)self;
    (void)conn;

    if (!transport->opened) {
        return YETTY_OK_VOID();
    }
    transport->connected = 0;
    /* 1000 = normal closure. Best-effort — the socket may already be
     * in CLOSING/CLOSED state, in which case emscripten returns an
     * error we don't care about. */
    emscripten_websocket_close(transport->socket, 1000, "yetty pty closed");
    return YETTY_OK_VOID();
}

static void websocket_transport_destroy(struct yetty_ytransport_conn_transport *self)
{
    struct websocket_transport *transport = (struct websocket_transport *)self;
    if (!transport) {
        return;
    }
    if (transport->opened) {
        websocket_transport_close(self, NULL);
        /* Drop the JS-side handler references before deleting so a
         * late event can't call into a freed transport. */
        emscripten_websocket_set_onopen_callback(transport->socket, NULL, NULL);
        emscripten_websocket_set_onmessage_callback(transport->socket, NULL, NULL);
        emscripten_websocket_set_onerror_callback(transport->socket, NULL, NULL);
        emscripten_websocket_set_onclose_callback(transport->socket, NULL, NULL);
        emscripten_websocket_delete(transport->socket);
        transport->socket = 0;
        transport->opened = 0;
    }
    free(transport->url);
    free(transport);
}

static const struct yetty_ytransport_conn_transport_ops *websocket_transport_ops(void)
{
    static const struct yetty_ytransport_conn_transport_ops ops = {
        .open = websocket_transport_open,
        .send = websocket_transport_send,
        .close = websocket_transport_close,
        .destroy = websocket_transport_destroy,
    };
    return &ops;
}

struct yetty_ytransport_conn_transport *yetty_ytransport_websocket_transport_create(const char *url)
{
    if (!url || !url[0]) {
        return NULL;
    }
    struct websocket_transport *transport = calloc(1, sizeof(*transport));
    if (!transport) {
        return NULL;
    }
    transport->url = strdup(url);
    if (!transport->url) {
        free(transport);
        return NULL;
    }
    transport->base.ops = websocket_transport_ops();
    return &transport->base;
}
