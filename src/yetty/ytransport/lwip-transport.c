/* lwip-transport.c — conn_transport over an lwIP raw-API TCP connection.
 *
 * Single-threaded: every lwIP raw callback (connected/recv/sent/err)
 * and every conn_transport op run on the browser main thread, the same
 * context the netstack timer and relay-WebSocket callbacks run on. No
 * locking.
 *
 * Connect path (open):
 *   wait for DHCP to bind  →  dns_gethostbyname  →  tcp_connect
 *     → connected_cb fires on_connect
 *     → recv_cb delivers bytes to on_data, sent_cb drains backpressure
 *     → err_cb / closed → on_disconnect
 *
 * Send backpressure: tcp_write only accepts up to tcp_sndbuf(); the
 * remainder is held in a small pending ring and flushed from the lwIP
 * sent callback, so send() always accepts the whole buffer and the
 * upper layer (libssh2 / telnet) never sees a spurious failure.
 */

#include <yetty/ytransport/lwip-transport.h>
#include <yetty/ytransport/conn-transport.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/ip_addr.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Re-check interval while waiting for DHCP to bind before dialing. */
#define LWIP_TRANSPORT_READY_POLL_MS 100

struct lwip_transport {
    struct yetty_ytransport_conn_transport base;

    char *host;
    uint16_t port;

    struct tcp_pcb *pcb;
    int opened;     /* open() called */
    int connecting; /* tcp_connect in flight (pre on_connect) */
    int connected;  /* connected_cb fired */

    struct yetty_yevent_tcp_client_callbacks cb;

    /* Pending outbound bytes that didn't fit tcp_sndbuf yet. Flushed
     * from the lwIP sent callback. Grows on demand. */
    uint8_t *pending;
    size_t pending_cap;
    size_t pending_head;
    size_t pending_tail;
};

/* The conn handle is opaque to callers; they only hand it back to our
 * own send()/close(), which ignore it. Return the transport itself so
 * we don't add a second concrete struct yetty_yevent_conn to the
 * webasm build (iframe-transport already defines one). */
static struct yetty_yevent_conn *lwip_transport_conn(struct lwip_transport *transport)
{
    return (struct yetty_yevent_conn *)transport;
}

/* ---- pending-send ring -------------------------------------------- */

static void pending_append(struct lwip_transport *transport, const uint8_t *src, size_t len)
{
    if (transport->pending_head > 0) {
        size_t live = transport->pending_tail - transport->pending_head;
        memmove(transport->pending, transport->pending + transport->pending_head, live);
        transport->pending_tail = live;
        transport->pending_head = 0;
    }
    if (transport->pending_tail + len > transport->pending_cap) {
        size_t need = transport->pending_tail + len;
        size_t newcap = transport->pending_cap ? transport->pending_cap : 4096;
        while (newcap < need) {
            newcap *= 2;
        }
        uint8_t *grown = realloc(transport->pending, newcap);
        if (!grown) {
            ywarn("lwip-transport: dropping %zu bytes (pending realloc failed)", len);
            return;
        }
        transport->pending = grown;
        transport->pending_cap = newcap;
    }
    memcpy(transport->pending + transport->pending_tail, src, len);
    transport->pending_tail += len;
}

/* Write as much pending data as tcp_sndbuf allows; tcp_output to push. */
static void pending_flush(struct lwip_transport *transport)
{
    if (!transport->pcb || !transport->connected) {
        return;
    }
    int wrote_any = 0;
    while (transport->pending_tail > transport->pending_head) {
        size_t avail = transport->pending_tail - transport->pending_head;
        u16_t sndbuf = tcp_sndbuf(transport->pcb);
        if (sndbuf == 0) {
            break; /* sent_cb will retry when the peer ACKs */
        }
        size_t chunk = avail < sndbuf ? avail : sndbuf;
        err_t write_result = tcp_write(transport->pcb, transport->pending + transport->pending_head,
                                       (u16_t)chunk, TCP_WRITE_FLAG_COPY);
        if (write_result == ERR_MEM) {
            break;
        }
        if (write_result != ERR_OK) {
            ywarn("lwip-transport: tcp_write failed (err=%d)", (int)write_result);
            break;
        }
        transport->pending_head += chunk;
        wrote_any = 1;
    }
    if (transport->pending_head == transport->pending_tail) {
        transport->pending_head = 0;
        transport->pending_tail = 0;
    }
    if (wrote_any) {
        tcp_output(transport->pcb);
    }
}

/* ---- lwIP raw TCP callbacks --------------------------------------- */

YETTY_EXTERNAL_CALLBACK
static err_t lwip_transport_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)pcb;
    (void)len;
    struct lwip_transport *transport = arg;
    pending_flush(transport);
    return ERR_OK;
}

YETTY_EXTERNAL_CALLBACK
static err_t lwip_transport_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *segment, err_t err)
{
    struct lwip_transport *transport = arg;

    if (!segment) {
        /* Remote closed the connection. */
        yinfo("lwip-transport: remote closed %s:%u", transport->host, transport->port);
        if (transport->cb.on_disconnect) {
            transport->cb.on_disconnect(transport->cb.ctx);
        }
        transport->connected = 0;
        return ERR_OK;
    }
    if (err != ERR_OK) {
        pbuf_free(segment);
        return err;
    }

    /* Deliver each pbuf in the chain to on_data. */
    for (struct pbuf *part = segment; part != NULL; part = part->next) {
        if (part->len > 0 && transport->cb.on_data) {
            transport->cb.on_data(transport->cb.ctx, lwip_transport_conn(transport),
                                  (const char *)part->payload, (long)part->len);
        }
    }
    tcp_recved(pcb, segment->tot_len);
    pbuf_free(segment);
    return ERR_OK;
}

YETTY_EXTERNAL_CALLBACK
static void lwip_transport_err_cb(void *arg, err_t err)
{
    struct lwip_transport *transport = arg;
    /* lwIP has already freed the pcb by the time err_cb fires. */
    transport->pcb = NULL;
    int was_connecting = transport->connecting;
    transport->connecting = 0;
    transport->connected = 0;
    yerror("lwip-transport: connection error to %s:%u (err=%d)", transport->host, transport->port,
           (int)err);
    if (was_connecting) {
        if (transport->cb.on_connect_error) {
            transport->cb.on_connect_error(transport->cb.ctx, "lwip tcp error");
        }
    } else if (transport->cb.on_disconnect) {
        transport->cb.on_disconnect(transport->cb.ctx);
    }
}

YETTY_EXTERNAL_CALLBACK
static err_t lwip_transport_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)pcb;
    struct lwip_transport *transport = arg;
    transport->connecting = 0;
    if (err != ERR_OK) {
        yerror("lwip-transport: connect failed %s:%u (err=%d)", transport->host, transport->port,
               (int)err);
        if (transport->cb.on_connect_error) {
            transport->cb.on_connect_error(transport->cb.ctx, "lwip tcp connect failed");
        }
        return ERR_OK;
    }
    transport->connected = 1;
    yinfo("lwip-transport: connected to %s:%u", transport->host, transport->port);
    if (transport->cb.on_connect) {
        transport->cb.on_connect(transport->cb.ctx, lwip_transport_conn(transport));
    }
    pending_flush(transport);
    return ERR_OK;
}

/* ---- connect sequence --------------------------------------------- */

static void lwip_transport_do_connect(struct lwip_transport *transport, const ip_addr_t *ip)
{
    transport->pcb = tcp_new();
    if (!transport->pcb) {
        yerror("lwip-transport: tcp_new failed (out of pcbs)");
        if (transport->cb.on_connect_error) {
            transport->cb.on_connect_error(transport->cb.ctx, "lwip tcp_new failed");
        }
        return;
    }
    tcp_arg(transport->pcb, transport);
    tcp_err(transport->pcb, lwip_transport_err_cb);
    tcp_recv(transport->pcb, lwip_transport_recv_cb);
    tcp_sent(transport->pcb, lwip_transport_sent_cb);

    transport->connecting = 1;
    err_t connect_result =
        tcp_connect(transport->pcb, ip, transport->port, lwip_transport_connected_cb);
    if (connect_result != ERR_OK) {
        transport->connecting = 0;
        yerror("lwip-transport: tcp_connect failed %s:%u (err=%d)", transport->host,
               transport->port, (int)connect_result);
        if (transport->cb.on_connect_error) {
            transport->cb.on_connect_error(transport->cb.ctx, "lwip tcp_connect failed");
        }
    }
}

YETTY_EXTERNAL_CALLBACK
static void lwip_transport_dns_found_cb(const char *name, const ip_addr_t *ip, void *arg)
{
    (void)name;
    struct lwip_transport *transport = arg;
    if (!ip) {
        yerror("lwip-transport: DNS resolution failed for %s", transport->host);
        if (transport->cb.on_connect_error) {
            transport->cb.on_connect_error(transport->cb.ctx, "lwip dns failed");
        }
        return;
    }
    yinfo("lwip-transport: %s resolved to %s", transport->host, ipaddr_ntoa(ip));
    lwip_transport_do_connect(transport, ip);
}

static int lwip_transport_stack_ready(void)
{
    struct netif *netif = netif_default;
    return netif && netif_is_link_up(netif) && !ip4_addr_isany_val(*netif_ip4_addr(netif));
}

/* Begin (or resume, after a readiness wait) the connect sequence. */
YETTY_EXTERNAL_CALLBACK
static void lwip_transport_begin(void *arg)
{
    struct lwip_transport *transport = arg;

    if (!lwip_transport_stack_ready()) {
        /* DHCP hasn't bound yet — re-check shortly. */
        sys_timeout(LWIP_TRANSPORT_READY_POLL_MS, lwip_transport_begin, transport);
        return;
    }

    /* Numeric host? skip DNS. */
    ip_addr_t ip;
    if (ipaddr_aton(transport->host, &ip)) {
        lwip_transport_do_connect(transport, &ip);
        return;
    }

    err_t dns_result =
        dns_gethostbyname(transport->host, &ip, lwip_transport_dns_found_cb, transport);
    if (dns_result == ERR_OK) {
        /* Cached — resolve was synchronous. */
        lwip_transport_do_connect(transport, &ip);
    } else if (dns_result != ERR_INPROGRESS) {
        yerror("lwip-transport: dns_gethostbyname(%s) failed (err=%d)", transport->host,
               (int)dns_result);
        if (transport->cb.on_connect_error) {
            transport->cb.on_connect_error(transport->cb.ctx, "lwip dns start failed");
        }
    }
    /* ERR_INPROGRESS → lwip_transport_dns_found_cb fires later. */
}

/* ---- conn_transport ops ------------------------------------------- */

static int lwip_transport_open(struct yetty_ytransport_conn_transport *self,
                               const struct yetty_yevent_tcp_client_callbacks *cb)
{
    struct lwip_transport *transport = (struct lwip_transport *)self;
    if (!cb) {
        return -1;
    }
    if (transport->opened) {
        ywarn("lwip-transport: open() called twice (%s:%u)", transport->host, transport->port);
        return -1;
    }
    transport->cb = *cb;
    transport->opened = 1;
    yinfo("lwip-transport: dialing %s:%u over the in-browser netstack", transport->host,
          transport->port);
    /* Kick the connect sequence; it waits internally for DHCP. */
    lwip_transport_begin(transport);
    return 0;
}

static struct yetty_ycore_size_result lwip_transport_send(
    struct yetty_ytransport_conn_transport *self, struct yetty_yevent_conn *conn, const void *data,
    size_t len)
{
    struct lwip_transport *transport = (struct lwip_transport *)self;
    (void)conn;

    if (!transport->connected || !transport->pcb) {
        return YETTY_ERR(yetty_ycore_size, "lwip-transport: send before connected");
    }
    if (len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    /* Always accept the whole buffer: queue it, then flush what fits.
     * The remainder drains from the sent callback, so callers never see
     * a partial/failed send under backpressure. */
    pending_append(transport, (const uint8_t *)data, len);
    pending_flush(transport);
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_void_result lwip_transport_close(
    struct yetty_ytransport_conn_transport *self, struct yetty_yevent_conn *conn)
{
    struct lwip_transport *transport = (struct lwip_transport *)self;
    (void)conn;

    transport->connected = 0;
    transport->connecting = 0;
    if (transport->pcb) {
        /* Detach callbacks first so a late event can't reach a
         * half-freed transport, then close. If tcp_close fails (ERR_MEM)
         * abort to guarantee the pcb is released. */
        tcp_arg(transport->pcb, NULL);
        tcp_recv(transport->pcb, NULL);
        tcp_sent(transport->pcb, NULL);
        tcp_err(transport->pcb, NULL);
        if (tcp_close(transport->pcb) != ERR_OK) {
            tcp_abort(transport->pcb);
        }
        transport->pcb = NULL;
    }
    return YETTY_OK_VOID();
}

static void lwip_transport_destroy(struct yetty_ytransport_conn_transport *self)
{
    struct lwip_transport *transport = (struct lwip_transport *)self;
    if (!transport) {
        return;
    }
    /* Cancel any pending readiness/DNS retry timer aimed at us. */
    sys_untimeout(lwip_transport_begin, transport);
    lwip_transport_close(self, NULL);
    free(transport->pending);
    free(transport->host);
    free(transport);
}

static const struct yetty_ytransport_conn_transport_ops *lwip_transport_ops(void)
{
    static const struct yetty_ytransport_conn_transport_ops ops = {
        .open = lwip_transport_open,
        .send = lwip_transport_send,
        .close = lwip_transport_close,
        .destroy = lwip_transport_destroy,
    };
    return &ops;
}

struct yetty_ytransport_conn_transport *yetty_ytransport_lwip_transport_create(const char *host,
                                                                               uint16_t port)
{
    if (!host || !host[0]) {
        return NULL;
    }
    struct lwip_transport *transport = calloc(1, sizeof(*transport));
    if (!transport) {
        return NULL;
    }
    transport->host = strdup(host);
    if (!transport->host) {
        free(transport);
        return NULL;
    }
    transport->base.ops = lwip_transport_ops();
    transport->port = port;
    return &transport->base;
}
