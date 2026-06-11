/*
 * ygui-internal.h — private types shared across the ygui implementation.
 *
 * Held to the .c files (not installed). Exposes:
 *   - struct yetty_yclass_object (full definition; public API is opaque)
 *   - struct yetty_ygui_framework (full definition; public API is opaque)
 *
 * Out-of-file callers go through the public API — including this
 * header from a widget or test .c is acceptable for unit tests that
 * need to poke at internals, but production widget code should stick
 * to public accessors / setters.
 */
#ifndef YETTY_YGUI_INTERNAL_H
#define YETTY_YGUI_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ygui/framework.h>
#include <yetty/ygui/event.h>
/* Deliberately NOT including <yetty/ygui/widget.h> here: that is the base
 * widget class's own generated public header. widget.c includes this
 * internal.h, and a TU must not pull in its own generated header (its
 * YETTY_YRESULT_DECLARE would clash with the one the TU declares manually).
 * Widgets that need the base-widget API include <yetty/ygui/widget.h>
 * directly — it is their parent header, which is always fine. */

#ifdef __cplusplus
extern "C" {
#endif

/* Slot-domain string every ygui class registers under. */
#define YETTY_YGUI_DOMAIN "yetty_ygui"

/*---------------------------------------------------------------------------
 * yclass dispatch conveniences (impl in dispatch.c). ygui owns no class
 * system of its own — these just wrap <yetty/yclass/class.h> with ygui's domain
 * and its "missing override == no-op" convention.
 *-------------------------------------------------------------------------*/

/* Look up the slot owned by `method_id` in ygui's domain. Returns
 * YETTY_YCLASS_METHOD_SLOT_UNDEFINED when the id isn't registered. */
struct yetty_yclass_method_slot_result yetty_ygui_method_slot_get(
    yetty_yclass_method_id_t method_id);

/* Resolve `slot` against `cls`'s dispatch table. NULL on miss (a missing
 * override is a no-op, not an error). */
struct yetty_yclass_impl_t_result yetty_ygui_dispatch_lookup(const struct yetty_yclass *cls,
                                                             yetty_yclass_method_slot slot);

/* Walk up the parent chain (skipping the leaf) and return the first
 * non-NULL dispatch entry for `slot`. Super invokers chain through this. */
struct yetty_yclass_impl_t_result yetty_ygui_dispatch_lookup_super(
    const struct yetty_yclass *self_class, yetty_yclass_method_slot slot);

/*===========================================================================
 * Classes — `struct yetty_yclass` (from <yetty/yclass/class.h>) is the only
 * class type ygui knows about. Slot allocation, dispatch, parent / mixin
 * walks, instance allocation (yetty_yclass_object_alloc) and data-slice
 * resolution (yetty_yclass_object_data) all go through the yclass runtime.
 * ygui objects ARE plain `struct yetty_yclass_object`s; the widget tree and
 * per-widget framework state are flat members of the base-class data slice
 * (struct yetty_ygui_widget, defined in widget.c).
 *=========================================================================*/

/*===========================================================================
 * Object — runtime widget instance.
 *=========================================================================*/

struct yetty_ygui_event_subscription {
    enum yetty_ygui_event_type type;
    yetty_ygui_event_cb cb;
    void *userdata;
    struct yetty_ygui_event_subscription *next;
};

struct yetty_ygui_framework;

/* Framework-internal mutators of the widget base data slice (struct
 * yetty_ygui_widget, defined in widget.c). The widget tree links and the
 * per-widget framework / dirty / hover state are flat members of that slice.
 * Public read accessors (yetty_ygui_widget_parent / _first_child /
 * _next_sibling / _id / _framework / _is_dirty / _is_hovered) are published
 * in <yetty/ygui/widget.h>; these write-side helpers stay module-private —
 * only framework.c (tree + dirty/hover bookkeeping) and event.c (subscription
 * list) touch them. Defined in widget.c, which owns the struct. The raw
 * _set_dirty_flag does NOT mark the framework dirty (unlike the public
 * yetty_ygui_widget_set_dirty), so framework.c manages that bit itself. */
struct yetty_ycore_void_result yetty_ygui_widget_set_framework(
    struct yetty_yclass_object *obj, struct yetty_ygui_framework *framework);
struct yetty_ycore_void_result yetty_ygui_widget_set_id(struct yetty_yclass_object *obj,
                                                        uint32_t id);
struct yetty_ycore_void_result yetty_ygui_widget_set_hovered(struct yetty_yclass_object *obj,
                                                             int hovered);
struct yetty_ycore_void_result yetty_ygui_widget_set_dirty_flag(struct yetty_yclass_object *obj,
                                                                int dirty);
struct yetty_ygui_event_subscription *yetty_ygui_widget_subscriptions(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_widget_set_subscriptions(
    struct yetty_yclass_object *obj, struct yetty_ygui_event_subscription *subscriptions);

/*===========================================================================
 * framework.
 *=========================================================================*/

/* Byte-stream input decoder state. ASCII pass-through; ESC starts a
 * CSI escape sequence that ends on a final byte and decodes to one of
 * the YETTY_YGUI_KEY_* codes. */
enum yetty_ygui_csi_state {
    YETTY_YGUI_CSI_NORMAL = 0,
    YETTY_YGUI_CSI_ESC,
    YETTY_YGUI_CSI_BRACKET,
};

struct yetty_ygui_input_state {
    enum yetty_ygui_csi_state st;
    char params[16];
    int params_len;
};

struct yetty_ygui_framework {
    /* Borrowed — caller owns the lifetime. */
    struct yetty_platform_pty *output_pty;

    uint32_t next_id;
    uint32_t *free_ids;
    size_t free_id_count;
    size_t free_id_cap;

    /* Monotonic floating-window raise allocator (see
     * yetty_ygui_framework_next_raise_z). Sits in the floating z band. */
    int32_t next_raise_z;

    /* Pending deletes — ids whose receiver-side figures need to go
     * away on the next envelope. */
    uint32_t *pending_deletes;
    size_t pending_delete_count;
    size_t pending_delete_cap;

    /* Receiver-side ygrid id. Primitive widgets share this one ygrid;
     * figure widgets emit their own CREATE_CHILD records with their
     * own ids alongside it. */
    uint32_t ygrid_id;
    int ygrid_created;

    /* Set of figure ids that have already been minted on the receiver
     * via CREATE_CHILD. Each frame: figure widgets check this set; if
     * their id is present they emit SET_CHILD_RECT (cheap rect update);
     * otherwise they emit CREATE_CHILD and add themselves to the set.
     *
     * The set is a sorted dense array kept small — figure widgets are
     * rare (a handful per app). free_id drops the id back out. */
    uint32_t *minted_figures;
    size_t minted_figure_count;
    size_t minted_figure_cap;

    struct yetty_yclass_object *root;

    /* Viewport in pixels — root widget bounds for the next layout
     * pass. Defaults to 800x600 until set_viewport is called. */
    float viewport_w;
    float viewport_h;

    /* Chrome palette + canonical sizes. Owned by the framework: created
     * in framework_create with the brand defaults; destroyed in
     * framework_destroy. yetty_ygui_framework_set_theme replaces the
     * owned theme (caller passes ownership in). Widget paint code
     * consults this via yetty_ygui_framework_theme(framework). */
    struct yetty_ygui_theme *theme;

    /* framework-level dirty flag. Cleared by emit. */
    int dirty;

    /* Reusable per-emit buffers. Cleared at the start of each emit. */
    struct yetty_ycore_buffer container_records;
    struct yetty_ycore_buffer figure_bodies;
    /* Shared ydraw drawable_list — primitive widgets append SDF / glyph
     * records here. Lazily created on first emit; reused across
     * frames. */
    struct yetty_ydraw_drawable_list *ygrid_drawable_list;

    /* Byte-stream input decoder state. */
    struct yetty_ygui_input_state input;

    /* App-level key callback. */
    yetty_ygui_key_cb key_cb;
    void *key_userdata;

    /* Deepest widget currently under the mouse, tracked by
     * feed_mouse_motion. Used to dispatch enter/leave + flip the
     * obj->hovered flag so widgets can paint a hover variant. */
    struct yetty_yclass_object *hovered_obj;

    /* Pointer-capture target. Set to the widget that consumed the last
     * press; subsequent motion + the matching release are routed here
     * regardless of hit-test, so click-and-drag (slider, splitter)
     * keeps working when the cursor leaves the widget's rect. Cleared
     * on release and on destroy of the captured object. */
    struct yetty_yclass_object *pressed_obj;

    /* yclass-dispatch state for shipping the per-emit envelope to the
     * receiver-side yfigure root container. When `container_obj` is
     * set, framework_flush calls `yetty_yfigure_process_records(&ctx,
     * obj, envelope)` instead of wrapping the bytes in a yface OSC
     * and writing to output_pty. The slot dispatches locally
     * (ctx.session == NULL → the impl runs directly on the in-process
     * container, zero copy) or via yrpc (ctx.session set → the stub
     * marshals the buffer over the session's transport).
     *
     * The runtime tracks ONLY the root container at the yclass level;
     * every child figure is still addressed by parent-scoped uint32_t
     * id inside the envelope's record stream (the container's
     * `process_records` impl decodes those and routes each record to
     * the right child). No per-child yclass proxy is kept here.
     *
     * Both pointers are caller-owned (borrowed) — the host (e.g. yui)
     * wires them post-create via yetty_ygui_framework_set_container_obj
     * / _set_session and keeps the underlying objects alive for as
     * long as the framework. When `container_obj` is NULL the
     * framework keeps using the legacy yface-over-pty path. */
    struct yetty_yclass_ctx yclass_ctx;
    struct yetty_yclass_object *container_obj;
};

/*===========================================================================
 * Internal helpers.
 *=========================================================================*/

/* Build the framework's own ygrid record if not yet created.
 * Idempotent — flips ygrid_created on first call. */
struct yetty_ycore_void_result yetty_ygui_framework_ensure_chrome(
    struct yetty_ygui_framework *framework, struct yetty_ygui_emit_ctx *ctx);

/* Walk the tree invoking emit_container on every widget (pre-order). */
struct yetty_ycore_void_result yetty_ygui_framework_walk_emit_container(
    struct yetty_yclass_object *node, struct yetty_ygui_emit_ctx *ctx);

/* Walk the tree invoking emit_body on every widget (pre-order). */
struct yetty_ycore_void_result yetty_ygui_framework_walk_emit_body(struct yetty_yclass_object *node,
                                                                   struct yetty_ygui_emit_ctx *ctx);

/* Flush the three streams into the output pty as one yface envelope.
 * The envelope body is a sequence of {length, id, payload} records:
 *   - container_records appear with id = framework'S CONTAINER ID and the
 *     bytes are admin-record payloads
 *   - ygrid_body appears with id = ygrid_id wrapped in one record
 *   - figure_bodies are already record-framed inside the buffer
 *     (framework wraps each figure's body at append time using
 *     current_figure_id). */
struct yetty_ycore_void_result yetty_ygui_framework_flush(struct yetty_ygui_framework *framework);

/* Append one {length, id, payload} record to `dst`. */
struct yetty_ycore_void_result yetty_ygui_wire_append_record(struct yetty_ycore_buffer *dst,
                                                             uint32_t id, const uint8_t *payload,
                                                             uint32_t payload_len);

/* Main-axis scroll offset on the base widget slice — read by the layout
 * pass to slide a scrolling container's in-flow children. Internal. */
struct yetty_ycore_void_result yetty_ygui_widget_scroll_main_set(struct yetty_yclass_object *obj,
                                                                 float offset);
float yetty_ygui_widget_scroll_main_get(const struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_INTERNAL_H */
