/* netstack.c — lwIP (NO_SYS) over an L2 relay WebSocket.
 *
 * Bring-up sequence:
 *   create() → lwip_init + netif_add (ethernet) + open relay WebSocket
 *            + start a 50 ms timer driving sys_check_timeouts()
 *   ws onopen → netif_set_link_up + dhcp_start
 *   ws onmessage(frame) → pbuf + netif->input (→ ethernet_input)
 *   netif linkoutput(frame) → emscripten_websocket_send_binary
 *   netif status callback → log the DHCP lease when an address binds
 *
 * Everything runs on the browser main thread (single-threaded build),
 * so there is no locking: lwIP's raw API is entered only from these
 * callbacks, never re-entrantly.
 */

#include <yetty/ynet/netstack.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "lwip/prot/ethernet.h"

#include <emscripten/emscripten.h>
#include <emscripten/websocket.h>

#include <stdlib.h>
#include <string.h>

/* Largest ethernet frame we ship/accept (1500 MTU + 14 header + a
 * little slack). One WebSocket binary message == one frame. */
#define YETTY_YNET_FRAME_MAX 1600
/* lwIP timeout wheel cadence. 50 ms is plenty for DHCP/TCP timers. */
#define YETTY_YNET_TICK_MS 50.0

struct yetty_ynet_netstack {
    struct netif netif;
    EMSCRIPTEN_WEBSOCKET_T socket;
    char *relay_url;
    long timer_id;
    uint8_t mac[6];
    int link_up;   /* relay socket open */
    int bound;     /* DHCP has given us an address */
};

/* ---- lwIP netif glue --------------------------------------------- */

/* Send one ethernet frame (lwIP → relay). pbuf may be a chain; flatten
 * into a stack buffer for the single binary WebSocket message. */
static err_t netstack_linkoutput(struct netif *netif, struct pbuf *frame)
{
    struct yetty_ynet_netstack *netstack = netif->state;
    if (!netstack->link_up) {
        return ERR_IF;
    }
    if (frame->tot_len > YETTY_YNET_FRAME_MAX) {
        ywarn("ynet: dropping oversize frame (%u bytes)", (unsigned)frame->tot_len);
        return ERR_MEM;
    }
    uint8_t bytes[YETTY_YNET_FRAME_MAX];
    u16_t copied = pbuf_copy_partial(frame, bytes, frame->tot_len, 0);
    EMSCRIPTEN_RESULT send_result =
        emscripten_websocket_send_binary(netstack->socket, bytes, copied);
    return send_result == EMSCRIPTEN_RESULT_SUCCESS ? ERR_OK : ERR_IF;
}

static err_t netstack_netif_init(struct netif *netif)
{
    struct yetty_ynet_netstack *netstack = netif->state;
    netif->name[0] = 'y';
    netif->name[1] = '0';
    netif->output = etharp_output;       /* IP → ARP → linkoutput */
    netif->linkoutput = netstack_linkoutput;
    netif->mtu = 1500;
    netif->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(netif->hwaddr, netstack->mac, ETH_HWADDR_LEN);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    return ERR_OK;
}

/* Fires on link/address changes. Log the lease once it binds. */
static void netstack_status_callback(struct netif *netif)
{
    struct yetty_ynet_netstack *netstack = netif->state;
    const ip4_addr_t *ip = netif_ip4_addr(netif);
    if (ip4_addr_isany_val(*ip)) {
        return; /* up but not yet addressed */
    }
    if (netstack->bound) {
        return; /* already logged this lease */
    }
    netstack->bound = 1;

    /* ip4addr_ntoa returns a shared static buffer — copy each field out
     * before the next call clobbers it. */
    char ip_str[16];
    char gw_str[16];
    char dns_str[16];
    strncpy(ip_str, ip4addr_ntoa(ip), sizeof(ip_str) - 1);
    ip_str[sizeof(ip_str) - 1] = '\0';
    strncpy(gw_str, ip4addr_ntoa(netif_ip4_gw(netif)), sizeof(gw_str) - 1);
    gw_str[sizeof(gw_str) - 1] = '\0';
    const ip_addr_t *dns = dns_getserver(0);
    strncpy(dns_str, ipaddr_ntoa(dns), sizeof(dns_str) - 1);
    dns_str[sizeof(dns_str) - 1] = '\0';

    yinfo("ynet: DHCP lease bound — ip=%s gw=%s mask=%s dns=%s", ip_str, gw_str,
          ip4addr_ntoa(netif_ip4_netmask(netif)), dns_str);
}

/* ---- WebSocket (relay uplink) callbacks --------------------------- */

YETTY_EXTERNAL_CALLBACK
static EM_BOOL netstack_on_open(int event_type, const EmscriptenWebSocketOpenEvent *event,
                               void *user_data)
{
    (void)event_type;
    (void)event;
    struct yetty_ynet_netstack *netstack = user_data;
    netstack->link_up = 1;
    netif_set_link_up(&netstack->netif);
    yinfo("ynet: relay connected (%s), starting DHCP", netstack->relay_url);
    err_t dhcp_result = dhcp_start(&netstack->netif);
    if (dhcp_result != ERR_OK) {
        yerror("ynet: dhcp_start failed (err=%d)", (int)dhcp_result);
    }
    return EM_TRUE;
}

YETTY_EXTERNAL_CALLBACK
static EM_BOOL netstack_on_message(int event_type, const EmscriptenWebSocketMessageEvent *event,
                                  void *user_data)
{
    (void)event_type;
    struct yetty_ynet_netstack *netstack = user_data;
    if (event->numBytes == 0 || event->isText) {
        return EM_TRUE; /* relay speaks binary frames only */
    }
    struct pbuf *frame = pbuf_alloc(PBUF_RAW, (u16_t)event->numBytes, PBUF_POOL);
    if (!frame) {
        ywarn("ynet: out of pbufs, dropping inbound frame (%u bytes)",
              (unsigned)event->numBytes);
        return EM_TRUE;
    }
    pbuf_take(frame, event->data, (u16_t)event->numBytes);
    if (netstack->netif.input(frame, &netstack->netif) != ERR_OK) {
        pbuf_free(frame);
    }
    return EM_TRUE;
}

YETTY_EXTERNAL_CALLBACK
static EM_BOOL netstack_on_error(int event_type, const EmscriptenWebSocketErrorEvent *event,
                                void *user_data)
{
    (void)event_type;
    (void)event;
    struct yetty_ynet_netstack *netstack = user_data;
    yerror("ynet: relay socket error (%s)", netstack->relay_url);
    return EM_TRUE;
}

YETTY_EXTERNAL_CALLBACK
static EM_BOOL netstack_on_close(int event_type, const EmscriptenWebSocketCloseEvent *event,
                                void *user_data)
{
    (void)event_type;
    struct yetty_ynet_netstack *netstack = user_data;
    yinfo("ynet: relay disconnected (code=%d clean=%d)", (int)event->code,
          (int)event->wasClean);
    netstack->link_up = 0;
    netif_set_link_down(&netstack->netif);
    return EM_TRUE;
}

/* ---- Timer (drives lwIP's timeout wheel) -------------------------- */

YETTY_EXTERNAL_CALLBACK
static void netstack_tick(void *user_data)
{
    (void)user_data; /* lwIP timeout state is global */
    sys_check_timeouts();
}

/* ---- Lifecycle ---------------------------------------------------- */

struct yetty_ynet_netstack_ptr_result yetty_ynet_netstack_create(const char *relay_url)
{
    if (!relay_url || !relay_url[0]) {
        return YETTY_ERR(yetty_ynet_netstack_ptr, "ynet: relay url required");
    }
    if (!emscripten_websocket_is_supported()) {
        return YETTY_ERR(yetty_ynet_netstack_ptr, "ynet: WebSocket unsupported in this browser");
    }

    struct yetty_ynet_netstack *netstack = calloc(1, sizeof(*netstack));
    if (!netstack) {
        return YETTY_ERR(yetty_ynet_netstack_ptr, "ynet: alloc failed");
    }
    netstack->relay_url = strdup(relay_url);
    if (!netstack->relay_url) {
        free(netstack);
        return YETTY_ERR(yetty_ynet_netstack_ptr, "ynet: relay url copy failed");
    }

    /* Locally-administered unicast MAC: bit1 of the first octet set
     * (locally administered), bit0 clear (unicast); the rest random so
     * we don't collide with other tenants on a shared relay segment. */
    netstack->mac[0] = 0x02;
    for (int i = 1; i < 6; i++) {
        netstack->mac[i] = (uint8_t)(lwip_port_rand() & 0xff);
    }

    lwip_init();

    ip4_addr_t zero;
    ip4_addr_set_zero(&zero);
    if (!netif_add(&netstack->netif, &zero, &zero, &zero, netstack, netstack_netif_init,
                   netif_input)) {
        free(netstack->relay_url);
        free(netstack);
        return YETTY_ERR(yetty_ynet_netstack_ptr, "ynet: netif_add failed");
    }
    netif_set_default(&netstack->netif);
    netif_set_status_callback(&netstack->netif, netstack_status_callback);
    netif_set_up(&netstack->netif);

    /* Drive lwIP timers from a browser interval (DHCP/TCP retransmits). */
    netstack->timer_id = emscripten_set_interval(netstack_tick, YETTY_YNET_TICK_MS, netstack);

    EmscriptenWebSocketCreateAttributes attributes;
    emscripten_websocket_init_create_attributes(&attributes);
    attributes.url = netstack->relay_url;
    netstack->socket = emscripten_websocket_new(&attributes);
    if (netstack->socket <= 0) {
        emscripten_clear_interval(netstack->timer_id);
        netif_remove(&netstack->netif);
        free(netstack->relay_url);
        free(netstack);
        return YETTY_ERR(yetty_ynet_netstack_ptr, "ynet: emscripten_websocket_new failed");
    }
    emscripten_websocket_set_onopen_callback(netstack->socket, netstack, netstack_on_open);
    emscripten_websocket_set_onmessage_callback(netstack->socket, netstack, netstack_on_message);
    emscripten_websocket_set_onerror_callback(netstack->socket, netstack, netstack_on_error);
    emscripten_websocket_set_onclose_callback(netstack->socket, netstack, netstack_on_close);

    yinfo("ynet: netstack up, connecting relay %s (mac %02x:%02x:%02x:%02x:%02x:%02x)",
          netstack->relay_url, netstack->mac[0], netstack->mac[1], netstack->mac[2],
          netstack->mac[3], netstack->mac[4], netstack->mac[5]);
    return YETTY_OK(yetty_ynet_netstack_ptr, netstack);
}

int yetty_ynet_netstack_is_ready(const struct yetty_ynet_netstack *netstack)
{
    return netstack && netstack->bound;
}

void yetty_ynet_netstack_destroy(struct yetty_ynet_netstack *netstack)
{
    if (!netstack) {
        return;
    }
    if (netstack->socket > 0) {
        emscripten_websocket_set_onopen_callback(netstack->socket, NULL, NULL);
        emscripten_websocket_set_onmessage_callback(netstack->socket, NULL, NULL);
        emscripten_websocket_set_onerror_callback(netstack->socket, NULL, NULL);
        emscripten_websocket_set_onclose_callback(netstack->socket, NULL, NULL);
        emscripten_websocket_close(netstack->socket, 1000, "yetty netstack shutdown");
        emscripten_websocket_delete(netstack->socket);
    }
    if (netstack->timer_id) {
        emscripten_clear_interval(netstack->timer_id);
    }
    netif_remove(&netstack->netif);
    free(netstack->relay_url);
    free(netstack);
}
