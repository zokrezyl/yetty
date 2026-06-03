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
 * collides with a sentinel.
 *
 * Error preservation policy
 * -------------------------
 * On RPC failure, the generated skel logs the impl's Result error
 * (file:line:func + msg + cause chain) via yetty_ycore_error_print to
 * the SERVER stderr / ytrace, then encodes only a one-byte status=1
 * in the wire response. The client stub maps that to a generic
 * `"<slot>: remote impl returned error"` Result.
 *
 * The structured chain is NOT carried over the wire because
 * yetty_ycore_error.msg must point to string-literal-lifetime
 * memory (per <yetty/ycore/result.h>); reconstructing a chain from
 * a runtime byte buffer would require a separate ownership model.
 *
 * Practical impact: when debugging a remote failure, correlate the
 * client's "remote impl returned error" line with the server's
 * yetty_ycore_error_print output (matching slot name + same ytrace
 * stream when the server runs under YTRACE_DEFAULT_ON=yes). */

#ifndef YCLASS_RPC_H
#define YCLASS_RPC_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/transport.h>

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/types.h> /* uint32_result */

struct yetty_yclass_rpc_session;

/* --- Result types for yclass-specific values ----------------------- */
YETTY_YRESULT_DECLARE(yetty_yclass_rpc_session_ptr, struct yetty_yclass_rpc_session *);
YETTY_YRESULT_DECLARE(yetty_yclass_handle, uint64_t); /* server-minted object id */
/* yetty_yclass_void_ptr_result is declared in <yetty/yclass/class.h>. */

/* Wire-bytes → typed-call bridge for one slot. Generator emits one body
 * per method; the RPC layer owns the slot→skel table. The class layer
 * is unaware of this type. */
typedef size_t (*yetty_yclass_rpc_skel_fn)(const void *body, size_t body_len, void *resp,
                                           size_t resp_max);

YETTY_YRESULT_DECLARE(yetty_yclass_rpc_skel_fn, yetty_yclass_rpc_skel_fn);

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
/*
 * Single-server limitation
 * ------------------------
 * The server state (object handle table, skel cache, skel-lookup
 * chain) is process-global; yetty_yclass_rpc_server_run() also uses
 * static body/resp scratch buffers. Running multiple concurrent
 * yclass servers in the same process is NOT supported:
 *
 *   - Object handles minted via rpc_register_object are drawn from a
 *     single global counter — two servers would mint colliding ids.
 *   - The skel cache is global — once cached for slot S, a CALL on
 *     either server resolves to the SAME skel.
 *   - The body/resp static buffers in rpc_server_run are shared —
 *     concurrent calls on two transports would corrupt frames.
 *
 * If multi-server support is needed, the state belongs on a server-
 * instance struct passed into rpc_server_run / register_object /
 * handle_resolve / skel_for. For now: one server per process.
 */

struct yetty_ycore_void_result yetty_yclass_rpc_init(void);

/* Register a user object and mint a new handle for it. Errors: table
 * full (MAX_OBJECTS), object_count overflow guards. */
struct yetty_yclass_handle_result yetty_yclass_rpc_register_object(void *obj);

/* Resolve a previously-minted handle to the user object. Errors:
 * handle=0 (invalid), handle not registered. */
struct yetty_yclass_void_ptr_result yetty_yclass_rpc_handle_resolve(uint64_t handle);

/* Serve requests from `transport` until the peer disconnects (recv
 * returns 0). The transport is borrowed; caller retains ownership.
 * Returns OK on clean disconnect, ERR on transport / dispatch failure
 * that ended the loop. */
struct yetty_ycore_void_result
yetty_yclass_rpc_server_run(struct yetty_yclass_transport *transport);

/* Skel lookup hook. Each module's generated rpc.gen.c adds its own
 * lookup at startup via a constructor; yetty_yclass_rpc_skel_for walks
 * the chain on cache miss. */
typedef yetty_yclass_rpc_skel_fn (*yetty_yclass_rpc_skel_lookup_fn)(yetty_yclass_method_slot slot);
struct yetty_ycore_void_result
yetty_yclass_rpc_add_skel_lookup(yetty_yclass_rpc_skel_lookup_fn fn);

/* Server's CALL dispatch resolves skels via this. Cached after first
 * lookup per slot. Errors: METHOD_SLOT_UNDEFINED, no skel registered
 * for this slot, lookup-chain hook failure. */
struct yetty_yclass_rpc_skel_fn_result
yetty_yclass_rpc_skel_for(yetty_yclass_method_slot slot);

/* Dispatch one already-assembled yrpc request frame. Used internally
 * by `yetty_yclass_rpc_server_run` and by alternative server
 * transports that assemble request frames themselves (e.g. the DCS
 * envelope-driven server attached to a wire_statemachine on a PTY).
 *
 * `header` is the packed (op | id) word the client sent; `body` /
 * `body_len` are the request payload; `resp` is a caller-owned buffer
 * of size `resp_max` that receives the response payload. Returns the
 * response length in bytes (0 on dispatch error — the error is logged
 * via ytrace, callers send a zero-length response so the client maps
 * it to "remote impl returned error"). */
size_t yetty_yclass_rpc_dispatch_one(uint32_t header, const void *body, size_t body_len,
                                     void *resp, size_t resp_max);

/* ---- Client side -------------------------------------------------- */

/* The session takes ownership of `transport` and destroys it on
 * session destroy. Errors: NULL transport, alloc failure. */
struct yetty_yclass_rpc_session_ptr_result
yetty_yclass_rpc_session_create(struct yetty_yclass_transport *transport);
struct yetty_ycore_void_result
yetty_yclass_rpc_session_destroy(struct yetty_yclass_rpc_session *s);

/* Generic call. Packs (op, id) into the wire header. The successful
 * value is the response payload length in bytes. */
struct yetty_ycore_size_result
yetty_yclass_rpc_call(struct yetty_yclass_rpc_session *s, enum yetty_yclass_rpc_op op,
                      uint32_t id, const void *body, size_t body_len, void *resp,
                      size_t resp_max);

/* T2 translation table — indexed by local slot. UINT32_MAX = unresolved
 * (no valid wire id can be ≥ 2^28 since the wire reserves the top 4
 * bits for the op). */
#define YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED UINT32_MAX

/* Look up the cached remote id for `local_slot`. Errors: not cached
 * (caller falls back to ensure_remote_id), NULL session. */
struct uint32_result yetty_yclass_rpc_session_remote_id(struct yetty_yclass_rpc_session *s,
                                                        yetty_yclass_method_slot local_slot);
struct yetty_ycore_void_result
yetty_yclass_rpc_session_set_remote_id(struct yetty_yclass_rpc_session *s,
                                       yetty_yclass_method_slot local_slot,
                                       uint32_t remote_id);

/* Batched per-class trigger. Sends GET_CLASS, parses entries, populates
 * the session xlat for every slot of `class_name` the client knows.
 * Idempotent per (session, class_name). */
struct yetty_ycore_void_result
yetty_yclass_rpc_session_translate_class(struct yetty_yclass_rpc_session *s,
                                         const char *class_name);

/* Lazy per-slot fallback. Returns the remote_id, doing a single
 * RESOLVE_SLOT round-trip if the xlat entry is missing. */
struct uint32_result
yetty_yclass_rpc_session_ensure_remote_id(struct yetty_yclass_rpc_session *s,
                                          yetty_yclass_method_slot local_slot);

#endif /* YCLASS_RPC_H */
