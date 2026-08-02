/*
 * framework-defs.h — hand-written public companions to the generated ygui
 * framework class header.
 *
 * The framework is a yclass class, so <yetty/ygui/framework.h> is produced by
 * codegen: the class machinery (create/destroy/from) plus the Result-returning
 * methods carried by `expose` annotations. The declarations here are the parts
 * that don't fit the expose model — the raw-return accessors, the input key
 * callback with its key codes, the z-band constants, and the emit-walk context
 * shared with widget paint code. The generated header pulls this file in via an
 * `include@` directive on the framework class, so a consumer that includes
 * <yetty/ygui/framework.h> transparently sees everything below.
 */
#ifndef YETTY_YGUI_FRAMEWORK_DEFS_H
#define YETTY_YGUI_FRAMEWORK_DEFS_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_ydraw_drawable_list;
struct yetty_yclass_object;
struct yetty_ygui_theme;
struct yetty_ygui_framework;
struct yetty_yfont_font;
struct yetty_yclass_rpc_session;
struct yetty_ywire_connection;

#ifdef __cplusplus
extern "C" {
#endif

/*-----------------------------------------------------------------------------
 * Raw-return accessors. These predate Result-returning getters and keep their
 * plain return types so callers read them inline; that is why they are hand-
 * declared here rather than generated from `expose` (which requires a Result
 * return). Each takes the framework yclass object.
 *---------------------------------------------------------------------------*/
struct yetty_ygui_theme *yetty_ygui_framework_theme(struct yetty_yclass_object *obj);

/* The RPC session / multiplexed wire connection behind a framework that was
 * attached over fds (yetty_ygui_framework_attach). NULL when the framework is
 * in-process (set_container_obj) or attached over a caller-owned connection.
 * The app's event loop owns the fds: select on connection_fd()/out_fd() and
 * call the connection pumps on readiness — reading the fd directly (a private
 * yface) races the connection's demux and drops its envelopes (credit grants,
 * RPC responses). */
struct yetty_yclass_rpc_session *yetty_ygui_framework_rpc_session(struct yetty_yclass_object *obj);
struct yetty_ywire_connection *yetty_ygui_framework_wire_connection(
    struct yetty_yclass_object *obj);

/* Borrowed measurement font (see the `font` field in the framework struct).
 * yetty_ygui_framework_font returns NULL until the app calls set_font; widgets
 * that measure text (textinput caret/click) then fall back to a fixed advance.
 * The font is NOT owned — the app keeps ownership. */
struct yetty_yfont_font *yetty_ygui_framework_font(struct yetty_yclass_object *obj);
void yetty_ygui_framework_set_font(struct yetty_yclass_object *obj, struct yetty_yfont_font *font);

void yetty_ygui_framework_viewport(struct yetty_yclass_object *obj, float *width_px,
                                   float *height_px);

void yetty_ygui_framework_mark_dirty(struct yetty_yclass_object *obj);
int yetty_ygui_framework_is_dirty(struct yetty_yclass_object *obj);

int yetty_ygui_framework_has_pressed_widget(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ygui_framework_pressed_widget(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ygui_framework_hovered_widget(struct yetty_yclass_object *obj);

struct yetty_yclass_object *yetty_ygui_framework_root(struct yetty_yclass_object *obj);

uint32_t yetty_ygui_framework_ygrid_id(struct yetty_yclass_object *obj);

/* Transient notification ("toast"). The toolkit has no overlay surface yet, so
 * these record the message to the trace log; the signatures exist so the host
 * can call them. `severity` is a caller-defined level (0 = info … higher = more
 * severe). */
void yetty_ygui_framework_notify(struct yetty_yclass_object *obj, int severity, const char *msg);
void yetty_ygui_framework_notify_ttl(struct yetty_yclass_object *obj, int severity, const char *msg,
                                     float ttl_seconds);

/*-----------------------------------------------------------------------------
 * App-level key callback. Fires after the byte-stream decoder produces a key
 * event; apps that want to consume input outside the widget focus model
 * (global shortcuts, quit hotkey) install one here. Returning non-zero swallows
 * the event. Special-key codes start at 0x100 and follow the constants below.
 *---------------------------------------------------------------------------*/
typedef int (*yetty_ygui_key_cb)(struct yetty_yclass_object *framework, uint32_t key, int mods,
                                 void *userdata);

void yetty_ygui_framework_set_key_cb(struct yetty_yclass_object *obj, yetty_ygui_key_cb cb,
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
#define YETTY_YGUI_KEY_F12 0x10A

#define YETTY_YGUI_MOD_SHIFT 0x01
#define YETTY_YGUI_MOD_ALT 0x02
#define YETTY_YGUI_MOD_CTRL 0x04

/* Coarse z bands for figure-boundary widgets. Chrome (the shared ygrid:
 * titlebar, statusbar, splitters) sits at 0; floating windows stack in
 * [FLOATING_BASE, MENU) and raise within that band; menus stay on top. Bands
 * keep layers from interleaving regardless of creation order. */
#define YETTY_YGUI_Z_CHROME 0
#define YETTY_YGUI_Z_FLOATING_BASE 100
#define YETTY_YGUI_Z_MENU 1000000

/* Monotonic "bring to front" allocator: returns an ever-increasing z in the
 * floating band so the most recently raised window sorts above its peers (but
 * still below YETTY_YGUI_Z_MENU). */
int32_t yetty_ygui_framework_next_raise_z(struct yetty_yclass_object *obj);

/*-----------------------------------------------------------------------------
 * Emit context — supplied to emit_container / emit_body / paint.
 *---------------------------------------------------------------------------*/
struct yetty_ygui_emit_ctx {
    struct yetty_ygui_framework *framework;
    struct yetty_ydraw_drawable_list *ygrid_drawable_list;
    uint32_t current_figure_id;

    /* Sender-side bookkeeping that the receiver only learns about after flush
     * actually delivers the envelope. We stage the deltas here during emit;
     * framework_emit copies them onto `framework` after flush returns OK and
     * discards them on failure so the next tick retries CREATE/DELETE rather
     * than skipping them.
     *
     *  - staged_mints: figure ids whose CREATE_CHILD admin record was appended
     *    this tick. Committed onto framework->minted_figures.
     *  - staged_ygrid_created: ygrid CREATE_CHILD was appended this tick (was
     *    not previously minted). Commits framework->ygrid_created.
     *  - staged_deletes_consumed: prefix of framework->pending_deletes that has
     *    been turned into DELETE_CHILD records. On commit that prefix is dropped
     *    from the queue. */
    uint32_t *staged_mints;
    size_t staged_mint_count;
    size_t staged_mint_cap;
    int staged_ygrid_created;
    size_t staged_deletes_consumed;

    /* Nested-figure clip. As the container walk descends through figure
     * boundaries it narrows this to the intersection of the ancestor figures'
     * rects; each figure is emitted with its rect clipped to it, so a scrollable
     * nested inside another scrollable can't paint outside its parent's box.
     * Inactive at the root (no clipping). */
    struct yetty_ycore_rectangle fig_clip;
    int fig_clip_active;

    /* Set while emitting a RETAINED-scene (yscene) figure boundary's body.
     * Content is document-space: the receiver keeps the whole document and
     * scrolls it on the GPU, so body-emitting widgets subtract figure_origin
     * (the boundary widget's unclipped rect.min) instead of emitting absolute
     * coords, and skip viewport culling. */
    int figure_retained;
    float figure_origin_x;
    float figure_origin_y;
};

/* True when the receiver already holds `child_id` from a PREVIOUS successful
 * flush (the committed mint set). Mints staged in the in-flight tick do NOT
 * count. Body-emitting widgets use this to skip re-shipping an unchanged body:
 * committed + unchanged content + unchanged rect ⇒ the receiver's copy is
 * already current (see yimage/yvideo emit_body). */
int yetty_ygui_emit_child_committed(const struct yetty_ygui_emit_ctx *ctx, uint32_t child_id);

struct yetty_ycore_void_result yetty_ygui_emit_create_child(
    struct yetty_ygui_emit_ctx *ctx, uint32_t child_id, uint32_t kind, float min_x, float min_y,
    float max_x, float max_y, const uint8_t *init_payload, uint32_t init_payload_bytes);

struct yetty_ycore_void_result yetty_ygui_emit_delete_child(struct yetty_ygui_emit_ctx *ctx,
                                                            uint32_t child_id);

struct yetty_ycore_void_result yetty_ygui_emit_set_child_rect(struct yetty_ygui_emit_ctx *ctx,
                                                              uint32_t child_id, float min_x,
                                                              float min_y, float max_x,
                                                              float max_y);

/* Set a child figure's stacking order (z). Additive to CREATE_CHILD — the child
 * exists at z=0; the producer calls this only when its z is non-zero or changes.
 * The receiver re-sorts children by (z, seq). */
struct yetty_ycore_void_result yetty_ygui_emit_set_child_z(struct yetty_ygui_emit_ctx *ctx,
                                                           uint32_t child_id, int32_t z);

/* Show/hide a child figure without destroying it (SET_CHILD_HIDDEN). Keeps the
 * figure + its last body so re-showing is one record, not a CREATE + full-body
 * re-ship. */
struct yetty_ycore_void_result yetty_ygui_emit_set_child_hidden(struct yetty_ygui_emit_ctx *ctx,
                                                                uint32_t child_id, int hidden);

/* Retained-scene figure view state: the receiver scrolls its retained content
 * on the GPU (SET_CHILD_SCROLL) over the declared document extent
 * (SET_CHILD_CONTENT_SIZE). Nothing is re-emitted for a scroll change. */
struct yetty_ycore_void_result yetty_ygui_emit_set_child_scroll(struct yetty_ygui_emit_ctx *ctx,
                                                                uint32_t child_id, float scroll_x,
                                                                float scroll_y);

struct yetty_ycore_void_result yetty_ygui_emit_set_child_content_size(
    struct yetty_ygui_emit_ctx *ctx, uint32_t child_id, float content_w, float content_h);

/* Idempotent helper for figure widgets: on first call for `child_id` emits
 * CREATE_CHILD; on subsequent calls emits SET_CHILD_RECT. Tracks per-framework
 * state so the receiver's binder cache is preserved across frames. Figure
 * widgets should use this from emit_container instead of calling
 * yetty_ygui_emit_create_child unconditionally. */
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

#endif /* YETTY_YGUI_FRAMEWORK_DEFS_H */
