/*
 * ygui2 widget — the base class of the drawable-contract toolkit.
 *
 * A widget is a yclass object in a parent/child tree. Rendering is a
 * projection onto the yvterm group tree (strategy.md §3): a NON-transparent
 * widget mints one wire group whose offset carries the widget's position;
 * its `paint` virtual appends primitives in WIDGET-LOCAL coordinates
 * (0,0 = the widget's top-left — the group offset places them). Transparent
 * widgets (layout-only containers: vbox/hbox) mint nothing; their children
 * attach to the nearest minted ancestor.
 *
 * Dirty classes (strategy.md §4): position (offset update), skin (reopen
 * this widget's group), structure (reopen; children re-emitted). The
 * framework walks these at emit and issues the smallest sufficient wire
 * operation.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ygui2/defs.h>
#include <yetty/ytrace/ytrace.h>

struct yetty_yclass_ptr_result yetty_ygui2_widget_class_get(void);

struct YETTY_ANNOTATE("class@ygui2:widget") yetty_ygui2_widget {
    /* Tree. */
    struct yetty_yclass_object *parent;
    struct yetty_yclass_object *first_child;
    struct yetty_yclass_object *next_sibling;
    /* Owning framework object; stored on the root, resolved via parent walk
     * elsewhere. */
    struct yetty_yclass_object *framework;

    /* Wire identity: id within the parent GROUP scope, allocated by the
     * framework at add time. 0 on transparent widgets (no group minted). */
    uint32_t node_id;
    /* The SKIN subgroup id: a nested group holding ONLY this widget's own
     * painted primitives, minted lazily at first insertion. A skin-only
     * change reopens just this subgroup — the widget's containment group
     * (and every descendant, complex runtimes included) stays live. */
    uint32_t skin_node_id;
    /* Layout-only container: mints no group; children attach to the nearest
     * minted ancestor and this widget contributes position only through its
     * children's offsets. */
    int transparent;

    /* Computed rect (logical px, pane-absolute) — output of the layout
     * pass. */
    float x, y, w, h;
    /* Offset last emitted for the minted group (relative to the nearest
     * minted ancestor), so a pure move is ONE offset update. */
    float emitted_offset_x, emitted_offset_y;
    int ever_emitted;

    struct yetty_ygui2_layout layout;
    int visible;
    int focusable;
    /* Overlay child policy: a press outside any overlay widget hides this
     * one (popup/dropdown behavior; dialogs keep 0). */
    int dismiss_on_outside;

    /* Absolute placement (ytop-style): when set, the layout pass places
     * this widget at the stored rect (pane px) instead of flex flow. */
    int absolute;
    float abs_x, abs_y, abs_w, abs_h;

    /* Scroll offset applied to the children's layout flow (a viewport
     * widget shifts its content up by scroll_y). Moving it re-lays children
     * → their offset updates ship automatically. */
    float scroll_x;
    float scroll_y;
    /* Emit a CLIP rect (0,0,w,h) for this widget's group — the viewport
     * boundary (contract §1a clip). */
    int clip_enabled;
    float emitted_clip_w;
    float emitted_clip_h;

    /* Dirty classes. */
    int dirty_skin;
    int dirty_structure;
    int dirty_position;
    /* Size changed since the last emit: the incremental walk calls the
     * widget_emit_geometry virtual so a widget hosting a resizable
     * runtime (plot) can ship its tiny addressed geometry op in the same
     * envelope — never a structural re-send of the retained record. */
    int dirty_geometry;
};

YETTY_YRESULT_DECLARE(yetty_ygui2_widget_ptr, struct yetty_ygui2_widget *);
struct yetty_ygui2_widget_ptr_result yetty_ygui2_widget_from(struct yetty_yclass_object *obj);

YETTY_ANNOTATE("virtual@ygui2:widget:constructor")
static struct yetty_ycore_void_result widget_constructor(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 widget ctor: data");
    struct yetty_ygui2_widget *widget = data_res.value;
    memset(widget, 0, sizeof(*widget));
    widget->visible = 1;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@ygui2:widget:destructor")
static struct yetty_ycore_void_result widget_destructor(struct yetty_yclass_object *obj)
{
    /* Children are separate objects owned by the tree; the framework
     * destroys them on remove. Nothing heap-owned in the base slice. */
    (void)obj;
    return YETTY_OK_VOID();
}

/* Paint the widget's own primitives into `list` in WIDGET-LOCAL pixels
 * (0,0 = widget top-left; the minted group's offset places them). The base
 * paints nothing. */
YETTY_ANNOTATE("virtual@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result widget_paint(struct yetty_yclass_object *obj,
                                                   struct yetty_ydraw_drawable_list *list)
{
    (void)obj;
    (void)list;
    return YETTY_OK_VOID();
}

/* RETAINED content (T5): emitted in the widget's CONTAINMENT group, after
 * the skin subgroup — hosted complex records whose runtime must survive
 * skin repaints, theme restyles, movement and ancestor resizes. Replaced
 * ONLY by an intentional structural reopen (set_record and friends mark
 * structure dirt). The base emits nothing. */
YETTY_ANNOTATE("virtual@ygui2:widget:widget_paint_retained")
static struct yetty_ycore_void_result widget_paint_retained(struct yetty_yclass_object *obj,
                                                            struct yetty_ydraw_drawable_list *list)
{
    (void)obj;
    (void)list;
    return YETTY_OK_VOID();
}

/* Geometry follow-up for retained content: called by the incremental emit
 * walk when the widget's SIZE changed (dirty_geometry), appending into
 * the SAME frame envelope. A widget hosting a resizable runtime (plot)
 * emits its addressed geometry op here — a few dozen bytes; the receiver
 * re-plans the runtime and its chrome locally, so the record and its
 * data are NEVER re-shipped on resize. The base emits nothing. */
YETTY_ANNOTATE("virtual@ygui2:widget:widget_emit_geometry")
static struct yetty_ycore_void_result widget_emit_geometry(struct yetty_yclass_object *obj,
                                                           struct yetty_ydraw_drawable_list *list)
{
    (void)obj;
    (void)list;
    return YETTY_OK_VOID();
}

/* Pointer input (widget-local pixels). Return 1 = consumed, 0 = bubble. */
YETTY_ANNOTATE("virtual@ygui2:widget:widget_on_press")
static struct yetty_ycore_int_result widget_on_press(struct yetty_yclass_object *obj, float local_x,
                                                     float local_y, int button, int mods)
{
    (void)obj;
    (void)local_x;
    (void)local_y;
    (void)button;
    (void)mods;
    return YETTY_OK(yetty_ycore_int, 0);
}

YETTY_ANNOTATE("virtual@ygui2:widget:widget_on_release")
static struct yetty_ycore_int_result widget_on_release(struct yetty_yclass_object *obj,
                                                       float local_x, float local_y, int button,
                                                       int mods)
{
    (void)obj;
    (void)local_x;
    (void)local_y;
    (void)button;
    (void)mods;
    return YETTY_OK(yetty_ycore_int, 0);
}

YETTY_ANNOTATE("virtual@ygui2:widget:widget_on_motion")
static struct yetty_ycore_int_result widget_on_motion(struct yetty_yclass_object *obj,
                                                      float local_x, float local_y,
                                                      uint32_t buttons_held)
{
    (void)obj;
    (void)local_x;
    (void)local_y;
    (void)buttons_held;
    return YETTY_OK(yetty_ycore_int, 0);
}

YETTY_ANNOTATE("virtual@ygui2:widget:widget_on_scroll")
static struct yetty_ycore_int_result widget_on_scroll(struct yetty_yclass_object *obj,
                                                      float local_x, float local_y, float wheel_dy)
{
    (void)obj;
    (void)local_x;
    (void)local_y;
    (void)wheel_dy;
    return YETTY_OK(yetty_ycore_int, 0);
}

/* Keyboard input for the focused widget. Return 1 = consumed. */
YETTY_ANNOTATE("virtual@ygui2:widget:widget_on_key")
static struct yetty_ycore_int_result widget_on_key(struct yetty_yclass_object *obj, uint32_t key,
                                                   uint32_t mods)
{
    (void)obj;
    (void)key;
    (void)mods;
    return YETTY_OK(yetty_ycore_int, 0);
}

/* Subclass teardown hook: release owned heap state (record copies, owned
 * drawable lists) before the base free. The base owns nothing. */
YETTY_ANNOTATE("virtual@ygui2:widget:widget_cleanup")
static struct yetty_ycore_void_result widget_cleanup(struct yetty_yclass_object *obj)
{
    (void)obj;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Exposed tree / state API (compatibility-shaped with ygui).
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_mark_skin_dirty(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 mark_skin_dirty: data");
    data_res.value->dirty_skin = 1;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_mark_structure_dirty(
    struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 mark_structure_dirty: data");
    data_res.value->dirty_structure = 1;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_layout_set(struct yetty_yclass_object *obj,
                                                             const struct yetty_ygui2_layout *spec)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 layout_set: data");
    if (!spec) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 layout_set: NULL spec");
    }
    struct yetty_ygui2_layout *current = &data_res.value->layout;
    /* Memberwise, not memcmp — struct padding is not canonical. An
     * identical spec is a NO-OP: no dirt, no emission. */
    if (current->basis == spec->basis && current->grow == spec->grow &&
        current->cross_size == spec->cross_size && current->min_main == spec->min_main &&
        current->direction == spec->direction && current->gap == spec->gap &&
        current->pad_left == spec->pad_left && current->pad_top == spec->pad_top &&
        current->pad_right == spec->pad_right && current->pad_bottom == spec->pad_bottom) {
        return YETTY_OK_VOID();
    }
    *current = *spec;
    /* A layout change requests RELAYOUT — it adds, removes and reorders
     * nothing on the wire. The layout pass turns the new spec into rect
     * deltas; those select offset updates and size-driven skin repaints.
     * Structure dirt would replace the whole subtree (and kill hosted
     * complex runtimes) for what may be pure movement. */
    data_res.value->dirty_position = 1;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_rect(struct yetty_yclass_object *obj,
                                                       float *out_x, float *out_y, float *out_w,
                                                       float *out_h)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 widget_rect: data");
    struct yetty_ygui2_widget *widget = data_res.value;
    if (out_x) {
        *out_x = widget->x;
    }
    if (out_y) {
        *out_y = widget->y;
    }
    if (out_w) {
        *out_w = widget->w;
    }
    if (out_h) {
        *out_h = widget->h;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Field accessors — the class struct is TU-private (yclass convention);
 * framework.c drives its layout/emit walks through these.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_init_base(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *framework,
                                                            struct yetty_yclass_object *parent,
                                                            uint32_t node_id)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 init_base: slice");
    struct yetty_ygui2_widget *widget = data_res.value;
    memset(widget, 0, sizeof(*widget));
    widget->visible = 1;
    widget->framework = framework;
    widget->parent = parent;
    widget->node_id = node_id;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_link_child(struct yetty_yclass_object *parent,
                                                             struct yetty_yclass_object *child)
{
    struct yetty_ygui2_widget_ptr_result parent_res = yetty_ygui2_widget_from(parent);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, parent_res, "ygui2 link_child: parent slice");
    struct yetty_yclass_object **link = &parent_res.value->first_child;
    while (*link) {
        struct yetty_ygui2_widget_ptr_result sibling_res = yetty_ygui2_widget_from(*link);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sibling_res, "ygui2 link_child: sibling slice");
        link = &sibling_res.value->next_sibling;
    }
    *link = child;
    parent_res.value->dirty_structure = 1;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui2_widget_first_child(
    struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, data_res, "ygui2 first_child: slice");
    return YETTY_OK(yetty_yclass_object_ptr, data_res.value->first_child);
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui2_widget_next_sibling(
    struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, data_res, "ygui2 next_sibling: slice");
    return YETTY_OK(yetty_yclass_object_ptr, data_res.value->next_sibling);
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui2_widget_parent_obj(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, data_res, "ygui2 parent_obj: slice");
    return YETTY_OK(yetty_yclass_object_ptr, data_res.value->parent);
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui2_widget_framework_obj(
    struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, data_res, "ygui2 framework_obj: slice");
    return YETTY_OK(yetty_yclass_object_ptr, data_res.value->framework);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ygui2_widget_node_id(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, data_res, "ygui2 node_id: slice");
    return YETTY_OK(yetty_ycore_uint32, data_res.value->node_id);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ygui2_widget_skin_node_id(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, data_res, "ygui2 skin_node_id: slice");
    return YETTY_OK(yetty_ycore_uint32, data_res.value->skin_node_id);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_set_skin_node_id(struct yetty_yclass_object *obj,
                                                                   uint32_t skin_node_id)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_skin_node_id: slice");
    data_res.value->skin_node_id = skin_node_id;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_set_node_id(struct yetty_yclass_object *obj,
                                                              uint32_t node_id)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_node_id: slice");
    data_res.value->node_id = node_id;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_set_transparent(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_transparent: slice");
    if (data_res.value->ever_emitted) {
        /* The widget's group already exists on the wire; going transparent
         * would silently change every descendant path while the emitted
         * group lives on. Transparency is an add-time property. */
        return YETTY_ERR(yetty_ycore_void, "ygui2 set_transparent: widget already emitted");
    }
    data_res.value->transparent = 1;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui2_widget_is_transparent(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 is_transparent: slice");
    return YETTY_OK(yetty_ycore_int, data_res.value->transparent);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui2_widget_is_visible(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 is_visible: slice");
    return YETTY_OK(yetty_ycore_int, data_res.value->visible);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_set_focusable(struct yetty_yclass_object *obj,
                                                                int focusable)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_focusable: slice");
    data_res.value->focusable = focusable ? 1 : 0;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui2_widget_is_focusable(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 is_focusable: slice");
    return YETTY_OK(yetty_ycore_int, data_res.value->focusable);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_set_dismiss_on_outside(
    struct yetty_yclass_object *obj, int dismiss)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_dismiss_on_outside: slice");
    data_res.value->dismiss_on_outside = dismiss ? 1 : 0;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui2_widget_dismiss_on_outside(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 dismiss_on_outside: slice");
    return YETTY_OK(yetty_ycore_int, data_res.value->dismiss_on_outside);
}

/* Cross-class within-module (accessor pattern): the framework owns focus. */
struct yetty_ycore_int_result yetty_ygui2_framework_widget_is_focused(
    struct yetty_yclass_object *framework_obj, struct yetty_yclass_object *widget_obj);

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui2_widget_has_focus(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 has_focus: slice");
    if (!data_res.value->framework && !data_res.value->parent) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_widget_framework_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, framework_res, "ygui2 has_focus: framework");
    return yetty_ygui2_framework_widget_is_focused(framework_res.value, obj);
}

/* Cross-class within-module: theme lives on the framework; widgets read it
 * at paint time. */
struct yetty_ycore_void_result yetty_ygui2_framework_theme_copy(
    struct yetty_yclass_object *framework_obj, struct yetty_ygui2_theme *out_theme);

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_theme_copy(struct yetty_yclass_object *obj,
                                                             struct yetty_ygui2_theme *out_theme)
{
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_widget_framework_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res, "ygui2 theme_copy: framework");
    return yetty_ygui2_framework_theme_copy(framework_res.value, out_theme);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_set_rect(struct yetty_yclass_object *obj, float x,
                                                           float y, float w, float h)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_rect: slice");
    struct yetty_ygui2_widget *widget = data_res.value;
    /* A SIZE change invalidates the painted primitives (widgets bake their
     * width/height into geometry) — schedule a repaint of the SKIN
     * subgroup. Retained content is untouched: a widget hosting a
     * resizable runtime follows up through the widget_emit_geometry
     * virtual (one tiny addressed op in the same envelope), NEVER a
     * structural re-send. A pure origin change stays a cheap offset
     * update, handled by the incremental emit walk. Pre-first-frame this
     * is harmless: the insertion clears the dirt. */
    if (!widget->transparent && (widget->w != w || widget->h != h)) {
        widget->dirty_skin = 1;
        widget->dirty_geometry = 1;
    }
    widget->x = x;
    widget->y = y;
    widget->w = w;
    widget->h = h;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_layout_copy(struct yetty_yclass_object *obj,
                                                              struct yetty_ygui2_layout *out_spec)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 layout_copy: slice");
    if (!out_spec) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 layout_copy: NULL out");
    }
    *out_spec = data_res.value->layout;
    return YETTY_OK_VOID();
}

/* Size dirt for the geometry follow-up (see widget_emit_geometry). Read
 * by the incremental walk; consumed alongside the other classes by
 * clear_dirty. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_geometry_dirty(struct yetty_yclass_object *obj,
                                                                 int *out_geometry)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 geometry_dirty: slice");
    if (out_geometry) {
        *out_geometry = data_res.value->dirty_geometry;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_dirty_flags(struct yetty_yclass_object *obj,
                                                              int *out_skin, int *out_structure,
                                                              int *out_position)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 dirty_flags: slice");
    if (out_skin) {
        *out_skin = data_res.value->dirty_skin;
    }
    if (out_structure) {
        *out_structure = data_res.value->dirty_structure;
    }
    if (out_position) {
        *out_position = data_res.value->dirty_position;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_clear_dirty(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 clear_dirty: slice");
    data_res.value->dirty_skin = 0;
    data_res.value->dirty_structure = 0;
    data_res.value->dirty_position = 0;
    data_res.value->dirty_geometry = 0;
    return YETTY_OK_VOID();
}

/* Absolute placement (compatibility with ytop-style hand layout). A move is
 * position dirt (one offset update on the wire); a size change repaints. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_set_position(struct yetty_yclass_object *obj,
                                                               float x, float y)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_position: slice");
    struct yetty_ygui2_widget *widget = data_res.value;
    /* Entering absolute mode is itself a layout change — the widget
     * leaves the parent's flex flow, so siblings move even when the
     * stored coordinates happen to match. */
    int became_absolute = !widget->absolute;
    widget->absolute = 1;
    if (became_absolute || widget->abs_x != x || widget->abs_y != y) {
        widget->abs_x = x;
        widget->abs_y = y;
        widget->dirty_position = 1;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_set_size(struct yetty_yclass_object *obj, float w,
                                                           float h)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_size: slice");
    struct yetty_ygui2_widget *widget = data_res.value;
    int became_absolute = !widget->absolute;
    widget->absolute = 1;
    if (became_absolute) {
        widget->dirty_position = 1; /* left the flex flow: relayout */
    }
    if (widget->abs_w != w || widget->abs_h != h) {
        widget->abs_w = w;
        widget->abs_h = h;
        widget->dirty_skin = 1; /* geometry is painted — resize repaints */
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_absolute_rect(struct yetty_yclass_object *obj,
                                                                int *out_absolute, float *out_x,
                                                                float *out_y, float *out_w,
                                                                float *out_h)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 absolute_rect: slice");
    struct yetty_ygui2_widget *widget = data_res.value;
    if (out_absolute) {
        *out_absolute = widget->absolute;
    }
    if (out_x) {
        *out_x = widget->abs_x;
    }
    if (out_y) {
        *out_y = widget->abs_y;
    }
    if (out_w) {
        *out_w = widget->abs_w;
    }
    if (out_h) {
        *out_h = widget->abs_h;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_set_visible(struct yetty_yclass_object *obj,
                                                              int visible)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_visible: slice");
    struct yetty_ygui2_widget *widget = data_res.value;
    if (widget->visible != (visible ? 1 : 0)) {
        widget->visible = visible ? 1 : 0;
        /* Membership changed: the PARENT's subtree re-emits (a hidden
         * widget's own flags are unreachable — it is skipped in walks). */
        if (widget->parent) {
            struct yetty_ygui2_widget_ptr_result parent_res =
                yetty_ygui2_widget_from(widget->parent);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, parent_res, "ygui2 set_visible: parent");
            parent_res.value->dirty_structure = 1;
        } else {
            widget->dirty_structure = 1;
        }
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_emitted_offset(struct yetty_yclass_object *obj,
                                                                 float *out_x, float *out_y,
                                                                 int *out_ever)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 emitted_offset: slice");
    if (out_x) {
        *out_x = data_res.value->emitted_offset_x;
    }
    if (out_y) {
        *out_y = data_res.value->emitted_offset_y;
    }
    if (out_ever) {
        *out_ever = data_res.value->ever_emitted;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_set_emitted_offset(
    struct yetty_yclass_object *obj, float x, float y)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_emitted_offset: slice");
    data_res.value->emitted_offset_x = x;
    data_res.value->emitted_offset_y = y;
    data_res.value->ever_emitted = 1;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_set_clip_enabled(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_clip_enabled: slice");
    struct yetty_ygui2_widget *widget = data_res.value;
    if (!widget->clip_enabled) {
        widget->clip_enabled = 1;
        /* Enabling clip on a live group must reach the wire: a skin reopen
         * recreates the group and the offsets/clip pass re-sends the
         * projection state. */
        widget->dirty_skin = 1;
        widget->emitted_clip_w = 0.0f;
        widget->emitted_clip_h = 0.0f;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_clip_state(struct yetty_yclass_object *obj,
                                                             int *out_enabled, float *out_w,
                                                             float *out_h)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 clip_state: slice");
    if (out_enabled) {
        *out_enabled = data_res.value->clip_enabled;
    }
    if (out_w) {
        *out_w = data_res.value->emitted_clip_w;
    }
    if (out_h) {
        *out_h = data_res.value->emitted_clip_h;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_set_emitted_clip(struct yetty_yclass_object *obj,
                                                                   float w, float h)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_emitted_clip: slice");
    data_res.value->emitted_clip_w = w;
    data_res.value->emitted_clip_h = h;
    return YETTY_OK_VOID();
}

/* Forget everything ever emitted for this widget's group instance. Called
 * when that wire instance is (about to be) destroyed — clear, rebuild, or
 * an ancestor reopen that recreates descendants with default group state —
 * so the next emission re-sends all non-default projection state instead
 * of trusting a cache describing the dead instance. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_reset_emitted(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 reset_emitted: slice");
    struct yetty_ygui2_widget *widget = data_res.value;
    widget->emitted_offset_x = 0.0f;
    widget->emitted_offset_y = 0.0f;
    widget->ever_emitted = 0;
    widget->emitted_clip_w = 0.0f;
    widget->emitted_clip_h = 0.0f;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_set_scroll(struct yetty_yclass_object *obj,
                                                             float scroll_x, float scroll_y)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 set_scroll: slice");
    struct yetty_ygui2_widget *widget = data_res.value;
    if (widget->scroll_x != scroll_x || widget->scroll_y != scroll_y) {
        widget->scroll_x = scroll_x;
        widget->scroll_y = scroll_y;
        widget->dirty_position = 1; /* children re-lay; offsets ship */
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_scroll(struct yetty_yclass_object *obj,
                                                         float *out_x, float *out_y)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 scroll: slice");
    if (out_x) {
        *out_x = data_res.value->scroll_x;
    }
    if (out_y) {
        *out_y = data_res.value->scroll_y;
    }
    return YETTY_OK_VOID();
}

/* The cleanup dispatcher is generated at the foot of this TU (the gen
 * impl include below) — declared here for the call in widget_destroy. */
struct yetty_ycore_void_result yetty_ygui2_widget_cleanup(struct yetty_yclass_object *obj);

/* Recursive teardown, children first (same-TU field access). Runs each
 * widget's cleanup virtual before freeing, so subclasses release owned
 * heap state (complex_host record, ydraw_embed list). INTERNAL — not part
 * of the exposed/bound surface: freeing a linked widget without unlinking
 * leaves the framework dangling. Apps remove via yetty_ygui2_widget_remove
 * (which unlinks and invalidates focus/capture first); the framework calls
 * this from dispose/remove only. */
struct yetty_ycore_void_result yetty_ygui2_widget_destroy(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK_VOID();
    }
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 widget_destroy: slice");
    struct yetty_ygui2_widget *widget = data_res.value;
    while (widget->first_child) {
        struct yetty_yclass_object *child = widget->first_child;
        struct yetty_ygui2_widget_ptr_result child_res = yetty_ygui2_widget_from(child);
        if (YETTY_IS_ERR(child_res)) {
            yetty_ycore_error_destroy(child_res.error);
            break;
        }
        widget->first_child = child_res.value->next_sibling;
        struct yetty_ycore_void_result destroy_res = yetty_ygui2_widget_destroy(child);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
    }
    struct yetty_ycore_void_result cleanup_res = yetty_ygui2_widget_cleanup(obj);
    if (YETTY_IS_ERR(cleanup_res)) {
        yetty_ycore_error_destroy(cleanup_res.error);
    }
    free(obj);
    return YETTY_OK_VOID();
}

/* Cross-class within-module: the framework owns focus/capture and must
 * forget any pointer into a subtree that is about to be freed. */
struct yetty_ycore_void_result yetty_ygui2_framework_forget_subtree(
    struct yetty_yclass_object *framework_obj, struct yetty_yclass_object *widget_obj);

/* Remove a widget from the live tree: unlink from the parent chain, mark
 * the parent structure-dirty (the next emit reopens it without this
 * subtree), invalidate focus/capture pointers into the subtree, run the
 * cleanup chain, free. The roots cannot be removed (dispose owns them). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_widget_remove(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_widget_ptr_result data_res = yetty_ygui2_widget_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 widget_remove: slice");
    struct yetty_ygui2_widget *widget = data_res.value;
    if (!widget->parent) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 widget_remove: cannot remove a root");
    }
    struct yetty_ygui2_widget_ptr_result parent_res = yetty_ygui2_widget_from(widget->parent);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, parent_res, "ygui2 widget_remove: parent slice");
    struct yetty_ygui2_widget *parent = parent_res.value;
    if (parent->first_child == obj) {
        parent->first_child = widget->next_sibling;
    } else {
        struct yetty_yclass_object *walk = parent->first_child;
        while (walk) {
            struct yetty_ygui2_widget_ptr_result walk_res = yetty_ygui2_widget_from(walk);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, walk_res, "ygui2 widget_remove: sibling");
            if (walk_res.value->next_sibling == obj) {
                walk_res.value->next_sibling = widget->next_sibling;
                break;
            }
            walk = walk_res.value->next_sibling;
        }
    }
    parent->dirty_structure = 1;
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_widget_framework_obj(obj);
    if (YETTY_IS_OK(framework_res) && framework_res.value) {
        struct yetty_ycore_void_result forget_res =
            yetty_ygui2_framework_forget_subtree(framework_res.value, obj);
        if (YETTY_IS_ERR(forget_res)) {
            yetty_ycore_error_destroy(forget_res.error);
        }
    } else if (YETTY_IS_ERR(framework_res)) {
        yetty_ycore_error_destroy(framework_res.error);
    }
    widget->parent = NULL;
    widget->next_sibling = NULL;
    return yetty_ygui2_widget_destroy(obj);
}

#include "yetty/gen/impl/ygui2/widget.c"
