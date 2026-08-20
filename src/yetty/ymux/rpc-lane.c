/* ymux RPC lane transport (#699.2) — see rpc-lane.h. */

#include "rpc-lane.h"

#include <yetty/ycore/result.h>

#include <stdlib.h>
#include <string.h>

struct rpc_lane_transport {
    struct yetty_yclass_transport base;
    yetty_ymux_rpc_lane_tx_fn tx;
    void *tx_userdata;
    /* Inbound response bytes pushed by the lane demux, consumed head-first by
     * recv_available. Control-plane sized (void-call replies), so a grow +
     * memmove ring is plenty. */
    uint8_t *rx_data;
    size_t rx_len;
    size_t rx_capacity;
};

static struct yetty_ycore_size_result lane_send(struct yetty_yclass_transport *transport,
                                                const void *bytes, size_t len)
{
    struct rpc_lane_transport *lane = (struct rpc_lane_transport *)transport;
    if (len && lane->tx) {
        struct yetty_ycore_void_result tx_res =
            lane->tx((const uint8_t *)bytes, len, lane->tx_userdata);
        if (YETTY_IS_ERR(tx_res)) {
            return YETTY_ERR(yetty_ycore_size, "ymux rpc lane: tx enqueue failed", tx_res);
        }
    }
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_size_result lane_recv(struct yetty_yclass_transport *transport, void *buf,
                                                size_t max)
{
    (void)transport;
    (void)buf;
    (void)max;
    /* The lane is async-only: inbound bytes arrive through the owner's event
     * loop (push_rx) and are drained via recv_available. A blocking recv here
     * would deadlock the single-threaded owner — refuse loudly. Admin
     * round-trips are avoided by seeding the session's name cache. */
    return YETTY_ERR(yetty_ycore_size, "ymux rpc lane: blocking recv forbidden (async lane)");
}

static struct yetty_ycore_size_result lane_recv_available(struct yetty_yclass_transport *transport,
                                                          void *buf, size_t max)
{
    struct rpc_lane_transport *lane = (struct rpc_lane_transport *)transport;
    size_t take = lane->rx_len < max ? lane->rx_len : max;
    if (take) {
        memcpy(buf, lane->rx_data, take);
        lane->rx_len -= take;
        memmove(lane->rx_data, lane->rx_data + take, lane->rx_len);
    }
    return YETTY_OK(yetty_ycore_size, take);
}

static struct yetty_ycore_void_result lane_flush(struct yetty_yclass_transport *transport)
{
    (void)transport;
    /* send() writes through to the lane callback — nothing buffered here. */
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result lane_destroy(struct yetty_yclass_transport *transport)
{
    struct rpc_lane_transport *lane = (struct rpc_lane_transport *)transport;
    free(lane->rx_data);
    free(lane);
    return YETTY_OK_VOID();
}

struct yetty_yclass_transport_ptr_result yetty_ymux_rpc_lane_create(yetty_ymux_rpc_lane_tx_fn tx,
                                                                    void *userdata)
{
    if (!tx) {
        return YETTY_ERR(yetty_yclass_transport_ptr, "ymux rpc lane: NULL tx callback");
    }
    struct rpc_lane_transport *lane = calloc(1, sizeof(struct rpc_lane_transport));
    if (!lane) {
        return YETTY_ERR(yetty_yclass_transport_ptr, "ymux rpc lane: calloc failed");
    }
    static const struct yetty_yclass_transport_ops ops = {
        .send = lane_send,
        .recv = lane_recv,
        .destroy = lane_destroy,
        .flush = lane_flush,
        .recv_available = lane_recv_available,
    };
    lane->base.ops = &ops;
    lane->tx = tx;
    lane->tx_userdata = userdata;
    return YETTY_OK(yetty_yclass_transport_ptr, &lane->base);
}

struct yetty_ycore_void_result yetty_ymux_rpc_lane_push_rx(struct yetty_yclass_transport *transport,
                                                           const uint8_t *bytes, size_t len)
{
    if (!transport || transport->ops->recv_available != lane_recv_available) {
        return YETTY_ERR(yetty_ycore_void, "ymux rpc lane push_rx: not a lane transport");
    }
    if (!len) {
        return YETTY_OK_VOID();
    }
    struct rpc_lane_transport *lane = (struct rpc_lane_transport *)transport;
    if (lane->rx_len + len > lane->rx_capacity) {
        size_t grown_capacity = lane->rx_capacity ? lane->rx_capacity : 256;
        while (grown_capacity < lane->rx_len + len) {
            grown_capacity *= 2;
        }
        uint8_t *grown = realloc(lane->rx_data, grown_capacity);
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "ymux rpc lane push_rx: realloc failed");
        }
        lane->rx_data = grown;
        lane->rx_capacity = grown_capacity;
    }
    memcpy(lane->rx_data + lane->rx_len, bytes, len);
    lane->rx_len += len;
    return YETTY_OK_VOID();
}
