/* yclass RPC DCS server adapter — server side.
 *
 * Attaches a yclass RPC dispatcher to an existing
 * `yetty_ywire_wire_statemachine` by registering a handler for the
 * given DCS code. Each inbound DCS envelope on `dcs_code` is
 * treated as one yrpc request frame (`u32 header | u32 body_len |
 * u8 body[]`); the handler reads it, runs
 * `yetty_yclass_rpc_dispatch_one`, then emits the response
 * (`u32 resp_len | u8 resp[]`) back as a DCS envelope on the same
 * code via the caller-supplied `emit` callback. */

#ifndef YCLASS_RPC_DCS_SERVER_H
#define YCLASS_RPC_DCS_SERVER_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ywire_wire_statemachine;

/* Opaque server state minted by attach. The wire_statemachine borrows
 * it (handler userdata); the caller owns it and destroys it AFTER the
 * wire_statemachine is destroyed. */
struct yetty_yclass_rpc_dcs_server;

YETTY_YRESULT_DECLARE(yetty_yclass_rpc_dcs_server_ptr, struct yetty_yclass_rpc_dcs_server *);

/* Caller-supplied function that ships an already-encoded DCS envelope
 * (`bytes`/`n`) to the peer. Implementation typically wraps
 * `pty->ops->write` (server runs inside the terminal that owns the
 * PTY master) so the bytes land on the client's stdin. */
typedef struct yetty_ycore_void_result (*yetty_yclass_rpc_dcs_emit_fn)(const uint8_t *bytes,
                                                                       size_t n, void *user);

/* Register the DCS RPC handler on `sm`. The handler is a coroutine
 * that runs per-envelope: read the request, dispatch, emit response.
 *
 * `dcs_code` must match what the client transport was created with.
 * `compressed`: matches the wire_statemachine `start_write` flag —
 * 0 = b64-only response, 1 = b64+lz4. Should mirror what the client
 * sends so envelope sizes stay balanced.
 *
 * `emit` is borrowed; `user` is the opaque pointer passed back to it
 * on every response. Caller owns both lifetimes — they must outlive
 * the wire_statemachine.
 *
 * Returns the server state handle. The caller owns it and must release
 * it with yetty_yclass_rpc_dcs_server_destroy AFTER destroying the
 * wire_statemachine (the SM's registered handler points at it). */
struct yetty_yclass_rpc_dcs_server_ptr_result yetty_yclass_rpc_dcs_server_attach(
    struct yetty_ywire_wire_statemachine *sm, int dcs_code, int compressed,
    yetty_yclass_rpc_dcs_emit_fn emit, void *user);

/* Free the server state minted by attach. NULL-safe. Only call once the
 * wire_statemachine it was attached to is destroyed. */
void yetty_yclass_rpc_dcs_server_destroy(struct yetty_yclass_rpc_dcs_server *server);

#ifdef __cplusplus
}
#endif

#endif /* YCLASS_RPC_DCS_SERVER_H */
