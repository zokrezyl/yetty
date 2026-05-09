/* iframe-transport.c — webasm-only yetty_ytransport_conn_transport over postMessage.
 *
 * Each transport instance represents a single yetty terminal session.
 * On open(), it:
 *   1. allocates a clientSid (process-local monotonic id),
 *   2. registers itself in the global clientSid → transport map,
 *   3. installs a one-shot postMessage listener (lazy, on first open),
 *   4. posts {type:'session-open', clientSid, port} to the iframe.
 *
 * The iframe responds with {type:'session-opened', clientSid, ok}.
 * On `ok=true` we fire the caller's on_connect with our private
 * struct yetty_yevent_conn (which is just our handle pointer plus
 * the clientSid). All subsequent {type:'session-rx', clientSid, data}
 * messages are routed to the matching transport's on_data callback.
 *
 * Send / close post {type:'session-tx'} / {type:'session-close'}
 * with the clientSid; the iframe's demux finds the wasm sid and
 * calls _tinyemu_session_send / _close on the slirp chr-backend.
 *
 * Concrete struct yetty_yevent_conn — webasm doesn't link
 * libuv-event-loop.c, so we own this typedef without symbol clash.
 * Each transport allocates exactly one conn (1:1 with the
 * connection lifecycle).
 */

#include <yetty/ytransport/conn-transport.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/ycore/result.h>
#include <yetty/yplatform/iframe-transport.h>
#include <yetty/ytrace/ytrace.h>

#include <emscripten/emscripten.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The opaque type telnet-pty (and other transport users) handle
 * around. Same name as the libuv-event-loop.c struct, but webasm
 * doesn't compile that file so there's no collision. Concrete fields
 * are ours alone. */
struct yetty_yevent_conn {
    uint32_t clientSid;
    /* Back-pointer to the transport that owns this conn — used by
     * the routing layer when an inbound session-rx arrives. */
    struct iframe_transport *transport;
};

struct iframe_transport {
    struct yetty_ytransport_conn_transport base;

    uint16_t port;
    uint32_t clientSid;
    int opened;    /* open() succeeded; transport in the route table */
    int connected; /* iframe acked session-opened ok */

    /* Caller-supplied callbacks. Stored verbatim; .ctx is opaque to us. */
    struct yetty_yevent_tcp_client_callbacks cb;

    /* The single conn we hand back via on_connect. Allocated lazily
     * inside dispatch_opened so on_connect always sees a valid
     * pointer; freed in close()/destroy(). */
    struct yetty_yevent_conn *conn;
};

/* ---- Routing table -----------------------------------------------
 * clientSid → transport. New sids are dense (monotonic counter), so
 * a small slot array is the simplest fit. 64 slots is more than
 * enough for the multi-terminal use case (and slot reuse is fine —
 * close() clears the slot). */

#define YETTY_IFRAME_MAX_TRANSPORTS 64

struct iframe_transport_slot {
    uint32_t clientSid;
    struct iframe_transport *transport;
};

static struct iframe_transport_slot g_slots[YETTY_IFRAME_MAX_TRANSPORTS];
static uint32_t g_next_clientSid = 1;
static int g_listener_installed = 0;

static struct iframe_transport *find_transport(uint32_t clientSid)
{
    for (int i = 0; i < YETTY_IFRAME_MAX_TRANSPORTS; i++) {
        if (g_slots[i].transport && g_slots[i].clientSid == clientSid) {
            return g_slots[i].transport;
        }
    }
    return NULL;
}

static int register_transport(struct iframe_transport *t)
{
    for (int i = 0; i < YETTY_IFRAME_MAX_TRANSPORTS; i++) {
        if (!g_slots[i].transport) {
            g_slots[i].clientSid = t->clientSid;
            g_slots[i].transport = t;
            return 0;
        }
    }
    return -1;
}

static void unregister_transport(struct iframe_transport *t)
{
    for (int i = 0; i < YETTY_IFRAME_MAX_TRANSPORTS; i++) {
        if (g_slots[i].transport == t) {
            g_slots[i].transport = NULL;
            g_slots[i].clientSid = 0;
            return;
        }
    }
}

/* ---- Dispatch entry points (called from JS) ----------------------
 * Exported under fixed names so the postMessage listener installed
 * below in install_listener_once() can reach them. */

EMSCRIPTEN_KEEPALIVE
void yetty_iframe_transport_on_opened(uint32_t clientSid, int ok)
{
    EM_ASM(
        {
            console.log('[yetty-side] yetty_iframe_transport_on_opened clientSid=' + $0 + ' ok=' +
                        $1);
        },
        clientSid, ok);
    struct iframe_transport *t = find_transport(clientSid);
    if (!t) {
        ywarn("iframe-transport: stray session-opened for clientSid=%u", clientSid);
        return;
    }
    if (!ok) {
        if (t->cb.on_connect_error) {
            t->cb.on_connect_error(t->cb.ctx, "iframe session-open failed");
        }
        return;
    }
    if (!t->conn) {
        t->conn = calloc(1, sizeof(*t->conn));
        if (!t->conn) {
            if (t->cb.on_connect_error) {
                t->cb.on_connect_error(t->cb.ctx, "conn alloc failed");
            }
            return;
        }
        t->conn->clientSid = clientSid;
        t->conn->transport = t;
    }
    t->connected = 1;
    if (t->cb.on_connect) {
        t->cb.on_connect(t->cb.ctx, t->conn);
    }
}

EMSCRIPTEN_KEEPALIVE
void yetty_iframe_transport_on_rx(uint32_t clientSid, const char *data, int nread)
{
    EM_ASM(
        {
            console.log('[yetty-side] yetty_iframe_transport_on_rx clientSid=' + $0 + ' nread=' +
                        $1);
        },
        clientSid, nread);
    struct iframe_transport *t = find_transport(clientSid);
    if (!t || !t->connected || nread <= 0) {
        EM_ASM(
            {
                console.warn('[yetty-side] on_rx DROPPED — t=' + ($0 ? 'ok' : 'NULL') +
                             ' connected=' + $1 + ' nread=' + $2);
            },
            !!t, t ? t->connected : 0, nread);
        return;
    }
    /* on_alloc isn't strictly required by telnet-pty (it allocates
     * its own read_buf), but call it for API consistency with the
     * TCP path so any future transport user that DOES rely on it
     * keeps working. The supplied buffer's contents are ignored —
     * we hand telnet-pty our own pointer. */
    if (t->cb.on_alloc) {
        char *buf = NULL;
        size_t len = 0;
        t->cb.on_alloc(t->cb.ctx, (size_t)nread, &buf, &len);
        (void)buf;
        (void)len;
    }
    if (t->cb.on_data) {
        t->cb.on_data(t->cb.ctx, t->conn, data, (long)nread);
    }
}

EMSCRIPTEN_KEEPALIVE
void yetty_iframe_transport_on_closed(uint32_t clientSid)
{
    struct iframe_transport *t = find_transport(clientSid);
    if (!t) {
        return;
    }
    t->connected = 0;
    if (t->cb.on_disconnect) {
        t->cb.on_disconnect(t->cb.ctx);
    }
}

/* ---- One-shot postMessage listener install ----------------------
 * Bridges {session-opened, session-rx, session-closed} from the
 * iframe to the C dispatch entry points above. We install once per
 * yetty.wasm process — the first transport's open() triggers it,
 * subsequent transports share. */

static void install_listener_once(void)
{
    if (g_listener_installed) {
        return;
    }
    g_listener_installed = 1;
    EM_ASM({
        if (window.__yettyIframeTransportListener) {
            return;
        }
        window.__yettyIframeTransportListener = function(e)
        {
            if (!e.data || typeof e.data.clientSid != = 'number') {
                return;
            }
            var sid = e.data.clientSid >>> 0;
            if (e.data.type == = 'session-opened') {
                Module._yetty_iframe_transport_on_opened(sid, e.data.ok ? 1 : 0);
                return;
            }
            if (e.data.type == = 'session-rx') {
                /* data is a Uint8Array-ish (postMessage structured
                 * clone preserves typed arrays). Copy into wasm
                 * heap as a transient buffer for the on_data call. */
                var bytes = e.data.data;
                if (!(bytes instanceof Uint8Array)) {
                    if (bytes && bytes.buffer) {
                        bytes = new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength);
                    } else if (bytes &&bytes.length != = undefined) {
                        bytes = new Uint8Array(bytes);
                    } else {
                        return;
                    }
                }
                if (bytes.length == = 0) {
                    return;
                }
                var ptr = Module._malloc(bytes.length);
                if (ptr == = 0) {
                    return;
                }
                Module.HEAPU8.set(bytes, ptr);
                Module._yetty_iframe_transport_on_rx(sid, ptr, bytes.length);
                Module._free(ptr);
                return;
            }
            if (e.data.type == = 'session-closed') {
                Module._yetty_iframe_transport_on_closed(sid);
                return;
            }
        };
        window.addEventListener('message', window.__yettyIframeTransportListener);
    });
}

/* ---- Transport ops ---------------------------------------------- */

static int iframe_transport_open(struct yetty_ytransport_conn_transport *self,
                                 const struct yetty_yevent_tcp_client_callbacks *cb)
{
    struct iframe_transport *t = (struct iframe_transport *)self;
    EM_ASM(
        { console.log('[yetty-side] iframe_transport_open clientSid=' + $0 + ' port=' + $1); },
        t->clientSid, (int)t->port);
    if (!cb) {
        return -1;
    }
    if (t->opened) {
        ywarn("iframe-transport: open() called twice (clientSid=%u)", t->clientSid);
        return -1;
    }

    t->cb = *cb;

    install_listener_once();

    if (register_transport(t) != 0) {
        yerror("iframe-transport: route table full (>%d sessions)", YETTY_IFRAME_MAX_TRANSPORTS);
        return -1;
    }
    t->opened = 1;

    /* Post the session-open. Iframe will respond with session-opened
     * once it's called Module._tinyemu_session_open and got a wasm
     * sid back (or queued the open if VM isn't ready yet — it will
     * still ack eventually, possibly with ok=false). */
    EM_ASM(
        {
            var clientSid = $0;
            var port = $1;
            var iframe = document.getElementById('yetty-vm-pty-1');
            if (iframe && iframe.contentWindow) {
                iframe.contentWindow.postMessage(
                    {type : 'session-open', clientSid : clientSid, port : port}, '*');
            }
        },
        t->clientSid, (int)t->port);

    yinfo("iframe-transport: posted session-open clientSid=%u port=%u", t->clientSid, t->port);
    return 0;
}

static struct yetty_ycore_size_result iframe_transport_send(
    struct yetty_ytransport_conn_transport *self, struct yetty_yevent_conn *conn, const void *data,
    size_t len)
{
    struct iframe_transport *t = (struct iframe_transport *)self;
    (void)conn;

    EM_ASM(
        {
            console.log('[yetty-side] iframe_transport_send clientSid=' + $0 + ' connected=' + $1 +
                        ' len=' + $2);
        },
        t->clientSid, t->connected, (int)len);

    if (!t->connected) {
        return YETTY_ERR(yetty_ycore_size, "iframe-transport: send before connected");
    }
    if (len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    /* Post raw bytes as a Uint8Array (copy on JS side). postMessage's
     * structured clone preserves the typed array. */
    EM_ASM(
        {
            var clientSid = $0;
            var ptr = $1;
            var len = $2;
            /* slice() copies — the wasm buffer may be reused by the
         * caller as soon as send() returns. */
            var bytes = HEAPU8.slice(ptr, ptr + len);
            var iframe = document.getElementById('yetty-vm-pty-1');
            if (iframe && iframe.contentWindow) {
                iframe.contentWindow.postMessage(
                    {type : 'session-tx', clientSid : clientSid, data : bytes}, '*');
            }
        },
        t->clientSid, data, (int)len);

    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_void_result iframe_transport_close(
    struct yetty_ytransport_conn_transport *self, struct yetty_yevent_conn *conn)
{
    struct iframe_transport *t = (struct iframe_transport *)self;
    (void)conn;

    if (!t->opened) {
        return YETTY_OK_VOID();
    }

    EM_ASM(
        {
            var clientSid = $0;
            var iframe = document.getElementById('yetty-vm-pty-1');
            if (iframe && iframe.contentWindow) {
                iframe.contentWindow.postMessage({type : 'session-close', clientSid : clientSid},
                                                 '*');
            }
        },
        t->clientSid);

    t->connected = 0;
    /* Keep the slot registered until destroy() — late session-rx
     * messages just drop on the floor (find_transport returns NULL
     * once the slot is cleared). */
    return YETTY_OK_VOID();
}

static void iframe_transport_destroy(struct yetty_ytransport_conn_transport *self)
{
    struct iframe_transport *t = (struct iframe_transport *)self;
    if (!t) {
        return;
    }
    if (t->opened) {
        iframe_transport_close(self, t->conn);
        unregister_transport(t);
        t->opened = 0;
    }
    free(t->conn);
    free(t);
}

static const struct yetty_ytransport_conn_transport_ops iframe_transport_ops = {
    .open = iframe_transport_open,
    .send = iframe_transport_send,
    .close = iframe_transport_close,
    .destroy = iframe_transport_destroy,
};

struct yetty_ytransport_conn_transport *yetty_yplatform_iframe_transport_create(uint16_t port)
{
    struct iframe_transport *t = calloc(1, sizeof(*t));
    if (!t) {
        return NULL;
    }
    t->base.ops = &iframe_transport_ops;
    t->port = port;
    t->clientSid = g_next_clientSid++;
    return &t->base;
}
