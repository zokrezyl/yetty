/*
 * ygui-ynode.c — one node in a ynodes editor.
 *
 * A ynode is a vbox: the node paints its own rounded frame, a title bar
 * and the input/output pins on its left/right edges; the vbox body lays
 * out whatever child widgets the app adds below the title. The node owns
 * a graph-space rect (gx, gy, gw, gh); the parent ynodes editor maps it
 * to a screen rect through its pan/zoom view, which the node applies to
 * its own (absolute) layout in yetty_ygui_ynode_reflow.
 *
 * Interaction (the node implements press/motion/release itself rather
 * than borrowing the draggable mixin, because a press has to mean either
 * "move this node" or "start a link from this pin" depending on where it
 * lands):
 *   - press on a pin     → begin a pending link in the editor
 *   - press on chrome    → begin moving the node
 *   - press on a child   → never reaches the node (the child consumes it)
 */

#include "../internal.h"
#include "yetty/gen/impl/ygui/widget.h"

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * ynode.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_ynode_ptr, struct yetty_ygui_ynode *);
struct yetty_yclass_ptr_result yetty_ygui_ynode_class_get(void);
struct yetty_ygui_ynode_ptr_result yetty_ygui_ynode_from(struct yetty_yclass_object *obj);
#include "paint-helpers.h"

#include "yetty/gen/impl/ygui/primitive-widget.h"
#include "yetty/gen/impl/ygui/widgets/vbox.h"
#include "yetty/gen/impl/ygui/widgets/ynodes.h"

#include <stdlib.h>
#include <string.h>

/* Brand palette, packed 0xAABBGGRR (the encoding pack_rgba / add_* use). */
#define YNODE_BG 0xFF1F1A14u         /* BRAND_BG_LIFTED  #141A1F */
#define YNODE_TITLE_BG 0xFF2C261Eu   /* BRAND_BG_ROW     #1E262C */
#define YNODE_BORDER 0xFF474A36u     /* BRAND_BORDER     #364A47 */
#define YNODE_BORDER_SEL 0xFFA5C574u /* BRAND_ACCENT_BRIGHT #74C5A5 */
#define YNODE_TITLE_FG 0xFFE4E5E0u   /* BRAND_TEXT_PRIMARY  #E0E5E4 */
#define YNODE_PIN 0xFF92A86Bu        /* BRAND_ACCENT     #6BA892 */
#define YNODE_GRIP 0xFF626155u       /* BRAND_TEXT_MUTED #556162 */

#define YNODE_TITLE_H 26.0f
#define YNODE_PIN_MARGIN 12.0f
#define YNODE_PIN_R 5.0f
#define YNODE_PIN_HIT_SLOP 6.0f
#define YNODE_CORNER 6.0f
#define YNODE_TITLE_FONT 13.0f
#define YNODE_RESIZE_GRIP 16.0f
#define YNODE_MIN_W 90.0f
#define YNODE_MIN_H 64.0f

/* Drag modes for the node's own pointer handling. */
enum ynode_drag_mode {
    YNODE_DRAG_NONE = 0,
    YNODE_DRAG_MOVE,
    YNODE_DRAG_LINK,
    YNODE_DRAG_RESIZE,
};

struct ynode_pin {
    char *name;
    uint32_t color;
};

struct YETTY_ANNOTATE("class@ygui:ynode") YETTY_ANNOTATE("parent@ygui:vbox") yetty_ygui_ynode {
    char *title;
    float gx, gy; /* graph-space top-left */
    float gw, gh; /* graph-space size */

    struct ynode_pin *inputs;
    size_t in_count, in_cap;
    struct ynode_pin *outputs;
    size_t out_count, out_cap;

    enum ynode_drag_mode drag_mode;
    int resize_corner;    /* while resizing: 0 = bottom-right, 1 = bottom-left */
    float last_x, last_y; /* last pointer sample (screen) while moving */
    int selected;
};

YETTY_EXTERNAL_CALLBACK
static const struct yetty_yclass *ynode_class(void)
{
    return yetty_ygui_ynode_class_get().value;
}

/* The owning editor: a node's parent is its ynodes editor. Returns NULL
 * if the parent is missing or is not a ynodes instance (a node used
 * outside an editor — every interactive helper degrades to a no-op). */
YETTY_EXTERNAL_CALLBACK
static struct yetty_yclass_object *ynode_editor(struct yetty_yclass_object *node)
{
    struct yetty_yclass_object_ptr_result parent_res = yetty_ygui_widget_parent(node);
    if (YETTY_IS_ERR(parent_res)) {
        yetty_ycore_error_destroy(parent_res.error);
        return NULL;
    }
    struct yetty_yclass_object *parent = parent_res.value;
    if (!parent) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_res = yetty_ygui_ynodes_class_get();
    if (YETTY_IS_ERR(class_res)) {
        yetty_ycore_error_destroy(class_res.error);
        return NULL;
    }
    return parent->klass == class_res.value ? parent : NULL;
}

static struct yetty_ycore_float_result ynode_view_zoom(struct yetty_yclass_object *node)
{
    struct yetty_yclass_object *editor = ynode_editor(node);
    if (!editor) {
        return YETTY_OK(yetty_ycore_float, 1.0f);
    }
    return yetty_ygui_ynodes_zoom(editor);
}

/*-----------------------------------------------------------------------------
 * Lifecycle.
 *---------------------------------------------------------------------------*/
YETTY_ANNOTATE("override@ygui:ynode:constructor")
static struct yetty_ycore_void_result ynode_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, ynode_class(), (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ynode_constructor: super");

    struct yetty_ygui_ynode_ptr_result data_res = yetty_ygui_ynode_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ynode_constructor: downcast");
    struct yetty_ygui_ynode *d = data_res.value;
    d->title = NULL;
    d->gx = d->gy = 0.0f;
    d->gw = 180.0f;
    d->gh = 120.0f;
    d->inputs = NULL;
    d->in_count = d->in_cap = 0;
    d->outputs = NULL;
    d->out_count = d->out_cap = 0;
    d->drag_mode = YNODE_DRAG_NONE;
    d->resize_corner = 0;
    d->last_x = d->last_y = 0.0f;
    d->selected = 0;

    /* The editor positions the node absolutely; the vbox body lays out
     * child widgets below the title bar, inset from the pin gutters. */
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "ynode_constructor: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.absolute = 1;
    l.width = d->gw;
    l.height = d->gh;
    l.padding_top = YNODE_TITLE_H + 6.0f;
    l.padding_left = YNODE_PIN_MARGIN;
    l.padding_right = YNODE_PIN_MARGIN;
    l.padding_bottom = 10.0f;
    l.gap = 6.0f;
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &l);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "ynode_constructor: layout");
    return yetty_ygui_widget_set_dirty(obj);
}

static void ynode_free_pins(struct ynode_pin *pins, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        free(pins[i].name);
    }
    free(pins);
}

YETTY_ANNOTATE("override@ygui:ynode:destructor")
static struct yetty_ycore_void_result ynode_destructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_ynode_ptr_result data_res = yetty_ygui_ynode_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ynode_destructor: downcast");
    struct yetty_ygui_ynode *d = data_res.value;

    /* Drop our links while the parent editor is still reachable (yetty_ygui_del
     * runs destructors before it detaches the object from its parent). */
    struct yetty_yclass_object *editor = ynode_editor(obj);
    if (editor) {
        struct yetty_ycore_void_result dr = yetty_ygui_ynodes_drop_links_for(editor, obj);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
    }

    free(d->title);
    d->title = NULL;
    ynode_free_pins(d->inputs, d->in_count);
    ynode_free_pins(d->outputs, d->out_count);
    d->inputs = d->outputs = NULL;
    d->in_count = d->out_count = 0;

    return yetty_ygui_super_void(obj, ynode_class(),
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

/*-----------------------------------------------------------------------------
 * Geometry — graph rect ↔ screen layout, pin positions.
 *---------------------------------------------------------------------------*/
static struct yetty_ycore_void_result ynode_apply_layout(struct yetty_yclass_object *node,
                                                         struct yetty_ygui_ynode *d, float pan_x,
                                                         float pan_y, float zoom)
{
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "ynode_apply_layout: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.absolute = 1;
    l.pos_x = d->gx * zoom + pan_x;
    l.pos_y = d->gy * zoom + pan_y;
    l.width = d->gw * zoom;
    l.height = d->gh * zoom;
    return yetty_ygui_widget_layout_set(node, &l);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_ynode_reflow(struct yetty_yclass_object *node)
{
    if (!node) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynode_reflow: NULL node");
    }
    struct yetty_ygui_ynode_ptr_result data_res = yetty_ygui_ynode_from(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "yetty_ygui_ynode_reflow: downcast");
    struct yetty_ygui_ynode *d = data_res.value;
    float pan_x = 0.0f, pan_y = 0.0f, zoom = 1.0f;
    struct yetty_yclass_object *editor = ynode_editor(node);
    if (editor) {
        struct yetty_ycore_void_result view_res =
            yetty_ygui_ynodes_view(editor, &pan_x, &pan_y, &zoom);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, view_res, "yetty_ygui_ynode_reflow: view");
    }
    return ynode_apply_layout(node, d, pan_x, pan_y, zoom);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_ynode_pin_pos(const struct yetty_yclass_object *node,
                                                       int output, int index, float *x, float *y)
{
    if (!node) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ygui_ynode_ptr_result data_res =
        yetty_ygui_ynode_from((struct yetty_yclass_object *)node);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "yetty_ygui_ynode_pin_pos: downcast");
    struct yetty_ygui_ynode *d = data_res.value;
    size_t count = output ? d->out_count : d->in_count;
    if (index < 0 || (size_t)index >= count) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, rect_res, "yetty_ygui_ynode_pin_pos: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    struct yetty_ycore_float_result zoom_res = ynode_view_zoom((struct yetty_yclass_object *)node);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, zoom_res, "yetty_ygui_ynode_pin_pos: zoom");
    float zoom = zoom_res.value;
    float title_h = YNODE_TITLE_H * zoom;
    float body_min = r.min.y + title_h;
    float body_h = r.max.y - body_min;
    if (body_h < 1.0f) {
        body_h = 1.0f;
    }
    if (x) {
        *x = output ? r.max.x : r.min.x;
    }
    if (y) {
        *y = body_min + ((float)index + 0.5f) / (float)count * body_h;
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_ynode_pin_at(const struct yetty_yclass_object *node,
                                                      float x, float y, int *output, int *index)
{
    if (!node) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ygui_ynode_ptr_result data_res =
        yetty_ygui_ynode_from((struct yetty_yclass_object *)node);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "yetty_ygui_ynode_pin_at: downcast");
    struct yetty_ygui_ynode *d = data_res.value;
    struct yetty_ycore_float_result zoom_res = ynode_view_zoom((struct yetty_yclass_object *)node);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, zoom_res, "yetty_ygui_ynode_pin_at: zoom");
    float zoom = zoom_res.value;
    float hit = YNODE_PIN_R * zoom + YNODE_PIN_HIT_SLOP;
    for (int side = 0; side < 2; side++) {
        size_t count = side ? d->out_count : d->in_count;
        for (size_t i = 0; i < count; i++) {
            float px = 0.0f, py = 0.0f;
            struct yetty_ycore_int_result pin_pos_res =
                yetty_ygui_ynode_pin_pos(node, side, (int)i, &px, &py);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, pin_pos_res, "yetty_ygui_ynode_pin_at: pin_pos");
            if (!pin_pos_res.value) {
                continue;
            }
            float dx = x - px, dy = y - py;
            if (dx * dx + dy * dy <= hit * hit) {
                if (output) {
                    *output = side;
                }
                if (index) {
                    *index = (int)i;
                }
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

/* Lower-corner resize-grip hit-test: 0 = bottom-right, 1 = bottom-left,
 * -1 = neither. */
static struct yetty_ycore_int_result ynode_resize_grip_at(struct yetty_yclass_object *node, float x,
                                                          float y)
{
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, rect_res, "ynode_resize_grip_at: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    struct yetty_ycore_float_result zoom_res = ynode_view_zoom(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, zoom_res, "ynode_resize_grip_at: zoom");
    float g = YNODE_RESIZE_GRIP * zoom_res.value;
    if (y < r.max.y - g || y > r.max.y) {
        return YETTY_OK(yetty_ycore_int, -1);
    }
    if (x >= r.max.x - g && x <= r.max.x) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    if (x >= r.min.x && x <= r.min.x + g) {
        return YETTY_OK(yetty_ycore_int, 1);
    }
    return YETTY_OK(yetty_ycore_int, -1);
}

/*-----------------------------------------------------------------------------
 * Paint — frame, title bar, title text, pins. Child widgets paint after
 * (the engine's pre-order walk recurses into them).
 *---------------------------------------------------------------------------*/
YETTY_ANNOTATE("override@ygui:ynode:widget_paint")
static struct yetty_ycore_void_result ynode_paint(struct yetty_yclass_object *yclass_obj,
                                                  struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "ynode_paint: NULL ctx");
    }
    struct yetty_ygui_ynode_ptr_result data_res = yetty_ygui_ynode_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ynode_paint: downcast");
    struct yetty_ygui_ynode *d = data_res.value;
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ynode_paint: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_float_result zoom_res = ynode_view_zoom(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, zoom_res, "ynode_paint: zoom");
    float zoom = zoom_res.value;
    float corner = YNODE_CORNER * zoom;
    float title_h = YNODE_TITLE_H * zoom;

    /* Body + frame. */
    struct yetty_ysdf_rounded_box frame = {
        .center_x = r.min.x + w * 0.5f,
        .center_y = r.min.y + h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
        .radius_top_right = corner,
        .radius_bottom_right = corner,
        .radius_top_left = corner,
        .radius_bottom_left = corner,
    };
    uint32_t border = d->selected ? YNODE_BORDER_SEL : YNODE_BORDER;
    struct yetty_ycore_void_result result_338 = yetty_ydraw_drawable_list_add_cmd_add_rounded_box(
        ctx->ygrid_drawable_list, 0, 0, YNODE_BG, border, d->selected ? 2.0f : 1.0f, &frame);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_338, "ynode_paint: frame");

    /* Title bar — a filled strip across the top. Square bottom corners,
     * rounded top to match the frame. */
    if (title_h > 0.0f) {
        struct yetty_ysdf_rounded_box title = {
            .center_x = r.min.x + w * 0.5f,
            .center_y = r.min.y + title_h * 0.5f,
            .half_width = w * 0.5f,
            .half_height = title_h * 0.5f,
            /* The rounded-box SDF samples in a y-up frame, so its "bottom"
             * radii land on the screen-TOP corners (and vice-versa). To
             * round the title's visible top edge we set the bottom radii;
             * the square pair sits at the title/body seam, hidden. */
            .radius_top_right = 0.0f,
            .radius_bottom_right = corner,
            .radius_top_left = 0.0f,
            .radius_bottom_left = corner,
        };
        struct yetty_ycore_void_result result_361 =
            yetty_ydraw_drawable_list_add_cmd_add_rounded_box(ctx->ygrid_drawable_list, 0, 0,
                                                              YNODE_TITLE_BG, 0, 0.0f, &title);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_361, "ynode_paint: title bar");
        if (d->title && d->title[0]) {
            float font_size = YNODE_TITLE_FONT * zoom;
            float ty = r.min.y + (title_h + font_size) * 0.5f - 2.0f;
            struct yetty_ycore_void_result result_368 =
                yguix_text(ctx, d->title, r.min.x + 10.0f * zoom, ty, font_size, YNODE_TITLE_FG);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, result_368, "ynode_paint: title text");
        }
    }

    /* Pins. */
    float pin_r = YNODE_PIN_R * zoom;
    for (int side = 0; side < 2; side++) {
        size_t count = side ? d->out_count : d->in_count;
        struct ynode_pin *pins = side ? d->outputs : d->inputs;
        for (size_t i = 0; i < count; i++) {
            float px = 0.0f, py = 0.0f;
            struct yetty_ycore_int_result pin_pos_res =
                yetty_ygui_ynode_pin_pos(obj, side, (int)i, &px, &py);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, pin_pos_res, "ynode_paint: pin_pos");
            if (!pin_pos_res.value) {
                continue;
            }
            struct yetty_ycore_void_result result_385 =
                yguix_circle(ctx, px, py, pin_r, pins[i].color ? pins[i].color : YNODE_PIN);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, result_385, "ynode_paint: pin");
        }
    }

    /* Resize grips — two diagonal hatch strokes in each lower corner. */
    float grip = YNODE_RESIZE_GRIP * zoom;
    float grip_w = 1.5f * zoom;
    if (grip_w < 1.0f) {
        grip_w = 1.0f;
    }
    for (int left = 0; left < 2; left++) {
        float xc = left ? r.min.x : r.max.x;
        float sx = left ? 1.0f : -1.0f;
        for (int i = 0; i < 2; i++) {
            float far = i == 0 ? 0.58f : 0.92f;
            struct yetty_ysdf_segment seg = {
                .start_x = xc + sx * 0.18f * grip,
                .start_y = r.max.y - far * grip,
                .end_x = xc + sx * far * grip,
                .end_y = r.max.y - 0.18f * grip,
            };
            struct yetty_ycore_void_result result_409 =
                yetty_ydraw_drawable_list_add_cmd_add_segment(ctx->ygrid_drawable_list, 0, 0, 0u,
                                                              YNODE_GRIP, grip_w, &seg);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, result_409, "ynode_paint: grip");
        }
    }
    return YETTY_OK_VOID();
}

/*-----------------------------------------------------------------------------
 * Pointer handling — move the node, or start/drive a link off a pin.
 *---------------------------------------------------------------------------*/
YETTY_ANNOTATE("override@ygui:ynode:widget_on_press")
static struct yetty_ycore_int_result ynode_on_press(struct yetty_yclass_object *yclass_obj, float x,
                                                    float y, int button)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_ynode_ptr_result data_res = yetty_ygui_ynode_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ynode_on_press: downcast");
    struct yetty_ygui_ynode *d = data_res.value;
    struct yetty_yclass_object *editor = ynode_editor(obj);

    /* A press while the editor's context menu is open just dismisses it. */
    if (editor) {
        struct yetty_ycore_int_result menu_open_r = yetty_ygui_ynodes_menu_is_open(editor);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, menu_open_r, "ynode_on_press: menu_is_open");
        if (menu_open_r.value) {
            struct yetty_ycore_void_result cr = yetty_ygui_ynodes_close_menu(editor);
            if (YETTY_IS_ERR(cr)) {
                return YETTY_ERR(yetty_ycore_int, "ynode_on_press: close menu", cr);
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }
    /* Right-press on node chrome → node context menu. */
    if (button == 1 && editor) {
        struct yetty_ycore_void_result mr = yetty_ygui_ynodes_open_node_menu(editor, obj, x, y);
        if (YETTY_IS_ERR(mr)) {
            return YETTY_ERR(yetty_ycore_int, "ynode_on_press: node menu", mr);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    int side = 0, index = 0;
    int pin_hit = 0;
    if (editor) {
        struct yetty_ycore_int_result pin_at_res =
            yetty_ygui_ynode_pin_at(obj, x, y, &side, &index);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, pin_at_res, "ynode_on_press: pin_at");
        pin_hit = pin_at_res.value;
    }
    if (pin_hit) {
        d->drag_mode = YNODE_DRAG_LINK;
        struct yetty_ycore_void_result br =
            yetty_ygui_ynodes_begin_link(editor, obj, index, side, x, y);
        if (YETTY_IS_ERR(br)) {
            return YETTY_ERR(yetty_ycore_int, "ynode_on_press: begin_link", br);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Lower-corner grip → resize. */
    struct yetty_ycore_int_result grip_res = ynode_resize_grip_at(obj, x, y);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, grip_res, "ynode_on_press: resize_grip_at");
    int grip = grip_res.value;
    if (grip >= 0) {
        d->drag_mode = YNODE_DRAG_RESIZE;
        d->resize_corner = grip;
        d->last_x = x;
        d->last_y = y;
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Chrome press → move the node. */
    d->drag_mode = YNODE_DRAG_MOVE;
    d->last_x = x;
    d->last_y = y;
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("override@ygui:ynode:widget_on_motion")
static struct yetty_ycore_int_result ynode_on_motion(struct yetty_yclass_object *yclass_obj,
                                                     float x, float y)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_ynode_ptr_result data_res = yetty_ygui_ynode_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ynode_on_motion: downcast");
    struct yetty_ygui_ynode *d = data_res.value;
    struct yetty_yclass_object *editor = ynode_editor(obj);

    if (d->drag_mode == YNODE_DRAG_LINK) {
        if (editor) {
            struct yetty_ycore_void_result ur = yetty_ygui_ynodes_update_link(editor, x, y);
            if (YETTY_IS_ERR(ur)) {
                return YETTY_ERR(yetty_ycore_int, "ynode_on_motion: update_link", ur);
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    if (d->drag_mode == YNODE_DRAG_RESIZE) {
        float zoom = 1.0f;
        if (editor) {
            struct yetty_ycore_float_result zoom_res = yetty_ygui_ynodes_zoom(editor);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, zoom_res, "ynode_on_motion: resize zoom");
            zoom = zoom_res.value;
        }
        if (zoom <= 0.0f) {
            zoom = 1.0f;
        }
        float dxg = (x - d->last_x) / zoom;
        float dyg = (y - d->last_y) / zoom;
        d->last_x = x;
        d->last_y = y;
        if (d->resize_corner == 1) {
            /* Bottom-left: the left edge tracks the cursor (top-right anchored).
             * Cap the rightward drag so width never shrinks below the minimum. */
            if (dxg > d->gw - YNODE_MIN_W) {
                dxg = d->gw - YNODE_MIN_W;
            }
            d->gx += dxg;
            d->gw -= dxg;
        } else {
            /* Bottom-right: width tracks the cursor (top-left anchored). */
            d->gw += dxg;
            if (d->gw < YNODE_MIN_W) {
                d->gw = YNODE_MIN_W;
            }
        }
        d->gh += dyg;
        if (d->gh < YNODE_MIN_H) {
            d->gh = YNODE_MIN_H;
        }
        struct yetty_ycore_void_result rr = yetty_ygui_ynode_reflow(obj);
        if (YETTY_IS_ERR(rr)) {
            return YETTY_ERR(yetty_ycore_int, "ynode_on_motion: resize reflow", rr);
        }
        if (editor) {
            struct yetty_ycore_void_result er = yetty_ygui_widget_set_dirty(editor);
            if (YETTY_IS_ERR(er)) {
                return YETTY_ERR(yetty_ycore_int, "ynode_on_motion: resize dirty", er);
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    if (d->drag_mode == YNODE_DRAG_MOVE) {
        float zoom = 1.0f;
        if (editor) {
            struct yetty_ycore_float_result zoom_res = yetty_ygui_ynodes_zoom(editor);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, zoom_res, "ynode_on_motion: move zoom");
            zoom = zoom_res.value;
        }
        if (zoom <= 0.0f) {
            zoom = 1.0f;
        }
        d->gx += (x - d->last_x) / zoom;
        d->gy += (y - d->last_y) / zoom;
        d->last_x = x;
        d->last_y = y;
        struct yetty_ycore_void_result rr = yetty_ygui_ynode_reflow(obj);
        if (YETTY_IS_ERR(rr)) {
            return YETTY_ERR(yetty_ycore_int, "ynode_on_motion: reflow", rr);
        }
        /* Marking the editor dirty redraws the links that follow the node;
         * the node's own move is picked up by the next layout pass. */
        if (editor) {
            struct yetty_ycore_void_result er = yetty_ygui_widget_set_dirty(editor);
            if (YETTY_IS_ERR(er)) {
                return YETTY_ERR(yetty_ycore_int, "ynode_on_motion: dirty", er);
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

YETTY_ANNOTATE("override@ygui:ynode:widget_on_release")
static struct yetty_ycore_int_result ynode_on_release(struct yetty_yclass_object *yclass_obj,
                                                      float x, float y, int button)
{
    (void)button;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_ynode_ptr_result data_res = yetty_ygui_ynode_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ynode_on_release: downcast");
    struct yetty_ygui_ynode *d = data_res.value;
    struct yetty_yclass_object *editor = ynode_editor(obj);

    enum ynode_drag_mode mode = d->drag_mode;
    d->drag_mode = YNODE_DRAG_NONE;

    if (mode == YNODE_DRAG_LINK && editor) {
        struct yetty_ycore_void_result er = yetty_ygui_ynodes_end_link(editor, x, y);
        if (YETTY_IS_ERR(er)) {
            return YETTY_ERR(yetty_ycore_int, "ynode_on_release: end_link", er);
        }
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

/*-----------------------------------------------------------------------------
 * App-facing API.
 *---------------------------------------------------------------------------*/
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_ynode_set_title(struct yetty_yclass_object *node,
                                                          const char *title)
{
    if (!node) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynode_set_title: NULL node");
    }
    struct yetty_ygui_ynode_ptr_result data_res = yetty_ygui_ynode_from(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "yetty_ygui_ynode_set_title: downcast");
    struct yetty_ygui_ynode *d = data_res.value;
    char *copy = NULL;
    if (title) {
        copy = strdup(title);
        if (!copy) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynode_set_title: oom");
        }
    }
    free(d->title);
    d->title = copy;
    return yetty_ygui_widget_set_dirty(node);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_ynode_set_graph_pos(struct yetty_yclass_object *node,
                                                              float gx, float gy)
{
    if (!node) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynode_set_graph_pos: NULL node");
    }
    struct yetty_ygui_ynode_ptr_result data_res = yetty_ygui_ynode_from(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "yetty_ygui_ynode_set_graph_pos: downcast");
    struct yetty_ygui_ynode *d = data_res.value;
    d->gx = gx;
    d->gy = gy;
    return yetty_ygui_ynode_reflow(node);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_ynode_graph_pos(const struct yetty_yclass_object *node,
                                                          float *gx, float *gy)
{
    if (!node) {
        if (gx) {
            *gx = 0.0f;
        }
        if (gy) {
            *gy = 0.0f;
        }
        return YETTY_OK_VOID();
    }
    struct yetty_ygui_ynode_ptr_result data_res =
        yetty_ygui_ynode_from((struct yetty_yclass_object *)node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "yetty_ygui_ynode_graph_pos: downcast");
    struct yetty_ygui_ynode *d = data_res.value;
    if (gx) {
        *gx = d->gx;
    }
    if (gy) {
        *gy = d->gy;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_ynode_set_graph_size(struct yetty_yclass_object *node,
                                                               float gw, float gh)
{
    if (!node) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_ynode_set_graph_size: NULL node");
    }
    struct yetty_ygui_ynode_ptr_result data_res = yetty_ygui_ynode_from(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "yetty_ygui_ynode_set_graph_size: downcast");
    struct yetty_ygui_ynode *d = data_res.value;
    d->gw = gw > 1.0f ? gw : 1.0f;
    d->gh = gh > 1.0f ? gh : 1.0f;
    return yetty_ygui_ynode_reflow(node);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_ynode_graph_size(const struct yetty_yclass_object *node,
                                                           float *gw, float *gh)
{
    if (!node) {
        if (gw) {
            *gw = 0.0f;
        }
        if (gh) {
            *gh = 0.0f;
        }
        return YETTY_OK_VOID();
    }
    struct yetty_ygui_ynode_ptr_result data_res =
        yetty_ygui_ynode_from((struct yetty_yclass_object *)node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "yetty_ygui_ynode_graph_size: downcast");
    struct yetty_ygui_ynode *d = data_res.value;
    if (gw) {
        *gw = d->gw;
    }
    if (gh) {
        *gh = d->gh;
    }
    return YETTY_OK_VOID();
}

static struct uint32_result ynode_add_pin(struct yetty_yclass_object *node, int output,
                                          const char *name)
{
    if (!node) {
        return YETTY_ERR(uint32, "yetty_ygui_ynode_add_pin: NULL node");
    }
    struct yetty_ygui_ynode_ptr_result data_res = yetty_ygui_ynode_from(node);
    YETTY_RETURN_IF_ERR(uint32, data_res, "yetty_ygui_ynode_add_pin: downcast");
    struct yetty_ygui_ynode *d = data_res.value;
    struct ynode_pin **arr = output ? &d->outputs : &d->inputs;
    size_t *count = output ? &d->out_count : &d->in_count;
    size_t *cap = output ? &d->out_cap : &d->in_cap;

    if (*count == *cap) {
        size_t new_cap = *cap ? *cap * 2 : 4;
        struct ynode_pin *grown = realloc(*arr, new_cap * sizeof(struct ynode_pin));
        if (!grown) {
            return YETTY_ERR(uint32, "yetty_ygui_ynode_add_pin: oom");
        }
        *arr = grown;
        *cap = new_cap;
    }
    char *copy = NULL;
    if (name) {
        copy = strdup(name);
        if (!copy) {
            return YETTY_ERR(uint32, "yetty_ygui_ynode_add_pin: oom (name)");
        }
    }
    (*arr)[*count].name = copy;
    (*arr)[*count].color = YNODE_PIN;
    uint32_t index = (uint32_t)*count;
    (*count)++;
    struct yetty_ycore_void_result dr = yetty_ygui_widget_set_dirty(node);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(uint32, "yetty_ygui_ynode_add_pin: dirty", dr);
    }
    return YETTY_OK(uint32, index);
}

YETTY_ANNOTATE("expose")
struct uint32_result yetty_ygui_ynode_add_input(struct yetty_yclass_object *node, const char *name)
{
    return ynode_add_pin(node, 0, name);
}

YETTY_ANNOTATE("expose")
struct uint32_result yetty_ygui_ynode_add_output(struct yetty_yclass_object *node, const char *name)
{
    return ynode_add_pin(node, 1, name);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_ynode_input_count(const struct yetty_yclass_object *node)
{
    if (!node) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ygui_ynode_ptr_result data_res =
        yetty_ygui_ynode_from((struct yetty_yclass_object *)node);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "yetty_ygui_ynode_input_count: downcast");
    return YETTY_OK(yetty_ycore_int, (int)data_res.value->in_count);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_ynode_output_count(const struct yetty_yclass_object *node)
{
    if (!node) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ygui_ynode_ptr_result data_res =
        yetty_ygui_ynode_from((struct yetty_yclass_object *)node);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "yetty_ygui_ynode_output_count: downcast");
    return YETTY_OK(yetty_ycore_int, (int)data_res.value->out_count);
}

#include "yetty/gen/impl/ygui/widgets/ynode.c"
