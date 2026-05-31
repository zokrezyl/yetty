/* ygui-scrollarea.c — vbox with a draggable scrollbar.
 *
 * Extends vbox and lists the draggable mixin, so a press in the content
 * area or the scrollbar gutter is captured and every motion arrives as a
 * (dx, dy) delta. The vertical delta drives a scroll offset that the
 * layout pass applies to the children (see yetty_ygui_widget_scroll_main_*
 * + layout.c), and the thumb's size and position are derived from the
 * content-vs-viewport ratio rather than being hardcoded.
 *
 * Overflow is culled to the viewport by the emit walk: this widget sets
 * the base "clip children" flag, so its subtree is clipped to its content
 * box (a CPU stand-in for a GPU scissor). The cull is geometry-granular —
 * whole widgets outside the viewport are dropped and multi-line text drops
 * whole rows — so a partial row at the edge steps rather than being
 * pixel-clipped; true per-pixel clipping still needs renderer support.
 */
#include "../internal.h"
#include "paint-helpers.h"
#include <yetty/ygui/mixins/draggable.h>
#include <yetty/ygui/widgets/scrollarea.h>
#include <yetty/ygui/widgets/vbox.h>

#define COLOR_TRACK 0xFF1F1A14u
#define COLOR_THUMB 0xFF474A36u
#define SCROLLBAR_W 6.0f
#define SCROLLBAR_MIN_THUMB 24.0f

struct [[clang::annotate("class@ygui:scrollarea")]] [[clang::annotate("parent@ygui:vbox")]]
[[clang::annotate("uses@ygui:draggable")]] scrollarea_data {
    /* Current scroll position in px (0 = top). The single source of truth;
     * mirrored into the base widget's scroll_main for the layout pass. */
    float offset;
    /* Cached each paint so the drag callback can map a cursor delta onto a
     * scroll delta without re-measuring the children. */
    float max_offset;  /* content_h - viewport_h, clamped >= 0 */
    float thumb_travel; /* track_h - thumb_h, clamped >= 0 */
};

static const struct yetty_yclass *scrollarea_class(void)
{
    return yetty_ygui_class_expect(yetty_ygui_scrollarea_class_get(),
                                   "yetty_ygui_scrollarea_class_get");
}

/* Drag callback installed on the draggable mixin. The cursor moves the
 * thumb 1:1, and the thumb's travel maps onto the full content offset, so
 * scale the vertical delta by content-travel / thumb-travel. */
static struct yetty_ycore_void_result scrollarea_on_drag(struct yetty_ygui_object *obj, float dx,
                                                         float dy, void *userdata)
{
    (void)dx;
    (void)userdata;
    struct scrollarea_data *d = yetty_ygui_data_get(obj, scrollarea_class());
    if (d->max_offset <= 0.0f || d->thumb_travel <= 0.0f) {
        return YETTY_OK_VOID(); /* content fits — nothing to scroll */
    }
    float new_off = d->offset + dy * (d->max_offset / d->thumb_travel);
    if (new_off < 0.0f) {
        new_off = 0.0f;
    }
    if (new_off > d->max_offset) {
        new_off = d->max_offset;
    }
    if (new_off == d->offset) {
        return YETTY_OK_VOID();
    }
    d->offset = new_off;
    struct yetty_ycore_void_result sr = yetty_ygui_widget_scroll_main_set(obj, new_off);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "scrollarea_on_drag: scroll_main");
    /* Request a frame: layout re-places the children at the new offset and
     * the thumb repaints at its new position. */
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("override@ygui:scrollarea:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj,
                              yetty_ygui_class_expect(yetty_ygui_scrollarea_class_get(),
                                                      "yetty_ygui_scrollarea_class_get"),
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "scrollarea: super");
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.padding_right = SCROLLBAR_W + 4.0f;
    l.padding_left = 4.0f;
    l.padding_top = 4.0f;
    l.padding_bottom = 4.0f;
    l.gap = 4.0f;
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &l);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "scrollarea: layout");
    /* Cull over-long content to the viewport — the emit walk clips this
     * widget's subtree to its content box (no GPU scissor yet). */
    struct yetty_ycore_void_result cr = yetty_ygui_widget_set_clip_children(obj, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "scrollarea: clip_children");
    return yetty_ygui_draggable_on_drag_set(obj, scrollarea_on_drag, NULL);
}

[[clang::annotate("override@ygui:scrollarea:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "scrollarea paint: NULL ctx");
    }
    struct scrollarea_data *d = yetty_ygui_data_get(obj, scrollarea_class());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) {
        return YETTY_OK_VOID();
    }

    /* Viewport (content-box) height vs. intrinsic content height. Children
     * keep their laid-out heights independent of the scroll offset, so the
     * sum of their heights + gaps is a stable content extent. */
    const struct yetty_ygui_layout *l = yetty_ygui_widget_layout_get(obj);
    float viewport_h = h - l->padding_top - l->padding_bottom;
    if (viewport_h < 0.0f) {
        viewport_h = 0.0f;
    }
    float content_h = 0.0f;
    int n = 0;
    for (struct yetty_ygui_object *c = yetty_ygui_object_first_child(obj); c;
         c = yetty_ygui_object_next_sibling(c)) {
        const struct yetty_ygui_layout *cl = yetty_ygui_widget_layout_get(c);
        if (cl->hidden || cl->absolute) {
            continue;
        }
        struct yetty_ycore_rectangle cr = yetty_ygui_widget_rect(c);
        content_h += cr.max.y - cr.min.y;
        n++;
    }
    if (n > 1) {
        content_h += l->gap * (float)(n - 1);
    }

    float max_off = content_h - viewport_h;
    if (max_off < 0.0f) {
        max_off = 0.0f;
    }

    /* Clamp the live offset (content may have shrunk since the last drag). */
    float off = d->offset;
    if (off > max_off) {
        off = max_off;
    }
    if (off < 0.0f) {
        off = 0.0f;
    }

    /* Track spans the content box vertically, inset to match the padding. */
    float track_x = r.max.x - SCROLLBAR_W - 2.0f;
    float track_y = r.min.y + 4.0f;
    float track_h = h - 8.0f;
    if (track_h < 0.0f) {
        track_h = 0.0f;
    }

    float thumb_h, thumb_y, thumb_travel;
    if (max_off <= 0.0f || content_h <= 0.0f) {
        /* Everything fits — full-height thumb, pinned, no travel. */
        thumb_h = track_h;
        thumb_travel = 0.0f;
        thumb_y = track_y;
        off = 0.0f;
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

    /* Cache what the drag callback needs, then commit the (clamped) offset.
     * Outside the clamp path offset == scroll_main already (kept in sync by
     * the drag callback), so only re-sync + repaint when the clamp moved
     * it — that keeps this from re-dirtying the widget every frame. */
    d->max_offset = max_off;
    d->thumb_travel = thumb_travel;
    if (off != d->offset) {
        d->offset = off;
        struct yetty_ycore_void_result mr = yetty_ygui_widget_scroll_main_set(obj, off);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, mr, "scrollarea: scroll_main clamp");
        struct yetty_ycore_void_result cr = yetty_ygui_object_set_dirty(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "scrollarea: clamp dirty");
    }

    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yguix_box(ctx, track_x, track_y, SCROLLBAR_W, track_h, COLOR_TRACK,
                                  SCROLLBAR_W * 0.5f),
                        "scrollarea: track");
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yguix_box(ctx, track_x, thumb_y, SCROLLBAR_W, thumb_h, COLOR_THUMB,
                                  SCROLLBAR_W * 0.5f),
                        "scrollarea: thumb");
    return YETTY_OK_VOID();
}

#include "scrollarea.gen.c"
