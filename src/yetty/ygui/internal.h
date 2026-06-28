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

/* Result wrapper for the subscription-list head getter below. Module-private:
 * the getter is declared only here (not in the generated widget.h), so the
 * matching result type lives here too. */
YETTY_YRESULT_DECLARE(yetty_ygui_event_subscription_ptr, struct yetty_ygui_event_subscription *);

struct yetty_ygui_framework;
struct yetty_yfigure_producer_session;

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
    struct yetty_yclass_object *obj, struct yetty_yclass_object *framework);

/* Clear any hover/press capture the framework holds on `widget`. Called from
 * widget destroy; the framework data slice is opaque outside framework.c, so
 * the field reset goes through this accessor. Defined in framework.c. */
struct yetty_ycore_void_result yetty_ygui_framework_forget_widget(
    struct yetty_yclass_object *obj, struct yetty_yclass_object *widget);
struct yetty_ycore_void_result yetty_ygui_widget_set_id(struct yetty_yclass_object *obj,
                                                        uint32_t id);
struct yetty_ycore_void_result yetty_ygui_widget_set_hovered(struct yetty_yclass_object *obj,
                                                             int hovered);
struct yetty_ycore_void_result yetty_ygui_widget_set_dirty_flag(struct yetty_yclass_object *obj,
                                                                int dirty);
struct yetty_ygui_event_subscription_ptr_result yetty_ygui_widget_subscriptions(
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

/* struct yetty_ygui_framework is the data slice of the `class@ygui:framework`
 * yclass class — its definition (with the annotation) lives in framework.c so
 * the codegen source scan can see it. Other ygui translation units treat it as
 * opaque and reach its state through the generated object-keyed accessors. */

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

/* Hand the accumulated chrome ygrid drawable_list to the ygrid child via
 * yetty_yfigure_apply_child_body on the container object. The figure-tree
 * mutations and figure bodies were already applied inline during the emit
 * walk through the typed yclass stubs, so the ygrid body is the only stream
 * left to ship here. */
struct yetty_ycore_void_result yetty_ygui_framework_flush(struct yetty_ygui_framework *framework);

/* Main-axis scroll offset on the base widget slice — read by the layout
 * pass to slide a scrolling container's in-flow children. Internal. */
struct yetty_ycore_void_result yetty_ygui_widget_scroll_main_set(struct yetty_yclass_object *obj,
                                                                 float offset);
struct yetty_ycore_float_result yetty_ygui_widget_scroll_main_get(
    const struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_INTERNAL_H */
