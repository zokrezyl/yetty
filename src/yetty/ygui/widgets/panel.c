/*
 * ygui-panel.c — filled / outlined rectangle container.
 *
 * Renders a background-color fill + optional border, then lets children
 * paint on top (the engine's tree walk recurses naturally — the panel
 * doesn't override emit_body, just paint).
 *
 * Paint marker: {"PANL", id, rgba_bg, rgba_border, border_w}.
 */

#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * panel.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_panel_ptr, struct yetty_ygui_panel *);
struct yetty_yclass_ptr_result yetty_ygui_panel_class_get(void);
struct yetty_ygui_panel_ptr_result yetty_ygui_panel_from(struct yetty_yclass_object *obj);

#include <yetty/ygui/primitive-widget.h>

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <string.h>

struct YETTY_ANNOTATE("class@ygui:panel") YETTY_ANNOTATE("parent@ygui:primitive_widget")
    yetty_ygui_panel {
    struct yetty_ycore_rgba bg;
    struct yetty_ycore_rgba border;
    float border_width;
};

YETTY_ANNOTATE("override@ygui:panel:constructor")
static struct yetty_ycore_void_result panel_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_panel_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "panel_constructor: super");
    struct yetty_ygui_panel_ptr_result d_dr = yetty_ygui_panel_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "panel_constructor: data_get");
    struct yetty_ygui_panel *d = d_dr.value;
    /* Defaults: BRAND_BG_LIFTED background, BRAND_BORDER border, 1px. */
    d->bg = (struct yetty_ycore_rgba){20, 26, 31, 255};
    d->border = (struct yetty_ycore_rgba){54, 74, 71, 255};
    d->border_width = 1.0f;
    return YETTY_OK_VOID();
}

static uint32_t pack_rgba(struct yetty_ycore_rgba c)
{
    return (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16) | ((uint32_t)c.a << 24);
}

YETTY_ANNOTATE("override@ygui:panel:widget_paint")
static struct yetty_ycore_void_result panel_paint(struct yetty_yclass_object *yclass_obj,
                                                  struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "panel_paint: NULL ctx");
    }
    struct yetty_ygui_panel_ptr_result d_dr = yetty_ygui_panel_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "panel_paint: data_get");
    struct yetty_ygui_panel *d = d_dr.value;
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
        .corner_radius = 0.0f,
    };
    return yetty_ydraw_drawable_list_add_cmd_add_box(ctx->ygrid_drawable_list, /*id=*/0,
                                                     /*z_order=*/0, pack_rgba(d->bg),
                                                     pack_rgba(d->border), d->border_width, &geom);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_panel_set_bg(struct yetty_yclass_object *obj,
                                                       struct yetty_ycore_rgba color)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_panel_set_bg: NULL obj");
    }
    struct yetty_ygui_panel_ptr_result d_dr = yetty_ygui_panel_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_panel_set_bg: data_get");
    struct yetty_ygui_panel *d = d_dr.value;
    d->bg = color;
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_panel_set_border(struct yetty_yclass_object *obj,
                                                           struct yetty_ycore_rgba color,
                                                           float width_px)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_panel_set_border: NULL obj");
    }
    struct yetty_ygui_panel_ptr_result d_dr = yetty_ygui_panel_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_panel_set_border: data_get");
    struct yetty_ygui_panel *d = d_dr.value;
    d->border = color;
    d->border_width = width_px;
    return yetty_ygui_widget_set_dirty(obj);
}

#include "panel.gen.c"
