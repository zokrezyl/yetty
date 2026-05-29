/* ygui-splitter.c — divider strip. */
#include "paint-helpers.h"
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/splitter.h>

#define COLOR_TRACK 0xFF2C261Eu
#define COLOR_GRIP 0xFF92A86Bu

[[clang::annotate("override@ygui:splitter:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "splitter paint: NULL ctx");
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) {
        return YETTY_OK_VOID();
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yguix_box(ctx, r.min.x, r.min.y, w, h, COLOR_TRACK, 0),
                        "splitter: track");
    /* Grip dots — three small marks centred. */
    float cx = r.min.x + w * 0.5f;
    float cy = r.min.y + h * 0.5f;
    for (int i = -1; i <= 1; i++) {
        YETTY_RETURN_IF_ERR(yetty_ycore_void, yguix_circle(ctx, cx, cy + i * 6, 2, COLOR_GRIP),
                            "splitter: dot");
    }
    return YETTY_OK_VOID();
}

struct [[clang::annotate("class@ygui:splitter")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] splitter_data {
    char _empty;
};

#include "splitter.gen.c"
