/*
 * framework.h — ygui framework. Pure widget framework — knows nothing
 * about deployment.
 *
 * Contract:
 *   - Caller hands the framework a `yetty_platform_pty` to write OSC
 *     envelopes to. The far end is consumed by SOMEONE (in-process
 *     wire_statemachine + figure_container, host yetty over a real pty,
 *     a memory ring inside yui, …) — the framework neither knows nor
 *     cares.
 *   - Caller feeds input bytes via yetty_ygui_framework_feed_input
 *     (terminal-style byte stream: ASCII + CSI escape sequences).
 *     The framework runs its own decoder and dispatches to widgets.
 *     Caller's job is just to get bytes from wherever (real stdin, a
 *     KEY_DOWN→bytes encoder, etc.) into this call.
 *   - Caller decides WHEN to emit a frame by calling framework_emit.
 *
 * No fds. No libuv. No SIGWINCH. No event loop integration. Those are
 * deployment concerns the framework refuses to learn.
 */
#ifndef YETTY_YGUI_FRAMEWORK_H
#define YETTY_YGUI_FRAMEWORK_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_platform_pty;
struct yetty_ydraw_drawable_list;
struct yetty_ycore_buffer;
struct yetty_yclass_object;
struct yetty_yclass_rpc_session;
struct yetty_ygui_theme;
struct yetty_yconfig_config;
struct yetty_yclass_object;
struct yetty_ygui_framework;

YETTY_YRESULT_DECLARE(yetty_ygui_framework_ptr, struct yetty_ygui_framework *);

#ifdef __cplusplus
extern "C" {
#endif

/*-----------------------------------------------------------------------------
 * Lifecycle.
 *---------------------------------------------------------------------------*/

/* Create a framework. `output_pty` is BORROWED — caller owns its lifetime;
 * it may be NULL for an in-process host that wires a container object via
 * yetty_ygui_framework_set_container_obj. */
struct yetty_ygui_framework_ptr_result yetty_ygui_framework_create(
    struct yetty_platform_pty *output_pty);

/* Destroy the framework and its widget tree. Does NOT touch the
 * output_pty (borrowed). */
struct yetty_ycore_void_result yetty_ygui_framework_destroy(struct yetty_ygui_framework *framework);

/* Wire the framework to a receiver-side yfigure container through the
 * yclass slot dispatch path. framework_emit drives the figure tree by
 * calling the typed yfigure stubs (yetty_yfigure_create_child / _set_child_rect
 * / _apply_child_body / …) directly on `container`.
 *
 * `container` is borrowed — caller owns its lifetime and must keep it
 * alive until the framework is destroyed (or this setter is called
 * again with a different pointer / NULL).
 *
 * `session` is borrowed too. NULL means in-process dispatch (the slot
 * impl is invoked directly on the local container instance); non-NULL
 * means the slot stub marshals the call over yrpc, and the same code
 * path drives a remote yfigure tree on the far end of the session. */
struct yetty_ycore_void_result yetty_ygui_framework_set_container_obj(
    struct yetty_ygui_framework *framework, struct yetty_yclass_object *container);

struct yetty_ycore_void_result yetty_ygui_framework_set_session(
    struct yetty_ygui_framework *framework, struct yetty_yclass_rpc_session *session);

/* Attach an out-of-process framework to the hosting yetty's root figure
 * container over the yclass RPC transport (DCS YETTY_DCS_YCLASS_RPC). On
 * success the framework's emit path drives the host container with the typed
 * yclass stubs, marshalled over the session.
 *
 * `read_fd` is where RPC responses arrive (the tool's input from the
 * terminal); `write_fd` is where requests go (its output to the terminal).
 * A tool over a PTY passes STDIN_FILENO / STDOUT_FILENO. `compressed`: 0 =
 * base64 only (cheapest for tiny frames), 1 = base64 + lz4.
 *
 * The framework takes ownership of the producer session it opens and tears
 * it down in framework_destroy. The fds are borrowed — the caller keeps
 * ownership and closes them after the framework is destroyed. On failure
 * the framework is left untouched (container_obj stays NULL) and the error
 * is returned for the caller to log or ignore. */
struct yetty_ycore_void_result yetty_ygui_framework_attach(struct yetty_ygui_framework *framework,
                                                           int read_fd, int write_fd,
                                                           int compressed);

/* Run one full emit cycle: layout pass against the current viewport,
 * walk the widget tree twice (containers then bodies), concatenate
 * streams, wrap in a yface envelope, write through output_pty. */
struct yetty_ycore_void_result yetty_ygui_framework_emit(struct yetty_ygui_framework *framework);

/* Drop every remote figure this framework produced by clearing the host
 * container directly through the typed yclass stub (yetty_yfigure_clear_all
 * on container_obj). Call this at client-mode shutdown BEFORE destroy so the
 * host does not keep the client's last frame frozen on the pane. Requires
 * container_obj to be set (the framework was wired to a host container). */
struct yetty_ycore_void_result yetty_ygui_framework_clear(struct yetty_ygui_framework *framework);

/*-----------------------------------------------------------------------------
 * Input — caller pushes raw byte stream (ASCII + CSI escapes) here.
 * The framework decodes and dispatches to widgets / the key callback.
 *---------------------------------------------------------------------------*/
struct yetty_ycore_void_result yetty_ygui_framework_feed_input(
    struct yetty_ygui_framework *framework, const char *bytes, size_t n);

/*-----------------------------------------------------------------------------
 * Mouse — caller pushes pointer events here. Coordinates are viewport
 * pixels (same space as set_viewport). `pressed`: 1 = button down, 0 =
 * button up. `button`: 0 = left, 1 = right, 2 = middle. The framework
 * hit-tests the widget tree and dispatches yetty_ygui_widget_on_press /
 * yetty_ygui_widget_on_release to the deepest widget whose rect
 * contains (x, y), bubbling up until something consumes the event.
 *---------------------------------------------------------------------------*/
/* Returns 1 (in the int_result value) if an interactive widget consumed the
 * event, 0 if it fell through unhandled. Callers route the event to the window
 * chrome (drag / resize / maximize) only when it was NOT consumed. */
struct yetty_ycore_int_result yetty_ygui_framework_feed_mouse_button(
    struct yetty_ygui_framework *framework, float x, float y, int button, int pressed, int mods);

/* Returns 1 (value) if a widget consumed the motion (e.g. an in-progress drag
 * or a hover the widget acted on), 0 otherwise — so the chrome can apply the
 * resize-edge cursor only when the client didn't claim the pointer. */
struct yetty_ycore_int_result yetty_ygui_framework_feed_mouse_motion(
    struct yetty_ygui_framework *framework, float x, float y);

/* Wheel / trackpad scroll at (x, y) with deltas (dx, dy). Delivered to the
 * widget under the pointer, bubbling up until a scrollable consumes it. */
struct yetty_ycore_void_result yetty_ygui_framework_feed_mouse_scroll(
    struct yetty_ygui_framework *framework, float x, float y, float dx, float dy);

/*-----------------------------------------------------------------------------
 * Root + viewport.
 *---------------------------------------------------------------------------*/
struct yetty_yclass_object *yetty_ygui_framework_root(struct yetty_ygui_framework *framework);

struct yetty_ycore_void_result yetty_ygui_framework_set_root(struct yetty_ygui_framework *framework,
                                                             struct yetty_yclass_object *root);

struct yetty_ycore_void_result yetty_ygui_framework_set_viewport(
    struct yetty_ygui_framework *framework, float width_px, float height_px);

void yetty_ygui_framework_viewport(const struct yetty_ygui_framework *framework, float *width_px,
                                   float *height_px);

/* Theme — the framework owns a default brand palette on create. Widget
 * paint code reads from this via yetty_ygui_framework_theme(framework).
 * Hosts that want config-driven theming call apply_config_to_theme
 * once with their loaded yconfig; missing keys leave the brand
 * defaults untouched.
 *
 * Lifetime: framework_create allocates the default theme; the framework
 * owns + destroys it on framework_destroy. set_theme replaces the
 * owned theme (framework takes ownership of the new pointer and frees
 * the old one). */
struct yetty_ygui_theme *yetty_ygui_framework_theme(struct yetty_ygui_framework *framework);

struct yetty_ycore_void_result yetty_ygui_framework_set_theme(
    struct yetty_ygui_framework *framework, struct yetty_ygui_theme *theme);

/* Convenience: overlay any `style.ygui.*` / `style.yui.*` keys from
 * `config` onto the framework's owned theme in place. Missing keys leave
 * the field at its current value (brand default or earlier overlay). */
struct yetty_ycore_void_result yetty_ygui_framework_apply_config_to_theme(
    struct yetty_ygui_framework *framework, const struct yetty_yconfig_config *config);

void yetty_ygui_framework_mark_dirty(struct yetty_ygui_framework *framework);

int yetty_ygui_framework_is_dirty(const struct yetty_ygui_framework *framework);

/* Non-zero while a widget holds the pointer capture (between a consumed
 * press and its release) — lets the host suppress other press routing
 * mid-drag. */
int yetty_ygui_framework_has_pressed_widget(const struct yetty_ygui_framework *framework);

/* The widget currently holding the pointer capture (the one that
 * consumed the last press), or NULL. The hovered widget is the deepest
 * hit under the pointer on the last motion event, or NULL. Hosts use
 * these to drive cursor-shape decisions (e.g. resize cursor over a
 * splitter). Both are borrowed — do not destroy. */
struct yetty_yclass_object *yetty_ygui_framework_pressed_widget(
    struct yetty_ygui_framework *framework);
struct yetty_yclass_object *yetty_ygui_framework_hovered_widget(
    struct yetty_ygui_framework *framework);

/* Transient notification ("toast"). The new toolkit has no overlay
 * surface yet, so these record the message to the trace log; the
 * signatures exist so the host (yui) can call them. `severity` is a
 * caller-defined level (0 = info … higher = more severe). */
void yetty_ygui_framework_notify(struct yetty_ygui_framework *framework, int severity,
                                 const char *msg);
void yetty_ygui_framework_notify_ttl(struct yetty_ygui_framework *framework, int severity,
                                     const char *msg, float ttl_seconds);

/*-----------------------------------------------------------------------------
 * App-level key callback. Fires after the byte-stream decoder produces
 * a key event; apps that want to consume input outside the widget
 * focus model (global shortcuts, quit hotkey) install one here.
 * Returning non-zero swallows the event.
 *
 * Special-key codes start at 0x100 and follow the constants below.
 *---------------------------------------------------------------------------*/
typedef int (*yetty_ygui_key_cb)(struct yetty_ygui_framework *framework, uint32_t key, int mods,
                                 void *userdata);

void yetty_ygui_framework_set_key_cb(struct yetty_ygui_framework *framework, yetty_ygui_key_cb cb,
                                     void *userdata);

#define YETTY_YGUI_KEY_ARROW_UP 0x100
#define YETTY_YGUI_KEY_ARROW_DOWN 0x101
#define YETTY_YGUI_KEY_ARROW_LEFT 0x102
#define YETTY_YGUI_KEY_ARROW_RIGHT 0x103
#define YETTY_YGUI_KEY_HOME 0x104
#define YETTY_YGUI_KEY_END 0x105
#define YETTY_YGUI_KEY_PAGE_UP 0x106
#define YETTY_YGUI_KEY_PAGE_DOWN 0x107
#define YETTY_YGUI_KEY_INSERT 0x108
#define YETTY_YGUI_KEY_DELETE 0x109

#define YETTY_YGUI_MOD_SHIFT 0x01
#define YETTY_YGUI_MOD_ALT 0x02
#define YETTY_YGUI_MOD_CTRL 0x04

/*-----------------------------------------------------------------------------
 * Wire id allocator — widgets request ids at construction time.
 *---------------------------------------------------------------------------*/
struct uint32_result yetty_ygui_framework_alloc_id(struct yetty_ygui_framework *framework);

struct yetty_ycore_void_result yetty_ygui_framework_free_id(struct yetty_ygui_framework *framework,
                                                            uint32_t id);

uint32_t yetty_ygui_framework_ygrid_id(const struct yetty_ygui_framework *framework);

/*-----------------------------------------------------------------------------
 * Emit context — supplied to emit_container / emit_body / paint.
 *---------------------------------------------------------------------------*/
struct yetty_ygui_emit_ctx {
    struct yetty_ygui_framework *framework;
    struct yetty_ydraw_drawable_list *ygrid_drawable_list;
    uint32_t current_figure_id;

    /* Sender-side bookkeeping that the receiver only learns about after
     * flush actually delivers the envelope. We stage the deltas here
     * during emit; framework_emit copies them onto `framework` after
     * flush returns OK and discards them on failure so the next tick
     * retries CREATE/DELETE rather than skipping them.
     *
     *  - staged_mints: figure ids whose CREATE_CHILD admin record was
     *    appended this tick. Committed onto framework->minted_figures.
     *  - staged_ygrid_created: ygrid CREATE_CHILD was appended this
     *    tick (was not previously minted). Commits framework->ygrid_created.
     *  - staged_deletes_consumed: prefix of framework->pending_deletes
     *    that has been turned into DELETE_CHILD records. On commit
     *    that prefix is dropped from the queue. */
    uint32_t *staged_mints;
    size_t staged_mint_count;
    size_t staged_mint_cap;
    int staged_ygrid_created;
    size_t staged_deletes_consumed;

    /* Nested-figure clip. As the container walk descends through figure
     * boundaries it narrows this to the intersection of the ancestor
     * figures' rects; each figure is emitted with its rect clipped to it,
     * so a scrollable nested inside another scrollable can't paint outside
     * its parent's box. Inactive at the root (no clipping). */
    struct yetty_ycore_rectangle fig_clip;
    int fig_clip_active;
};

struct yetty_ycore_void_result yetty_ygui_emit_create_child(
    struct yetty_ygui_emit_ctx *ctx, uint32_t child_id, uint32_t kind, float min_x, float min_y,
    float max_x, float max_y, const uint8_t *init_payload, uint32_t init_payload_bytes);

struct yetty_ycore_void_result yetty_ygui_emit_delete_child(struct yetty_ygui_emit_ctx *ctx,
                                                            uint32_t child_id);

struct yetty_ycore_void_result yetty_ygui_emit_set_child_rect(struct yetty_ygui_emit_ctx *ctx,
                                                              uint32_t child_id, float min_x,
                                                              float min_y, float max_x,
                                                              float max_y);

/* Set a child figure's stacking order (z). Additive to CREATE_CHILD —
 * the child exists at z=0; the producer calls this only when its z is
 * non-zero or changes. The receiver re-sorts children by (z, seq). */
struct yetty_ycore_void_result yetty_ygui_emit_set_child_z(struct yetty_ygui_emit_ctx *ctx,
                                                           uint32_t child_id, int32_t z);

/* Show/hide a child figure without destroying it (SET_CHILD_HIDDEN).
 * Keeps the figure + its last body so re-showing is one record, not a
 * CREATE + full-body re-ship. */
struct yetty_ycore_void_result yetty_ygui_emit_set_child_hidden(struct yetty_ygui_emit_ctx *ctx,
                                                                uint32_t child_id, int hidden);

/* Coarse z bands for figure-boundary widgets. Chrome (the shared ygrid:
 * titlebar, statusbar, splitters) sits at 0; floating windows stack in
 * [FLOATING_BASE, MENU) and raise within that band; menus stay on top.
 * Bands keep layers from interleaving regardless of creation order. */
#define YETTY_YGUI_Z_CHROME 0
#define YETTY_YGUI_Z_FLOATING_BASE 100
#define YETTY_YGUI_Z_MENU 1000000

/* Monotonic "bring to front" allocator: returns an ever-increasing z in
 * the floating band so the most recently raised window sorts above its
 * peers (but still below YETTY_YGUI_Z_MENU). */
int32_t yetty_ygui_framework_next_raise_z(struct yetty_ygui_framework *framework);

/* Idempotent helper for figure widgets: on first call for `child_id`
 * emits CREATE_CHILD; on subsequent calls emits SET_CHILD_RECT.
 * Tracks per-framework state so the receiver's binder cache is preserved
 * across frames. Figure widgets should use this from emit_container
 * instead of calling yetty_ygui_emit_create_child unconditionally. */
struct yetty_ycore_void_result yetty_ygui_emit_ensure_child(
    struct yetty_ygui_emit_ctx *ctx, uint32_t child_id, uint32_t kind, float min_x, float min_y,
    float max_x, float max_y, const uint8_t *init_payload, uint32_t init_payload_bytes);

struct yetty_ycore_void_result yetty_ygui_emit_figure_body(struct yetty_ygui_emit_ctx *ctx,
                                                           uint32_t figure_id,
                                                           const uint8_t *payload,
                                                           uint32_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_FRAMEWORK_H */
