/* ymux RPC lane transport (#699.2) — a yetty_yclass_transport that rides the
 * ymux daemon⇄client socket as a dedicated framed lane.
 *
 * The producer side (the daemon's vtsink session) sends RPC request bytes
 * through a write-through callback that wraps them into lane frames
 * (YMUX_PROTO_VTSINK_RPC) toward one attachment; response bytes arriving in
 * lane frames are pushed into the transport's receive buffer and drained
 * non-blockingly by the session's pump. `recv_available` is declared, so the
 * session runs in ASYNC pipelined mode: typed void calls send and return, and
 * a blocking recv is forbidden — the lane peer publishes its root handle and
 * slot ids out of band (seed_remote_id_by_name), so no admin round-trip ever
 * needs one.
 *
 * Plain-C leaf transport, mirroring the yclass transport family (transport-fd
 * and friends); it is not a yclass class. */

#ifndef YETTY_YMUX_RPC_LANE_H
#define YETTY_YMUX_RPC_LANE_H

#include <yetty/yclass/transport.h>
#include <yetty/ycore/result.h>

#include <stddef.h>
#include <stdint.h>

/* Write-through sink for outbound request bytes: the owner wraps them into a
 * lane frame toward the peer and reports the enqueue Result — a failure MUST
 * propagate so the caller's typed RPC call fails synchronously and no
 * flow-control state advances for bytes that never reached the queue. */
typedef struct yetty_ycore_void_result (*yetty_ymux_rpc_lane_tx_fn)(const uint8_t *bytes,
                                                                    size_t len, void *userdata);

struct yetty_yclass_transport_ptr_result yetty_ymux_rpc_lane_create(yetty_ymux_rpc_lane_tx_fn tx,
                                                                    void *userdata);

/* Push response bytes that arrived in a lane frame into the transport's
 * receive buffer (drained by the session's recv_available). */
struct yetty_ycore_void_result yetty_ymux_rpc_lane_push_rx(struct yetty_yclass_transport *transport,
                                                           const uint8_t *bytes, size_t len);

#endif /* YETTY_YMUX_RPC_LANE_H */
