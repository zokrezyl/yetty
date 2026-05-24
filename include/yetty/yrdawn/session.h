/*
 * yetty_yrdawn_session — server-side state for one yrdawn client.
 *
 * One client process == one session_id (client-chosen at HELLO time)
 * == one struct yetty_yrdawn_session on the server. The session owns
 * everything that's *shared across canvases the same client opens*:
 *
 *   - the WGPU handle table (instance / adapter / device / buffers /
 *     pipelines / ...);
 *   - BULK reassembly slots (texture/buffer uploads target a session-
 *     wide ref namespace; the codegen takes the assembled bytes and
 *     hands them to the appropriate WGPU* method);
 *   - the borrowed shared Dawn handles pulled from yframework's GPU
 *     context (every session shares the same Dawn instance/device —
 *     a second in-process instance deadlocks Vulkan during pipeline
 *     creation);
 *   - the list of figures bound to this session.
 *
 * Sessions are lazily created when the factory sees a SUB_HELLO with a
 * previously unseen session_id and torn down when the last figure
 * bound to them is removed.
 *
 * The session is INVISIBLE to the codegen dispatcher: server-dispatch
 * callbacks (yrdawn_server_handle_*, get_shared_*, bulk_take,
 * set_frame, emit_reply) receive `ctx = figure`. Each callback
 * navigates `figure->session` for shared state and uses the figure
 * itself for per-canvas state (presentation texture, outbound emit
 * tagging). Keeping ctx=figure means the 5 MB of generated
 * server-dispatch code requires zero changes for the multi-canvas
 * refactor.
 */
#ifndef YETTY_YRDAWN_SESSION_H
#define YETTY_YRDAWN_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include <webgpu/webgpu.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_context;
struct yetty_yrdawn_figure;
struct yetty_yrdawn_session;

YETTY_YRESULT_DECLARE(yetty_yrdawn_session_ptr, struct yetty_yrdawn_session *);

/* Construct a session for `session_id`. Borrows the shared Dawn
 * handles from `context->runtime->gpu`. Sessions are owned by the
 * yrdawn factory args' session table; callers don't destroy directly. */
struct yetty_yrdawn_session_ptr_result yetty_yrdawn_session_create(
    uint32_t session_id, const struct yetty_context *context);

/* Destroy. Releases the handle table contents (calls wgpu*Release for
 * each kind), frees BULK reassembly buffers, frees the figure list
 * (does NOT destroy the figures — they belong to the figure tree). */
struct yetty_ycore_void_result yetty_yrdawn_session_destroy(struct yetty_yrdawn_session *session);

uint32_t yetty_yrdawn_session_id(const struct yetty_yrdawn_session *session);

/* Bind / unbind a figure to this session. The session refcounts figures
 * so the factory args' session table can drop the entry when the last
 * figure unbinds. */
void yetty_yrdawn_session_attach_figure(struct yetty_yrdawn_session *session,
                                        struct yetty_yrdawn_figure *figure);
void yetty_yrdawn_session_detach_figure(struct yetty_yrdawn_session *session,
                                        struct yetty_yrdawn_figure *figure);
size_t yetty_yrdawn_session_figure_count(const struct yetty_yrdawn_session *session);

/* Handle table — opaque u64 → void * mapping shared across every
 * figure bound to the session. The codegen dispatcher reaches these
 * via the server-side callbacks defined in <yetty/yrdawn/server.h>;
 * those callbacks navigate from `figure → figure->session` and call
 * the helpers here. */
void *yetty_yrdawn_session_handle_get(struct yetty_yrdawn_session *session, uint64_t handle);
struct yetty_ycore_void_result yetty_yrdawn_session_handle_set(struct yetty_yrdawn_session *session,
                                                               uint64_t handle, void *ptr);
void yetty_yrdawn_session_handle_release(struct yetty_yrdawn_session *session, uint64_t handle);

/* Shared Dawn objects borrowed from yframework. Cast-thru getters so
 * the codegen wrappers stay opaque. */
WGPUInstance yetty_yrdawn_session_shared_instance(struct yetty_yrdawn_session *session);
WGPUAdapter yetty_yrdawn_session_shared_adapter(struct yetty_yrdawn_session *session);
WGPUDevice yetty_yrdawn_session_shared_device(struct yetty_yrdawn_session *session);
WGPUQueue yetty_yrdawn_session_shared_queue(struct yetty_yrdawn_session *session);

/* BULK reassembly. The reference namespace is session-wide so two
 * canvases can interleave uploads without colliding ref ids.
 * bulk_append accumulates a chunk; bulk_take hands ownership of the
 * fully-assembled buffer to the caller (which then frees with
 * yetty_ycore_buffer_destroy). */
struct yetty_ycore_void_result yetty_yrdawn_session_bulk_append(
    struct yetty_yrdawn_session *session, uint32_t ref, uint32_t seq, const uint8_t *chunk,
    size_t chunk_size, int last);
int yetty_yrdawn_session_bulk_take(struct yetty_yrdawn_session *session, uint32_t ref,
                                   struct yetty_ycore_buffer *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRDAWN_SESSION_H */
