/* yclass RPC runtime.
 *
 * Wire request:
 *   u32 header     — bits 31:28 = enum yetty_yclass_rpc_op, bits 27:0 = id
 *                    (id is the slot index for YETTY_YCLASS_RPC_OP_CALL,
 *                    0 otherwise)
 *   u32 body_len
 *   u8  body[body_len]
 *
 * Wire response:
 *   u32 resp_len
 *   u8  resp[resp_len]
 *
 * Admin and call live in different op-spaces — the slot id never
 * collides with a sentinel. */

#ifndef YCLASS_RPC_H
#define YCLASS_RPC_H

#include <yclass/class.h>
#include <yclass/transport.h>

#include <stddef.h>
#include <stdint.h>

struct yetty_yclass_rpc_session;

/* Wire-bytes → typed-call bridge for one slot. Generator emits one body
 * per method; the RPC layer owns the slot→skel table. The class layer
 * is unaware of this type. */
typedef size_t (*yetty_yclass_rpc_skel_fn)(const void *body, size_t body_len, void *resp,
                                           size_t resp_max);

enum yetty_yclass_rpc_op {
    YETTY_YCLASS_RPC_OP_CALL = 0,     /* id = slot index; body = packed args */
    YETTY_YCLASS_RPC_OP_RESOLVE_SLOT, /* body = slot name; resp = u32 server slot id */
    YETTY_YCLASS_RPC_OP_GET_CLASS,    /* body = class name; resp = (u16 nl, name, u32 id)* */
    YETTY_YCLASS_RPC_OP_CREATE,       /* body = class name; resp = u64 handle */
};

#define YETTY_YCLASS_RPC_OP_SHIFT 28
#define YETTY_YCLASS_RPC_OP_MASK 0xFu
#define YETTY_YCLASS_RPC_ID_MASK 0x0FFFFFFFu
#define YETTY_YCLASS_RPC_HDR_MAKE(op, id)                                                          \
    (((uint32_t)(op) << YETTY_YCLASS_RPC_OP_SHIFT) | ((id) & YETTY_YCLASS_RPC_ID_MASK))
#define YETTY_YCLASS_RPC_HDR_OP(h)                                                                 \
    ((enum yetty_yclass_rpc_op)(((h) >> YETTY_YCLASS_RPC_OP_SHIFT) & YETTY_YCLASS_RPC_OP_MASK))
#define YETTY_YCLASS_RPC_HDR_ID(h) ((h) & YETTY_YCLASS_RPC_ID_MASK)

/* ---- Server side -------------------------------------------------- */

void yetty_yclass_rpc_init(void);
uint64_t yetty_yclass_rpc_register_object(void *obj);
void *yetty_yclass_rpc_handle_resolve(uint64_t handle);

/* Serve requests from `transport` until the peer disconnects (recv
 * returns 0). The transport is borrowed; caller retains ownership. */
void yetty_yclass_rpc_server_run(struct yetty_yclass_transport *transport);

/* Skel lookup hook. Each module's generated rpc.gen.c adds its own
 * lookup at startup via a constructor; yetty_yclass_rpc_skel_for walks
 * the chain on cache miss. */
typedef yetty_yclass_rpc_skel_fn (*yetty_yclass_rpc_skel_lookup_fn)(yetty_yclass_method_slot slot);
void yetty_yclass_rpc_add_skel_lookup(yetty_yclass_rpc_skel_lookup_fn fn);

/* Server's CALL dispatch resolves skels via this. Cached after first
 * lookup per slot. */
yetty_yclass_rpc_skel_fn yetty_yclass_rpc_skel_for(yetty_yclass_method_slot slot);

/* ---- Client side -------------------------------------------------- */

/* The session takes ownership of `transport` and destroys it on
 * session destroy. */
struct yetty_yclass_rpc_session *
yetty_yclass_rpc_session_create(struct yetty_yclass_transport *transport);
void yetty_yclass_rpc_session_destroy(struct yetty_yclass_rpc_session *s);

/* Generic call. Packs (op, id) into the wire header. */
size_t yetty_yclass_rpc_call(struct yetty_yclass_rpc_session *s, enum yetty_yclass_rpc_op op,
                             uint32_t id, const void *body, size_t body_len, void *resp,
                             size_t resp_max);

/* T2 translation table — indexed by local slot. UINT32_MAX = unresolved
 * (no valid wire id can be ≥ 2^28 since the wire reserves the top 4
 * bits for the op). */
#define YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED UINT32_MAX

uint32_t yetty_yclass_rpc_session_remote_id(struct yetty_yclass_rpc_session *s,
                                            yetty_yclass_method_slot local_slot);
void yetty_yclass_rpc_session_set_remote_id(struct yetty_yclass_rpc_session *s,
                                            yetty_yclass_method_slot local_slot,
                                            uint32_t remote_id);

/* Batched per-class trigger. Sends GET_CLASS, parses entries, populates
 * the session xlat for every slot of `class_name` the client knows.
 * Idempotent per (session, class_name). */
int yetty_yclass_rpc_session_translate_class(struct yetty_yclass_rpc_session *s,
                                             const char *class_name);

/* Lazy per-slot fallback. Returns the remote_id, doing a single
 * RESOLVE_SLOT round-trip if the xlat entry is still 0. */
uint32_t yetty_yclass_rpc_session_ensure_remote_id(struct yetty_yclass_rpc_session *s,
                                                   yetty_yclass_method_slot local_slot);

#endif /* YCLASS_RPC_H */
