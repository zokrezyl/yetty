/*
 * ygui-separator.c — hairline divider. Paints a flat box at its rect.
 */

#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * separator.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_separator_data_ptr, struct yetty_ygui_separator *);
struct yetty_yclass_ptr_result yetty_ygui_separator_class_get(void);
struct yetty_ygui_separator_data_ptr_result yetty_ygui_separator_data(
    struct yetty_ygui_object *obj);

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ysdf/funcs.gen.h>

#define COLOR_BORDER 0xFF474A36u

[[clang::annotate("override@ygui:separator:widget_paint")]]
static struct yetty_ycore_void_result separator_paint(struct yetty_yclass_ctx *yclass_ctx,
                                                      struct yetty_yclass_object *yclass_obj,
                                                      struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "separator_paint: NULL ctx");
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_ysdf_box geom = {
        .center_x = r.min.x + w * 0.5f,
        .center_y = r.min.y + h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
    };
    return yetty_ydraw_drawable_list_add_cmd_add_box(ctx->ygrid_drawable_list, 0u, 0u, COLOR_BORDER,
                                                     0u, 0.0f, &geom);
}

struct [[clang::annotate("class@ygui:separator")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] yetty_ygui_separator {
    char _empty;
};

#include "separator.gen.c"
