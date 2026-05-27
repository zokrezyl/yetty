/* Tiny RPC runtime.
 *
 * Wire request:
 *   u32 header     — bits 31:28 = enum rpc_op, bits 27:0 = id
 *                    (id is the slot index for RPC_OP_CALL, 0 otherwise)
 *   u32 body_len
 *   u8  body[body_len]
 *
 * Wire response:
 *   u32 resp_len
 *   u8  resp[resp_len]
 *
 * Admin and call live in different op-spaces — the slot id never collides
 * with a sentinel. */

#ifndef POC_RPC_H
#define POC_RPC_H

#include "class.h"

#include <stddef.h>
#include <stdint.h>

struct rpc_session;

/* Wire-bytes → typed-call bridge for one slot. Generator emits one body
 * per method; the RPC layer owns the slot→skel table. The class layer
 * is unaware of this type. */
typedef size_t (*rpc_skel_fn)(const void *body, size_t body_len,
                              void *resp, size_t resp_max);

enum rpc_op {
    RPC_OP_CALL = 0,        /* id = slot index; body = packed args */
    RPC_OP_RESOLVE_SLOT,    /* body = slot name; resp = u32 server slot id */
    RPC_OP_GET_CLASS,       /* body = class name; resp = (u16 nl, name, u32 id)* */
    RPC_OP_CREATE,          /* body = class name; resp = u64 handle */
};

#define RPC_OP_SHIFT        28
#define RPC_OP_MASK         0xFu
#define RPC_ID_MASK         0x0FFFFFFFu
#define RPC_HDR_MAKE(op, id) (((uint32_t)(op) << RPC_OP_SHIFT) | ((id) & RPC_ID_MASK))
#define RPC_HDR_OP(h)        ((enum rpc_op)(((h) >> RPC_OP_SHIFT) & RPC_OP_MASK))
#define RPC_HDR_ID(h)        ((h) & RPC_ID_MASK)

/* ---- Server side -------------------------------------------------- */

void rpc_init(void);
uint64_t rpc_register_object(void *obj);
void *rpc_handle_resolve(uint64_t handle);
void rpc_server_run(int fd);

/* Skel lookup hook. Each module's generated rpc.gen.c adds its own
 * lookup at startup via a constructor; rpc_skel_for walks the chain
 * on cache miss. */
typedef rpc_skel_fn (*skel_lookup_fn)(method_slot slot);
void rpc_add_skel_lookup(skel_lookup_fn fn);

/* Server's CALL dispatch resolves skels via this. Cached after first
 * lookup per slot. */
rpc_skel_fn rpc_skel_for(method_slot slot);

/* ---- Client side -------------------------------------------------- */

struct rpc_session *rpc_session_create(int fd);
void rpc_session_destroy(struct rpc_session *s);

/* Generic call. Packs (op, id) into the wire header. */
size_t rpc_call(struct rpc_session *s, enum rpc_op op, uint32_t id,
                const void *body, size_t body_len,
                void *resp, size_t resp_max);

/* T2 translation table — indexed by local slot. UINT32_MAX = unresolved
 * (no valid wire id can be ≥ 2^28 since the wire reserves the top 4
 * bits for the op). */
#define RPC_REMOTE_ID_UNRESOLVED UINT32_MAX

uint32_t rpc_session_remote_id(struct rpc_session *s, method_slot local_slot);
void rpc_session_set_remote_id(struct rpc_session *s, method_slot local_slot,
                               uint32_t remote_id);

/* Batched per-class trigger. Sends GET_CLASS, parses entries, populates
 * the session xlat for every slot of `class_name` the client knows.
 * Idempotent per (session, class_name). */
int rpc_session_translate_class(struct rpc_session *s, const char *class_name);

/* Lazy per-slot fallback. Returns the remote_id, doing a single
 * RESOLVE_SLOT round-trip if the xlat entry is still 0. */
uint32_t rpc_session_ensure_remote_id(struct rpc_session *s, method_slot local_slot);

#endif
