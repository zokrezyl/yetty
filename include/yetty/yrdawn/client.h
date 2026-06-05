/*
 * yrdawn/client.h — pure-C side of the WebGPU-over-OSC bridge.
 *
 * Used by remote wasm processes (or POSIX demos) that want to render
 * inside a yetty terminal as if Dawn lived in their process. One
 * `client` owns the PTY connection; one `canvas` represents one remote
 * yfigure in the terminal's root container. A client may own as many
 * canvases as the host accepts.
 *
 * Wire shape (matches the figure-tree contract used by ymgui):
 *   - Each canvas is announced via a CREATE_CHILD admin record sent
 *     on YETTY_DCS_YCOMPOSITOR_BIN (handled in client.c). The record's
 *     `init_payload` carries `{u32 SUB_HELLO} +
 *     struct yetty_yrdawn_wire_hello{session_id, figure_id}`.
 *   - Subsequent wgpu* CMDs, BULK uploads, and BYEs go out as
 *     id-targeted records on the same OSC code. Bodies start with a
 *     u32 SUB_OP discriminator from `enum yetty_yrdawn_figure_sub_op`.
 *
 * The codegen client stubs (yrdawn_client_wgpu*) take a client pointer
 * (not a canvas) — they emit through the client's "current canvas"
 * marker. canvas_create() sets the new canvas as current; switch by
 * calling yetty_yrdawn_canvas_make_current(canvas). This keeps the
 * 5 MB of generated code untouched and matches WebGPU's own model
 * where instance / adapter / device / buffer / pipeline calls are
 * device-scoped and only present-time calls are surface-scoped.
 *
 * Inbound: every SC OSC envelope carries a `figure_id` so the pump
 * demuxes to the right canvas's per-canvas callbacks (reply / event /
 * key / resize).
 */
#ifndef YETTY_YRDAWN_CLIENT_H
#define YETTY_YRDAWN_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yrdawn_client;
struct yetty_yrdawn_canvas;

YETTY_YRESULT_DECLARE(yetty_yrdawn_client_ptr, struct yetty_yrdawn_client *);
YETTY_YRESULT_DECLARE(yetty_yrdawn_canvas_ptr, struct yetty_yrdawn_canvas *);

/* Per-canvas callbacks. Each fires on the canvas the event is
 * addressed to (figure_id-tagged on the wire). */
typedef void (*yetty_yrdawn_reply_cb)(void *user, uint32_t status, uint32_t method_id,
                                      const uint8_t *body, size_t body_len);
typedef void (*yetty_yrdawn_event_cb)(void *user, uint32_t kind, uint64_t device_handle,
                                      const uint8_t *body, size_t body_len);
typedef void (*yetty_yrdawn_input_key_cb)(void *user, uint32_t kind, int32_t key, int32_t mods,
                                          uint32_t codepoint);
typedef void (*yetty_yrdawn_input_resize_cb)(void *user, float width, float height);

/* Raw-byte input callback. Fires for bytes on the input stream that are
 * NOT part of a yface/figure envelope — i.e. ordinary keystrokes the host
 * terminal forwards to the PTY when no figure has keyboard focus (a typed
 * 'q', a Ctrl-C delivered as ETX 0x03, etc.). This is the plain-tty input
 * path; figure-focused key events arrive via the per-canvas key callback
 * instead. `bytes` is valid only for the duration of the call. */
typedef void (*yetty_yrdawn_raw_input_cb)(void *user, const char *bytes, size_t len);

/* Construct a client bound to (in_fd, out_fd). For the typical wasm
 * deployment, in_fd = STDIN_FILENO and out_fd = STDOUT_FILENO. The
 * session_id is client-chosen, must be non-zero, and is stamped into
 * every canvas's HELLO. A common choice is `(uint32_t)getpid()`. */
struct yetty_yrdawn_client_ptr_result yetty_yrdawn_client_create(int in_fd, int out_fd,
                                                                 uint32_t session_id);

struct yetty_ycore_void_result yetty_yrdawn_client_destroy(struct yetty_yrdawn_client *c);

uint32_t yetty_yrdawn_client_session_id(const struct yetty_yrdawn_client *c);

/* Register a handler for raw (non-envelope) input bytes — see
 * yetty_yrdawn_raw_input_cb. One per client; pass NULL to clear. Fires
 * from yetty_yrdawn_client_pump as the input stream is drained. */
void yetty_yrdawn_client_set_raw_input_cb(struct yetty_yrdawn_client *c,
                                          yetty_yrdawn_raw_input_cb cb, void *user);

/* Open one canvas at the given pane rect (host-pixel space). Emits the
 * CREATE_CHILD admin record and SUB_HELLO; the canvas becomes the
 * client's "current" canvas automatically so the codegen wgpu*
 * stubs that immediately follow target it. */
struct yetty_yrdawn_canvas_ptr_result yetty_yrdawn_canvas_create(struct yetty_yrdawn_client *c,
                                                                 uint32_t figure_id, float x,
                                                                 float y, float w, float h);

/* Send DELETE_CHILD + free the canvas struct. Safe with NULL. */
struct yetty_ycore_void_result yetty_yrdawn_canvas_destroy(struct yetty_yrdawn_canvas *canvas);

/* Make `canvas` the current canvas — all wgpu* CMDs the codegen stubs
 * subsequently emit ride records tagged with this canvas's figure_id.
 * Returns the previously-current canvas (may be NULL). */
struct yetty_yrdawn_canvas *yetty_yrdawn_canvas_make_current(struct yetty_yrdawn_canvas *canvas);

uint32_t yetty_yrdawn_canvas_figure_id(const struct yetty_yrdawn_canvas *canvas);
struct yetty_yrdawn_client *yetty_yrdawn_canvas_client(struct yetty_yrdawn_canvas *canvas);

/* Per-canvas callbacks. event_cb fires per WGPUDevice event with the
 * canvas the event was addressed to (the server sets figure_id on
 * EVENT envelopes from canvas-affinitised events; multi-canvas
 * clients use that to find the right canvas). The key / resize
 * callbacks fire when input is delivered to the focused / resized
 * canvas. */
void yetty_yrdawn_canvas_set_event_cb(struct yetty_yrdawn_canvas *canvas, yetty_yrdawn_event_cb cb,
                                      void *user);
void yetty_yrdawn_canvas_set_input_key_cb(struct yetty_yrdawn_canvas *canvas,
                                          yetty_yrdawn_input_key_cb cb, void *user);
void yetty_yrdawn_canvas_set_input_resize_cb(struct yetty_yrdawn_canvas *canvas,
                                             yetty_yrdawn_input_resize_cb cb, void *user);

/* True once the server has acknowledged this canvas's HELLO. Canvases
 * that aren't yet connected silently queue CMDs (the server rejects
 * them with VALIDATION_ERROR) — callers should pump until connected
 * before issuing real work. */
int yetty_yrdawn_canvas_connected(const struct yetty_yrdawn_canvas *canvas);

/* Monotonic u64 handle allocator. Shared across every canvas of the
 * client (matches the server's per-session handle table). Never
 * returns YETTY_YRDAWN_HANDLE_NULL. */
uint64_t yetty_yrdawn_client_alloc_handle(struct yetty_yrdawn_client *c);

/* Send a CMD without a callback. The codegen wgpu* stubs use this.
 * The wire envelope is tagged with the client's CURRENT canvas. */
struct yetty_ycore_void_result yetty_yrdawn_client_send_cmd_sync(struct yetty_yrdawn_client *c,
                                                                 uint32_t method_id,
                                                                 const void *body, size_t body_len);

/* Send a CMD with a callback. cb fires from a later pump() call. */
struct yetty_ycore_void_result yetty_yrdawn_client_send_cmd_async(struct yetty_yrdawn_client *c,
                                                                  uint32_t method_id,
                                                                  const void *body, size_t body_len,
                                                                  yetty_yrdawn_reply_cb cb,
                                                                  void *user);

/* Send a CMD with a fixed-size return value, pump until the matching
 * REPLY lands. */
struct yetty_ycore_void_result yetty_yrdawn_client_send_cmd_blocking(
    struct yetty_yrdawn_client *c, uint32_t method_id, const void *body, size_t body_len, void *out,
    size_t out_size, uint32_t *out_status);

/* Variable-length variant; *out_buf is malloc'd, caller frees. */
struct yetty_ycore_void_result yetty_yrdawn_client_send_cmd_blocking_dyn(
    struct yetty_yrdawn_client *c, uint32_t method_id, const void *body, size_t body_len,
    uint8_t **out_buf, size_t *out_len, uint32_t *out_status);

/* Emit a complete BULK payload as one or more chunk frames. */
struct yetty_ycore_void_result yetty_yrdawn_client_send_bulk(struct yetty_yrdawn_client *c,
                                                             uint32_t ref, const void *bytes,
                                                             size_t len);

/* Ship a single RGBA8 frame to the current canvas. Pixels travel over
 * BULK; a follow-up CMD references them via method_id 100
 * (yetty_yrdawn_present_frame). Caller-owned bytes. */
struct yetty_ycore_void_result yetty_yrdawn_canvas_present_frame(struct yetty_yrdawn_canvas *canvas,
                                                                 uint32_t width, uint32_t height,
                                                                 const void *pixels, size_t bytes);

/* Pump inbound. Reads available bytes from in_fd, parses SC OSC
 * envelopes, demuxes by figure_id, and dispatches per-canvas
 * callbacks. Returns when in_fd has no pending bytes. */
struct yetty_ycore_void_result yetty_yrdawn_client_pump(struct yetty_yrdawn_client *c);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRDAWN_CLIENT_H */
