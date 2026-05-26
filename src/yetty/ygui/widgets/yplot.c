/*
 * yplot.c — figure widget that wraps yplot's expression DSL.
 *
 * Subclass of the base widget (not primitive_widget): emit_container
 * mints CREATE_CHILD(kind=YPLOT) and emit_body builds the yplot
 * primitive from the widget's source + current rect and ships its
 * bytes as the figure body.
 */

#include "../internal.h"

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ygui/widgets/yplot.h>
#include <yetty/yplot/yplot.h>

#include <stdlib.h>
#include <string.h>

struct yplot_data {
    char *source;
    size_t source_len;
    struct yetty_ygui_yplot_config cfg;
    int has_cfg;
};

static struct yetty_ycore_void_result yplot_constructor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_yplot_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yplot_constructor: super");
    struct yplot_data *d = yetty_ygui_data_get(obj, yetty_ygui_yplot_class_get());
    d->source = NULL;
    d->source_len = 0;
    d->has_cfg = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yplot_destructor(struct yetty_ygui_object *obj)
{
    struct yplot_data *d = yetty_ygui_data_get(obj, yetty_ygui_yplot_class_get());
    free(d->source);
    d->source = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_yplot_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_void_result yplot_emit_container(struct yetty_ygui_object *obj,
                                                           struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    return yetty_ygui_emit_ensure_child(ctx, yetty_ygui_object_id(obj), YETTY_YFIGURE_KIND_YPLOT,
                                        r.min.x, r.min.y, r.max.x, r.max.y,
                                        /*init_payload=*/NULL, /*init_payload_bytes=*/0);
}

static struct yetty_ycore_void_result yplot_emit_body(struct yetty_ygui_object *obj,
                                                      struct yetty_ygui_emit_ctx *ctx)
{
    struct yplot_data *d = yetty_ygui_data_get(obj, yetty_ygui_yplot_class_get());
    if (!d->source || d->source_len == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) return YETTY_OK_VOID();
    struct yetty_yplot_render_config cfg = {
        .bounds_x = 0.0f,
        .bounds_y = 0.0f,
        .bounds_w = w,
        .bounds_h = h,
        .x_min = d->has_cfg ? d->cfg.x_min : -3.14159f,
        .x_max = d->has_cfg ? d->cfg.x_max : 3.14159f,
        .y_min = d->has_cfg ? d->cfg.y_min : -1.5f,
        .y_max = d->has_cfg ? d->cfg.y_max : 1.5f,
        .flags = d->has_cfg ? d->cfg.flags
                            : (YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES |
                               YETTY_YPLOT_FLAG_LABELS),
    };
    struct yetty_ydraw_draw_list_result dlr =
        yetty_yplot_render(d->source, d->source_len, &cfg);
    if (YETTY_IS_ERR(dlr)) {
        return YETTY_ERR(yetty_ycore_void, "yplot_emit_body: yplot_render", dlr);
    }
    struct yetty_ydraw_draw_list *dl = dlr.value;
    const void *bytes = yetty_ydraw_draw_list_data(dl);
    size_t size = yetty_ydraw_draw_list_size(dl);
    struct yetty_ycore_void_result fr = YETTY_OK_VOID();
    if (bytes && size > 0 && size <= 0xFFFFFFFFu) {
        fr = yetty_ygui_emit_figure_body(ctx, yetty_ygui_object_id(obj), (const uint8_t *)bytes,
                                         (uint32_t)size);
    }
    yetty_ydraw_draw_list_destroy(dl);
    return fr;
}

struct yetty_ycore_void_result yetty_ygui_yplot_set_source(struct yetty_ygui_object *obj,
                                                           const char *source)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yplot_set_source: NULL obj");
    struct yplot_data *d = yetty_ygui_data_get(obj, yetty_ygui_yplot_class_get());
    free(d->source);
    d->source = NULL;
    d->source_len = 0;
    if (source) {
        size_t n = strlen(source);
        d->source = malloc(n + 1);
        if (!d->source) return YETTY_ERR(yetty_ycore_void, "yplot_set_source: malloc");
        memcpy(d->source, source, n + 1);
        d->source_len = n;
    }
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_yplot_set_config(struct yetty_ygui_object *obj,
                                                           const struct yetty_ygui_yplot_config *cfg)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yplot_set_config: NULL obj");
    struct yplot_data *d = yetty_ygui_data_get(obj, yetty_ygui_yplot_class_get());
    if (cfg) {
        d->cfg = *cfg;
        d->has_cfg = 1;
    } else {
        d->has_cfg = 0;
    }
    return yetty_ygui_object_set_dirty(obj);
}


static const struct yetty_ygui_op yplot_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, yplot_constructor),
    YETTY_YGUI_OP(yetty_ygui_destructor, yplot_destructor),
    YETTY_YGUI_OP(yetty_ygui_widget_emit_container, yplot_emit_container),
    YETTY_YGUI_OP(yetty_ygui_widget_emit_body, yplot_emit_body),
};

static const struct yetty_ygui_class_descriptor yplot_desc = {
    .name = "yetty_ygui_yplot",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct yplot_data),
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_yplot_class_get, &yplot_desc, yplot_ops, yetty_ygui_widget_class_get(), NULL)
