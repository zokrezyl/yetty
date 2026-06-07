/* GENERATED — do not edit. */
/* Public interface for regular class(es) `widget` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGET_H
#define YETTY_YCLASSGEN_YGUI_WIDGET_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_widget_class_get(void);

struct yetty_ygui_object;
struct yetty_ygui_widget_data;
YETTY_YRESULT_DECLARE(yetty_ygui_widget_data_ptr, struct yetty_ygui_widget_data *);
struct yetty_ygui_widget_data_ptr_result yetty_ygui_widget_data(struct yetty_ygui_object *obj);

/* Header-destined content for the generated widget.h (skipped by the real build, which takes it from that header). */
/*-----------------------------------------------------------------------------
 * Flex / CSS layout — the field set the flex layout pass in layout.c
 * consumes. Apps author these values directly or via apply_css.
 *---------------------------------------------------------------------------*/
enum yetty_ygui_flex_direction {
    YETTY_YGUI_FLEX_ROW = 0,
    YETTY_YGUI_FLEX_COLUMN = 1,
};

enum yetty_ygui_flex_justify {
    YETTY_YGUI_JUSTIFY_START = 0,
    YETTY_YGUI_JUSTIFY_CENTER,
    YETTY_YGUI_JUSTIFY_END,
    YETTY_YGUI_JUSTIFY_SPACE_BETWEEN,
};

enum yetty_ygui_flex_align {
    YETTY_YGUI_ALIGN_START = 0,
    YETTY_YGUI_ALIGN_CENTER,
    YETTY_YGUI_ALIGN_END,
    YETTY_YGUI_ALIGN_STRETCH,
};

struct yetty_ygui_layout {
    enum yetty_ygui_flex_direction direction;
    enum yetty_ygui_flex_justify justify;
    enum yetty_ygui_flex_align align;
    float gap;
    float padding_top;
    float padding_right;
    float padding_bottom;
    float padding_left;
    /* Authored intrinsic size — used as the starting point for the main-
     * axis distribution and as the cross-axis size unless overridden by
     * align=stretch. -1.0f means "unset" (the layout treats as 0 on the
     * main axis, parent-cross-content on the cross axis). */
    float width;
    float height;
    /* flex factors — main-axis space distribution. */
    float flex_grow;
    float flex_shrink;
    /* min / max constraints; -1.0f means "unset". */
    float min_width;
    float max_width;
    float min_height;
    float max_height;
    /* Absolute positioning. When `absolute` is non-zero, the layout
     * pass skips this widget in its parent's flex flow and instead
     * places it at (parent_content_min.x + pos_x, parent_content_min.y +
     * pos_y) with size (width, height). Used by overlays (popup_menu,
     * dialog) that need to float over their siblings. */
    int absolute;
    float pos_x;
    float pos_y;
    /* When non-zero, this widget and its subtree are excluded from both
     * the layout pass (consumes no main-axis space, contributes no flex
     * sum) and the emit walk (no container record, no body prims). Used
     * by collapsible containers (collapsing_header, tree_node) to fold
     * away their children when closed without disturbing the children's
     * authored sizes — restoring visibility is a single flag flip. */
    int hidden;
};

/* Initial layout — all enums zeroed (row / start / start), no gap, no
 * padding, no flex grow/shrink, unbounded min/max. */
struct yetty_ygui_layout yetty_ygui_layout_default(void);

/*-----------------------------------------------------------------------------
 * Widget geometry / layout setters. Setters take effect on the next
 * engine emit; the engine flags the widget dirty.
 *---------------------------------------------------------------------------*/
struct yetty_ycore_void_result yetty_ygui_widget_set_rect(struct yetty_ygui_object *obj,
                                                          struct yetty_ycore_rectangle rect);

struct yetty_ycore_rectangle yetty_ygui_widget_rect(const struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_widget_layout_set(struct yetty_ygui_object *obj,
                                                            const struct yetty_ygui_layout *layout);

const struct yetty_ygui_layout *yetty_ygui_widget_layout_get(const struct yetty_ygui_object *obj);

/*-----------------------------------------------------------------------------
 * Convenience geometry / visibility setters — thin wrappers over the
 * layout struct, exposing geometry/visibility as first-class widget
 * calls. Each marks the widget dirty.
 *---------------------------------------------------------------------------*/
/* Toggle the widget (and its subtree) in/out of layout + paint via the
 * layout `hidden` flag. */
struct yetty_ycore_void_result yetty_ygui_widget_set_visible(struct yetty_ygui_object *obj,
                                                             int visible);
int yetty_ygui_widget_is_visible(const struct yetty_ygui_object *obj);

/* Author the widget's main/cross size (width, height). */
struct yetty_ycore_void_result yetty_ygui_widget_set_size(struct yetty_ygui_object *obj, float w,
                                                          float h);

/* Place the widget at an absolute (pos_x, pos_y) inside its parent's
 * content box (sets layout.absolute). */
struct yetty_ycore_void_result yetty_ygui_widget_set_position(struct yetty_ygui_object *obj,
                                                              float x, float y);

/* Promote this widget to its own receiver-side child figure of `kind`
 * (e.g. YETTY_YFIGURE_KIND_YGRID) stacked at `z`. The whole subtree
 * then paints into that figure's own draw list instead of the shared
 * chrome ygrid — giving floating windows / menus an independent z and
 * damage region. kind=0 reverts to inline. */
struct yetty_ycore_void_result yetty_ygui_widget_make_figure(struct yetty_ygui_object *obj,
                                                             uint32_t kind, int32_t z);

/* Update only the figure z (for raise-on-click). No-op if unchanged. */
struct yetty_ycore_void_result yetty_ygui_widget_set_figure_z(struct yetty_ygui_object *obj,
                                                              int32_t z);

uint32_t yetty_ygui_widget_figure_kind(const struct yetty_ygui_object *obj);
int32_t yetty_ygui_widget_figure_z(const struct yetty_ygui_object *obj);

/* Mark this widget a floating overlay (dialog / debug window): a press
 * anywhere inside it moves it to the end of its parent's child list, so
 * it paints last (front) and wins the hit-test — click-to-front, no
 * figures involved. */
struct yetty_ycore_void_result yetty_ygui_widget_set_floating(struct yetty_ygui_object *obj,
                                                              int floating);
int yetty_ygui_widget_is_floating(const struct yetty_ygui_object *obj);

/* Apply a small CSS-like declaration string to the widget's layout.
 * Supported properties (others ignored): width, height, flex,
 * flex-grow, flex-shrink, gap, padding, align-items, justify-content,
 * (flex-)direction, align-self. Values may carry a trailing "px".
 * Eases the yui port, which authored layout via CSS strings. */
struct yetty_ycore_void_result yetty_ygui_widget_apply_css(struct yetty_ygui_object *obj,
                                                           const char *css);

/* Background fill (packed 0xAABBGGRR; 0 = transparent). Painted by the
 * primitive_widget base before the widget's own paint. */
struct yetty_ycore_void_result yetty_ygui_widget_set_bg_color(struct yetty_ygui_object *obj,
                                                              uint32_t color);

/* Current background fill (0 = transparent). Used by primitive_widget. */
uint32_t yetty_ygui_widget_bg(const struct yetty_ygui_object *obj);

/*-----------------------------------------------------------------------------
 * Flex layout pass.
 *
 * Walks the widget tree top-down assigning rects according to each
 * widget's layout struct + the parent's content box. Call this before
 * yetty_ygui_framework_emit when the tree's geometry might have changed
 * (set_rect, set_layout, add / del). The engine's emit pass uses the
 * rects assigned here.
 *
 * `root_rect` is the absolute pixel rect the root widget should fit
 * into — typically the engine's viewport.
 *---------------------------------------------------------------------------*/
struct yetty_ycore_void_result yetty_ygui_layout_compute(struct yetty_ygui_object *root,
                                                         struct yetty_ycore_rectangle root_rect);

/*-----------------------------------------------------------------------------
 * Wire emission — two explicit passes. The engine invokes these via
 * dispatch as it walks the widget tree.
 *
 * Defaults (provided by the base widget class):
 *
 *   emit_container — if figure_kind != 0, writes CREATE_CHILD /
 *     SET_CHILD_RECT / DELETE_CHILD admin records into the engine's
 *     container record stream using obj->id as the figure id. Otherwise
 *     no-op. Subclasses with figure_kind==0 do not override.
 *
 *   emit_body — if figure_kind == 0, opens CMD_GROUP(obj->id, ...) in
 *     the engine's ygrid, calls the widget_paint hook to write prims,
 *     closes the group. If figure_kind != 0, the widget's own override
 *     writes figure-specific body bytes into the per-figure stream.
 *---------------------------------------------------------------------------*/
/* Method public-stub declarations — emit_container, emit_body,
 * widget_paint, widget_on_press, widget_on_release, widget_on_motion
 * — come from the codegen-emitted module-wide methods.h pulled
 * in by class.h's include chain. The yclass slot signature is
 * `(struct yetty_yclass_ctx *, struct yetty_yclass_object *, …)`. */

#endif
