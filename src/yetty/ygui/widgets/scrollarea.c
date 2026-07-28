/* ygui-scrollarea.c — vbox that scrolls its content as a ygrid figure.
 *
 * The scrollarea promotes itself to its own YGRID figure (make_figure). The
 * grid base does the clipping for free — it renders the (absolute-coord)
 * subtree and GPU-scissors it to the figure's rect, so content never bleeds
 * past the box. Because coords stay absolute, layout, hit-testing and paint
 * need no special-casing: the children inside stay fully interactive.
 *
 * Scrolling is the base widget's main-axis scroll offset: the drag delta
 * moves it, the layout slides the children by it, the scissor clips the
 * overflow. The thumb sits at the gutter and tracks the offset.
 */
#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * scrollarea.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_scrollarea_ptr, struct yetty_ygui_scrollarea *);
struct yetty_yclass_ptr_result yetty_ygui_scrollarea_class_get(void);
struct yetty_ygui_scrollarea_ptr_result yetty_ygui_scrollarea_from(struct yetty_yclass_object *obj);
#include "paint-helpers.h"
#include <yetty/yfigure/kind.h>
#include <yetty/ygui/mixins/draggable.h>
#include <yetty/ygui/widgets/vbox.h>

#define COLOR_TRACK 0xFF1F1A14u
#define COLOR_THUMB 0xFF474A36u
#define SCROLLBAR_W 6.0f
#define SCROLLBAR_MIN_THUMB 24.0f

struct YETTY_ANNOTATE("class@ygui:scrollarea") YETTY_ANNOTATE("parent@ygui:vbox")
    YETTY_ANNOTATE("uses@ygui:draggable") yetty_ygui_scrollarea {
    float offset;       /* scroll position in px (0 = top) */
    float max_offset;   /* content_h - viewport_h, clamped >= 0 (cached) */
    float thumb_travel; /* track_h - thumb_h, clamped >= 0 (cached) */
};

static struct yetty_yclass_ptr_result scrollarea_class(void)
{
    return yetty_ygui_scrollarea_class_get();
}

/* Apply a clamped offset: store it, push it to the base scroll offset (so
 * the layout slides the children), and request a frame. */
static struct yetty_ycore_void_result scrollarea_set_offset(struct yetty_yclass_object *obj,
                                                            struct yetty_ygui_scrollarea *d,
                                                            float off)
{
    if (off < 0.0f) {
        off = 0.0f;
    }
    if (off > d->max_offset) {
        off = d->max_offset;
    }
    d->offset = off;
    struct yetty_ycore_void_result sr = yetty_ygui_widget_scroll_main_set(obj, off);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "scrollarea: scroll_main");
    return yetty_ygui_widget_set_dirty(obj);
}

/* Drag callback installed on the draggable mixin (fires for a drag anywhere
 * in the area). WHERE the drag started picks the gesture:
 *
 *   - on the scrollbar gutter → THUMB drag: the thumb follows the cursor,
 *     so the offset moves WITH dy, scaled from track pixels to content
 *     pixels (max_offset / thumb_travel). Dragging the thumb down scrolls
 *     down, like every desktop scrollbar.
 *   - anywhere else → DIRECT content drag: the content tracks the
 *     finger/cursor 1:1, the natural touch-scroll gesture — dragging down
 *     (dy>0) pulls the content down, revealing what's above. The inverse
 *     mapping of the thumb.
 *
 * The wheel path (on_scroll) is separate and unaffected. */
static struct yetty_ycore_void_result scrollarea_on_drag(struct yetty_yclass_object *obj, float dx,
                                                         float dy, void *userdata)
{
    (void)dx;
    (void)userdata;
    struct yetty_yclass_ptr_result class_result = scrollarea_class();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, class_result, "scrollarea_on_drag: class");
    struct yetty_yclass_void_ptr_result d_dr = yetty_yclass_object_data(obj, class_result.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "scrollarea_on_drag: data_get");
    struct yetty_ygui_scrollarea *d = d_dr.value;
    if (d->max_offset <= 0.0f) {
        return YETTY_OK_VOID();
    }
    float press_x = 0.0f, press_y = 0.0f;
    struct yetty_ycore_void_result press_res =
        yetty_ygui_draggable_press_point(obj, &press_x, &press_y);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, press_res, "scrollarea_on_drag: press_point");
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "scrollarea_on_drag: rect");
    int on_gutter = press_x >= rect_res.value.max.x - SCROLLBAR_W - 4.0f;

    float new_off;
    if (on_gutter) {
        /* thumb_travel is cached by the last paint — a drag can only start
		 * on a thumb that has been painted. */
        float track_to_content = d->thumb_travel > 0.0f ? (d->max_offset / d->thumb_travel) : 0.0f;
        new_off = d->offset + dy * track_to_content;
    } else {
        new_off = d->offset - dy;
    }
    if (new_off == d->offset) {
        return YETTY_OK_VOID();
    }
    return scrollarea_set_offset(obj, d, new_off);
}

/* Wheel / trackpad scroll. dy>0 (wheel up) moves toward the top. Consumed
 * only when there's room to scroll, so it bubbles to an enclosing
 * scrollable when this one is empty/short. */
YETTY_ANNOTATE("override@ygui:scrollarea:widget_on_scroll")
static struct yetty_ycore_int_result on_scroll(struct yetty_yclass_object *yclass_obj, float x,
                                               float y, float dx, float dy)
{
    (void)x;
    (void)y;
    (void)dx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_yclass_ptr_result class_result = scrollarea_class();
    YETTY_RETURN_IF_ERR(yetty_ycore_int, class_result, "on_scroll: class");
    struct yetty_yclass_void_ptr_result d_dr = yetty_yclass_object_data(obj, class_result.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "on_scroll: data_get");
    struct yetty_ygui_scrollarea *d = d_dr.value;
    if (d->max_offset <= 0.0f) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ycore_void_result sr = scrollarea_set_offset(obj, d, d->offset - dy * 48.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, sr, "scrollarea on_scroll");
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("override@ygui:scrollarea:constructor")
static struct yetty_ycore_void_result ctor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_scrollarea_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "scrollarea: super");
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "scrollarea: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.padding_right = SCROLLBAR_W + 4.0f;
    l.padding_left = 4.0f;
    l.padding_top = 4.0f;
    l.padding_bottom = 4.0f;
    l.gap = 4.0f;
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &l);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "scrollarea: layout");
    /* Become our own ygrid figure: dedicated grid at our rect → GPU scissor
     * clips the content, and the subtree lays out in local coords. */
    struct yetty_ycore_void_result fr =
        yetty_ygui_widget_make_figure(obj, yetty_yfigure_kind_token("ygrid"), 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "scrollarea: make_figure");
    return yetty_ygui_draggable_on_drag_set(obj, scrollarea_on_drag, NULL);
}

YETTY_ANNOTATE("override@ygui:scrollarea:widget_paint")
static struct yetty_ycore_void_result paint(struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "scrollarea paint: NULL ctx");
    }
    struct yetty_yclass_ptr_result class_result = scrollarea_class();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, class_result, "paint: class");
    struct yetty_yclass_void_ptr_result d_dr = yetty_yclass_object_data(obj, class_result.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "paint: data_get");
    struct yetty_ygui_scrollarea *d = d_dr.value;
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "scrollarea paint: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) {
        return YETTY_OK_VOID();
    }

    /* Content height = sum of in-flow children heights + gaps. Children are
     * laid out in this figure's local space; heights are offset-invariant. */
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "scrollarea paint: layout_get");
    const struct yetty_ygui_layout *l = layout_res.value;
    float viewport_h = h - l->padding_top - l->padding_bottom;
    if (viewport_h < 0.0f) {
        viewport_h = 0.0f;
    }
    float content_h = 0.0f;
    int n = 0;
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res, "scrollarea paint: first_child");
    for (struct yetty_yclass_object *c = child_res.value; c;) {
        struct yetty_ygui_layout_const_ptr_result child_layout_res =
            yetty_ygui_widget_layout_get(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, child_layout_res,
                            "scrollarea paint: child layout_get");
        const struct yetty_ygui_layout *cl = child_layout_res.value;
        if (!cl->hidden && !cl->absolute) {
            struct yetty_ycore_rectangle_result child_rect_res = yetty_ygui_widget_rect(c);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, child_rect_res, "scrollarea paint: child rect");
            struct yetty_ycore_rectangle cr = child_rect_res.value;
            content_h += cr.max.y - cr.min.y;
            n++;
        }
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, next_res, "scrollarea paint: next_sibling");
        c = next_res.value;
    }
    if (n > 1) {
        content_h += l->gap * (float)(n - 1);
    }

    float max_off = content_h - viewport_h;
    if (max_off < 0.0f) {
        max_off = 0.0f;
    }
    d->max_offset = max_off;

    /* Re-clamp if content shrank since the last drag. */
    if (d->offset > max_off) {
        struct yetty_ycore_void_result cr = scrollarea_set_offset(obj, d, max_off);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "scrollarea: clamp");
    }
    float off = d->offset;

    /* Track + thumb in absolute coords (the grid renders this figure's
     * content absolute and scissor-clips to the rect). Track spans the
     * content box vertically at the gutter. */
    float track_x = r.max.x - SCROLLBAR_W - 2.0f;
    float track_y = r.min.y + 4.0f;
    float track_h = h - 8.0f;
    if (track_h < 0.0f) {
        track_h = 0.0f;
    }

    float thumb_h, thumb_y, thumb_travel;
    if (max_off <= 0.0f || content_h <= 0.0f) {
        thumb_h = track_h;
        thumb_travel = 0.0f;
        thumb_y = track_y;
    } else {
        thumb_h = track_h * (viewport_h / content_h);
        if (thumb_h < SCROLLBAR_MIN_THUMB) {
            thumb_h = SCROLLBAR_MIN_THUMB;
        }
        if (thumb_h > track_h) {
            thumb_h = track_h;
        }
        thumb_travel = track_h - thumb_h;
        if (thumb_travel < 0.0f) {
            thumb_travel = 0.0f;
        }
        thumb_y = track_y + (off / max_off) * thumb_travel;
    }
    d->thumb_travel = thumb_travel;

    struct yetty_ycore_void_result result_211 =
        yguix_box(ctx, track_x, track_y, SCROLLBAR_W, track_h, COLOR_TRACK, SCROLLBAR_W * 0.5f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_211, "scrollarea: track");
    struct yetty_ycore_void_result result_215 =
        yguix_box(ctx, track_x, thumb_y, SCROLLBAR_W, thumb_h, COLOR_THUMB, SCROLLBAR_W * 0.5f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_215, "scrollarea: thumb");
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui/widgets/scrollarea.c"
