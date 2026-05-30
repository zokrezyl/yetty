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
#include <yetty/ygui/class.h>

struct yetty_platform_pty;
struct yetty_ydraw_draw_list;
struct yetty_ycore_buffer;
struct yetty_yclass_object;
struct yetty_yclass_rpc_session;

#ifdef __cplusplus
extern "C" {
#endif

/*-----------------------------------------------------------------------------
 * Lifecycle.
 *---------------------------------------------------------------------------*/

/* Create a framework that writes OSC envelopes through `output_pty`.
 * `output_pty` is BORROWED — caller owns its lifetime. */
struct yetty_ygui_framework_ptr_result yetty_ygui_framework_create(
    struct yetty_platform_pty *output_pty);

/* Destroy the framework and its widget tree. Does NOT touch the
 * output_pty (borrowed). */
struct yetty_ycore_void_result yetty_ygui_framework_destroy(struct yetty_ygui_runtime *engine);

/* Wire the framework to a receiver-side yfigure container through the
 * yclass slot dispatch path. When set, framework_emit ships its
 * envelope by calling `yetty_yfigure_process_records(&ctx, container,
 * envelope)` instead of building a yface OSC and writing to output_pty.
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
    struct yetty_ygui_runtime *engine, struct yetty_yclass_object *container);

struct yetty_ycore_void_result yetty_ygui_framework_set_session(
    struct yetty_ygui_runtime *engine, struct yetty_yclass_rpc_session *session);

/* Run one full emit cycle: layout pass against the current viewport,
 * walk the widget tree twice (containers then bodies), concatenate
 * streams, wrap in a yface envelope, write through output_pty. */
struct yetty_ycore_void_result yetty_ygui_framework_emit(struct yetty_ygui_runtime *engine);

/*-----------------------------------------------------------------------------
 * Input — caller pushes raw byte stream (ASCII + CSI escapes) here.
 * The framework decodes and dispatches to widgets / the key callback.
 *---------------------------------------------------------------------------*/
struct yetty_ycore_void_result yetty_ygui_framework_feed_input(struct yetty_ygui_runtime *engine,
                                                               const char *bytes, size_t n);

/*-----------------------------------------------------------------------------
 * Mouse — caller pushes pointer events here. Coordinates are viewport
 * pixels (same space as set_viewport). `pressed`: 1 = button down, 0 =
 * button up. `button`: 0 = left, 1 = right, 2 = middle. The framework
 * hit-tests the widget tree and dispatches yetty_ygui_widget_on_press /
 * yetty_ygui_widget_on_release to the deepest widget whose rect
 * contains (x, y), bubbling up until something consumes the event.
 *---------------------------------------------------------------------------*/
struct yetty_ycore_void_result yetty_ygui_framework_feed_mouse_button(
    struct yetty_ygui_runtime *engine, float x, float y, int button, int pressed, int mods);

struct yetty_ycore_void_result yetty_ygui_framework_feed_mouse_motion(
    struct yetty_ygui_runtime *engine, float x, float y);

/*-----------------------------------------------------------------------------
 * Root + viewport.
 *---------------------------------------------------------------------------*/
struct yetty_ygui_object *yetty_ygui_framework_root(struct yetty_ygui_runtime *engine);

struct yetty_ycore_void_result yetty_ygui_framework_set_root(struct yetty_ygui_runtime *engine,
                                                             struct yetty_ygui_object *root);

struct yetty_ycore_void_result yetty_ygui_framework_set_viewport(struct yetty_ygui_runtime *engine,
                                                                 float width_px, float height_px);

void yetty_ygui_framework_viewport(const struct yetty_ygui_runtime *engine, float *width_px,
                                   float *height_px);

void yetty_ygui_framework_mark_dirty(struct yetty_ygui_runtime *engine);

int yetty_ygui_framework_is_dirty(const struct yetty_ygui_runtime *engine);

/* Non-zero while a widget holds the pointer capture (between a consumed
 * press and its release) — lets the host suppress other press routing
 * mid-drag. */
int yetty_ygui_framework_has_pressed_widget(const struct yetty_ygui_runtime *engine);

/* The widget currently holding the pointer capture (the one that
 * consumed the last press), or NULL. The hovered widget is the deepest
 * hit under the pointer on the last motion event, or NULL. Hosts use
 * these to drive cursor-shape decisions (e.g. resize cursor over a
 * splitter). Both are borrowed — do not destroy. */
struct yetty_ygui_object *yetty_ygui_framework_pressed_widget(struct yetty_ygui_runtime *engine);
struct yetty_ygui_object *yetty_ygui_framework_hovered_widget(struct yetty_ygui_runtime *engine);

/* Transient notification ("toast"). The new toolkit has no overlay
 * surface yet, so these record the message to the trace log; the
 * signatures exist so the host (yui) can call them. `severity` is a
 * caller-defined level (0 = info … higher = more severe). */
void yetty_ygui_framework_notify(struct yetty_ygui_runtime *engine, int severity, const char *msg);
void yetty_ygui_framework_notify_ttl(struct yetty_ygui_runtime *engine, int severity,
                                     const char *msg, float ttl_seconds);

/*-----------------------------------------------------------------------------
 * App-level key callback. Fires after the byte-stream decoder produces
 * a key event; apps that want to consume input outside the widget
 * focus model (global shortcuts, quit hotkey) install one here.
 * Returning non-zero swallows the event.
 *
 * Special-key codes start at 0x100 and follow the constants below.
 *---------------------------------------------------------------------------*/
typedef int (*yetty_ygui_key_cb)(struct yetty_ygui_runtime *engine, uint32_t key, int mods,
                                 void *userdata);

void yetty_ygui_framework_set_key_cb(struct yetty_ygui_runtime *engine, yetty_ygui_key_cb cb,
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
struct uint32_result yetty_ygui_framework_alloc_id(struct yetty_ygui_runtime *engine);

struct yetty_ycore_void_result yetty_ygui_framework_free_id(struct yetty_ygui_runtime *engine,
                                                            uint32_t id);

uint32_t yetty_ygui_framework_ygrid_id(const struct yetty_ygui_runtime *engine);

/*-----------------------------------------------------------------------------
 * Emit context — supplied to emit_container / emit_body / paint.
 *---------------------------------------------------------------------------*/
struct yetty_ygui_emit_ctx {
    struct yetty_ygui_runtime *engine;
    struct yetty_ycore_buffer *container_records;
    struct yetty_ydraw_draw_list *ygrid_draw_list;
    struct yetty_ycore_buffer *figure_bodies;
    uint32_t current_figure_id;

    /* Sender-side bookkeeping that the receiver only learns about after
     * flush actually delivers the envelope. We stage the deltas here
     * during emit; framework_emit copies them onto `engine` after
     * flush returns OK and discards them on failure so the next tick
     * retries CREATE/DELETE rather than skipping them.
     *
     *  - staged_mints: figure ids whose CREATE_CHILD admin record was
     *    appended this tick. Committed onto engine->minted_figures.
     *  - staged_ygrid_created: ygrid CREATE_CHILD was appended this
     *    tick (was not previously minted). Commits engine->ygrid_created.
     *  - staged_deletes_consumed: prefix of engine->pending_deletes
     *    that has been turned into DELETE_CHILD records. On commit
     *    that prefix is dropped from the queue. */
    uint32_t *staged_mints;
    size_t staged_mint_count;
    size_t staged_mint_cap;
    int staged_ygrid_created;
    size_t staged_deletes_consumed;
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

/* Idempotent helper for figure widgets: on first call for `child_id`
 * emits CREATE_CHILD; on subsequent calls emits SET_CHILD_RECT.
 * Tracks per-engine state so the receiver's binder cache is preserved
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
