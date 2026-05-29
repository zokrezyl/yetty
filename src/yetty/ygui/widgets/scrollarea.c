/* ygui-scrollarea.c — vbox + scrollbar visual. */
#include "paint-helpers.h"
#include <yetty/ygui/widgets/scrollarea.h>
#include <yetty/ygui/widgets/vbox.h>

#define COLOR_TRACK 0xFF1F1A14u
#define COLOR_THUMB 0xFF474A36u
#define SCROLLBAR_W 6.0f

[[clang::annotate("override@ygui:scrollarea:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *_yc_ctx,
                                           struct yetty_yclass_object *_yc_obj)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_scrollarea_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "scrollarea: super");
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.padding_right = SCROLLBAR_W + 4.0f;
    l.padding_left = 4.0f;
    l.padding_top = 4.0f;
    l.padding_bottom = 4.0f;
    l.gap = 4.0f;
    return yetty_ygui_widget_layout_set(obj, &l);
}

[[clang::annotate("override@ygui:scrollarea:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *_yc_ctx,
                                            struct yetty_yclass_object *_yc_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "scrollarea paint: NULL ctx");
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) {
        return YETTY_OK_VOID();
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yguix_box(ctx, r.max.x - SCROLLBAR_W - 2, r.min.y + 4, SCROLLBAR_W, h - 8,
                                  COLOR_TRACK, SCROLLBAR_W * 0.5f),
                        "scrollarea: track");
    /* Thumb covers top third for now (no scroll state). */
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yguix_box(ctx, r.max.x - SCROLLBAR_W - 2, r.min.y + 6, SCROLLBAR_W,
                                  (h - 8) * 0.33f, COLOR_THUMB, SCROLLBAR_W * 0.5f),
                        "scrollarea: thumb");
    return YETTY_OK_VOID();
}

struct [[clang::annotate("class@ygui:scrollarea")]] [[clang::annotate("parent@ygui:vbox")]]
scrollarea_data {
    char _empty;
};

#include "scrollarea.gen.c"
