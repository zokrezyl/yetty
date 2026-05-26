/*
 * ygui-widget.h — base widget class.
 *
 * Every widget class inherits from yetty_ygui_widget_class_get(). The
 * base class owns geometry (x, y, w, h), flex layout config, the dirty
 * flag (already on the object), and the two virtual wire-emit methods
 * (emit_container / emit_body) with sensible defaults driven by
 * `figure_kind`.
 */
#ifndef YETTY_YGUI_WIDGET_H
#define YETTY_YGUI_WIDGET_H

#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward decl: emit context passed to the two emit methods. */
struct yetty_ygui_emit_ctx;

/* Base widget class accessor. */
const struct yetty_ygui_class *yetty_ygui_widget_class_get(void);

/*-----------------------------------------------------------------------------
 * Flex / CSS layout — minimal field set that ports cleanly from
 * ygui-old's yetty_ygui_layout. The full algorithm will be added in a
 * follow-up phase; this phase only defines the storage shape so apps
 * can author values today and have them flow once the layout pass lands.
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
YETTY_YGUI_DECLARE_METHOD(yetty_ygui_widget_emit_container, struct yetty_ycore_void_result,
                          (struct yetty_ygui_object * obj, struct yetty_ygui_emit_ctx *ctx));

YETTY_YGUI_DECLARE_METHOD(yetty_ygui_widget_emit_body, struct yetty_ycore_void_result,
                          (struct yetty_ygui_object * obj, struct yetty_ygui_emit_ctx *ctx));

/* Inner paint hook — chrome widgets (figure_kind == 0) override this to
 * write SDF / glyph prim records into the ygrid body. The base widget
 * provides a no-op default. The default emit_body wraps the call in a
 * CMD_GROUP open/close for the widget's id. */
YETTY_YGUI_DECLARE_METHOD(yetty_ygui_widget_paint, struct yetty_ycore_void_result,
                          (struct yetty_ygui_object * obj, struct yetty_ygui_emit_ctx *ctx));

/*-----------------------------------------------------------------------------
 * Input — pointer and key events dispatched against the leaf widget the
 * engine hit-tests. Returns 1 = consumed (stop propagation), 0 = pass
 * through to parent.
 *---------------------------------------------------------------------------*/
YETTY_YGUI_DECLARE_METHOD(yetty_ygui_widget_on_press, struct yetty_ycore_int_result,
                          (struct yetty_ygui_object * obj, float x, float y, int button));

YETTY_YGUI_DECLARE_METHOD(yetty_ygui_widget_on_release, struct yetty_ycore_int_result,
                          (struct yetty_ygui_object * obj, float x, float y, int button));

YETTY_YGUI_DECLARE_METHOD(yetty_ygui_widget_on_motion, struct yetty_ycore_int_result,
                          (struct yetty_ygui_object * obj, float x, float y));

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_WIDGET_H */
