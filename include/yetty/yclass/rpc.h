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
 * The CALL response carries a one-byte status (0 = OK, 1 = ERR). On
 * success the value bytes follow; on error the generated skel appends
 * the impl's whole Result error — serialized by
 * yetty_ycore_error_serialize: every frame's msg, file, func, line
 * across the entire cause chain. The client stub receives that
 * variable-length blob (via yetty_yclass_rpc_call_alloc),
 * reconstructs the chain with yetty_ycore_error_deserialize, and
 * returns it as the `.cause` of a locally-raised
 * `"<slot>: remote impl returned error"` head — so
 * yetty_ycore_error_print on the caller shows the remote frames
 * exactly as a local error would. The error path is thus symmetric
 * with the value path.
 *
 * The skel ALSO logs the error to the SERVER stderr / ytrace before
 * sending it, so a remote failure is visible on both ends. If the
 * serialized chain does not fit the response buffer the skel falls
 * back to a status-only reply and the client surfaces just the
 * generic head (a debugging-context loss, never a correctness one). */

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
    YETTY_YCLASS_RPC_OP_GET_ROOT,     /* no body; resp = u64 handle of the server's root object
                                       * (0 if none set). Lets a remote producer obtain a proxy to
                                       * the host's pre-existing root object — e.g. a terminal's
                                       * yfigure root container — which RPC_OP_CREATE (always a new
                                       * object) cannot. */
    YETTY_YCLASS_RPC_OP_CALL_ONEWAY,  /* id = slot index; body = packed args. Like OP_CALL, but the
                                       * server dispatches WITHOUT writing a response and the client
                                       * reads none — fire-and-forget for void methods flagged
                                       * `oneway@`. Preserves the legacy one-way wire's semantics
                                       * (no per-call error feedback) so an interactive producer
                                       * never blocks its input loop waiting on a reply. */
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

/* Designate `obj` as this server's root object: registers it (minting a
 * handle) and records that handle as the one RPC_OP_GET_ROOT returns. A
 * host (e.g. a terminal) calls this once on its root container so remote
 * producers can obtain a proxy to it rather than each minting a detached
 * container. Returns the minted handle. */
struct yetty_yclass_handle_result yetty_yclass_rpc_set_root(void *obj);

/* Serve requests from `transport` until the peer disconnects (recv
 * returns 0). The transport is borrowed; caller retains ownership.
 * Returns OK on clean disconnect, ERR on transport / dispatch failure
 * that ended the loop. */
struct yetty_ycore_void_result yetty_yclass_rpc_server_run(
    struct yetty_yclass_transport *transport);

/* Skel lookup hook. Each module's generated rpc.gen.c adds its own
 * lookup at startup via a constructor; yetty_yclass_rpc_skel_for walks
 * the chain on cache miss. */
typedef yetty_yclass_rpc_skel_fn (*yetty_yclass_rpc_skel_lookup_fn)(yetty_yclass_method_slot slot);
struct yetty_ycore_void_result yetty_yclass_rpc_add_skel_lookup(yetty_yclass_rpc_skel_lookup_fn fn);

/* Server's CALL dispatch resolves skels via this. Cached after first
 * lookup per slot. Errors: METHOD_SLOT_UNDEFINED, no skel registered
 * for this slot, lookup-chain hook failure. */
struct yetty_yclass_rpc_skel_fn_result yetty_yclass_rpc_skel_for(yetty_yclass_method_slot slot);

/* Dispatch one already-assembled yrpc request frame. Used internally
 * by `yetty_yclass_rpc_server_run` and by alternative server
 * transports that assemble request frames themselves (e.g. the DCS
 * envelope-driven server attached to a wire_statemachine on a PTY).
 *
 * `header` is the packed (op | id) word the client sent; `body` /
 * `body_len` are the request payload; `resp` is a caller-owned buffer
 * of size `resp_max` that receives the response payload. The success
 * value is the response length in bytes. On dispatch error the Result
 * carries the cause chain; callers typically send a zero-length
 * response so the client maps it to "remote impl returned error". */
struct yetty_ycore_size_result yetty_yclass_rpc_dispatch_one(uint32_t header, const void *body,
                                                             size_t body_len, void *resp,
                                                             size_t resp_max);

/* ---- Client side -------------------------------------------------- */

/* The session takes ownership of `transport` and destroys it on
 * session destroy. Errors: NULL transport, alloc failure. */
struct yetty_yclass_rpc_session_ptr_result yetty_yclass_rpc_session_create(
    struct yetty_yclass_transport *transport);
struct yetty_ycore_void_result yetty_yclass_rpc_session_destroy(struct yetty_yclass_rpc_session *s);

/* Generic call. Packs (op, id) into the wire header. The successful
 * value is the response payload length in bytes. The response is read
 * into the caller-owned `resp` buffer; a response larger than
 * `resp_max` is drained and rejected — use yetty_yclass_rpc_call_alloc
 * when the response length is not known up front. */
struct yetty_ycore_size_result yetty_yclass_rpc_call(struct yetty_yclass_rpc_session *s,
                                                     enum yetty_yclass_rpc_op op, uint32_t id,
                                                     const void *body, size_t body_len, void *resp,
                                                     size_t resp_max);

/* Like yetty_yclass_rpc_call, but heap-allocates the response to fit whatever
 * the peer returns — needed for CALL responses whose error path carries a
 * variable-length serialized cause chain that will not fit a caller-sized
 * buffer. On success `*resp_out` is a malloc'd buffer of `*resp_len_out` bytes
 * that the CALLER frees; an empty (zero-length) response yields
 * `*resp_out = NULL`, `*resp_len_out = 0`. The length is bounded by the wire
 * BUF_MAX limit. */
struct yetty_ycore_void_result yetty_yclass_rpc_call_alloc(struct yetty_yclass_rpc_session *s,
                                                           enum yetty_yclass_rpc_op op, uint32_t id,
                                                           const void *body, size_t body_len,
                                                           uint8_t **resp_out,
                                                           size_t *resp_len_out);

/* Fire-and-forget CALL (YETTY_YCLASS_RPC_OP_CALL_ONEWAY): marshal the args and
 * return without awaiting a response. The server dispatches the method (side
 * effects apply) but writes no reply, so the caller cannot observe a remote
 * error — matching the legacy one-way figure wire. Only valid for void slots
 * (codegen emits this path for `oneway@`-flagged methods). Flushes the
 * transport so buffered transports (DCS) actually put the frame on the wire,
 * since no recv() follows to trigger their lazy flush. */
struct yetty_ycore_void_result yetty_yclass_rpc_call_oneway(struct yetty_yclass_rpc_session *s,
                                                            uint32_t id, const void *body,
                                                            size_t body_len);

/* T2 translation table — indexed by local slot. UINT32_MAX = unresolved
 * (no valid wire id can be ≥ 2^28 since the wire reserves the top 4
 * bits for the op). */
#define YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED UINT32_MAX

/* Look up the cached remote id for `local_slot`. Errors: not cached
 * (caller falls back to ensure_remote_id), NULL session. */
struct uint32_result yetty_yclass_rpc_session_remote_id(struct yetty_yclass_rpc_session *s,
                                                        yetty_yclass_method_slot local_slot);
struct yetty_ycore_void_result yetty_yclass_rpc_session_set_remote_id(
    struct yetty_yclass_rpc_session *s, yetty_yclass_method_slot local_slot, uint32_t remote_id);

/* Batched per-class trigger. Sends GET_CLASS, parses entries, populates
 * the session xlat for every slot of `class_name` the client knows.
 * Idempotent per (session, class_name). */
struct yetty_ycore_void_result yetty_yclass_rpc_session_translate_class(
    struct yetty_yclass_rpc_session *s, const char *class_name);

/* Lazy per-slot fallback. Returns the remote_id, doing a single
 * RESOLVE_SLOT round-trip if the xlat entry is missing. */
struct uint32_result yetty_yclass_rpc_session_ensure_remote_id(struct yetty_yclass_rpc_session *s,
                                                               yetty_yclass_method_slot local_slot);

/* Client: fetch the server's root object handle (RPC_OP_GET_ROOT). 0 if the
 * server set no root. Pair with yetty_yclass_object_proxy_create to wrap it. */
struct yetty_yclass_handle_result yetty_yclass_rpc_session_get_root(
    struct yetty_yclass_rpc_session *s);

#endif /* YCLASS_RPC_H */
