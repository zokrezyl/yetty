/*
 * yplot.c — figure widget that wraps yplot's expression DSL.
 *
 * Subclass of the base widget (not primitive_widget): emit_container
 * mints CREATE_CHILD(kind=YPLOT) and emit_body builds the yplot
 * primitive from the widget's source + current rect and ships its
 * bytes as the figure body.
 */

#include "../internal.h"

#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ygui/widgets/yplot.h>
#include <yetty/yplot/yplot.h>

#include <stdlib.h>
#include <string.h>

struct [[clang::annotate("class@ygui:yplot")]] [[clang::annotate("parent@ygui:widget")]]
yplot_data {
    char *source;
    size_t source_len;
    struct yetty_ygui_yplot_config cfg;
    int has_cfg;
};

[[clang::annotate("override@ygui:yplot:constructor")]]
static struct yetty_ycore_void_result yplot_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                        struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_class_expect(yetty_ygui_yplot_class_get(), "yetty_ygui_yplot_class_get"),
        (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yplot_constructor: super");
    struct yplot_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_yplot_class_get(), "yetty_ygui_yplot_class_get"));
    d->source = NULL;
    d->source_len = 0;
    d->has_cfg = 0;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:yplot:destructor")]]
static struct yetty_ycore_void_result yplot_destructor(struct yetty_yclass_ctx *yclass_ctx,
                                                       struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yplot_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_yplot_class_get(), "yetty_ygui_yplot_class_get"));
    free(d->source);
    d->source = NULL;
    return yetty_ygui_super_void(
        obj, yetty_ygui_class_expect(yetty_ygui_yplot_class_get(), "yetty_ygui_yplot_class_get"),
        (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:yplot:widget_emit_container")]]
static struct yetty_ycore_void_result yplot_emit_container(struct yetty_yclass_ctx *yclass_ctx,
                                                           struct yetty_yclass_object *yclass_obj,
                                                           struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    return yetty_ygui_emit_ensure_child(ctx, yetty_ygui_object_id(obj), YETTY_YFIGURE_KIND_YPLOT,
                                        r.min.x, r.min.y, r.max.x, r.max.y,
                                        /*init_payload=*/NULL, /*init_payload_bytes=*/0);
}

[[clang::annotate("override@ygui:yplot:widget_emit_body")]]
static struct yetty_ycore_void_result yplot_emit_body(struct yetty_yclass_ctx *yclass_ctx,
                                                      struct yetty_yclass_object *yclass_obj,
                                                      struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yplot_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_yplot_class_get(), "yetty_ygui_yplot_class_get"));
    if (!d->source || d->source_len == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_yplot_render_config cfg = {
        .bounds_x = 0.0f,
        .bounds_y = 0.0f,
        .bounds_w = w,
        .bounds_h = h,
        .x_min = d->has_cfg ? d->cfg.x_min : -3.14159f,
        .x_max = d->has_cfg ? d->cfg.x_max : 3.14159f,
        .y_min = d->has_cfg ? d->cfg.y_min : -1.5f,
        .y_max = d->has_cfg ? d->cfg.y_max : 1.5f,
        .flags = d->has_cfg
                     ? d->cfg.flags
                     : (YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS),
    };
    struct yetty_ydraw_draw_list_result dlr = yetty_yplot_render(d->source, d->source_len, &cfg);
    if (YETTY_IS_ERR(dlr)) {
        return YETTY_ERR(yetty_ycore_void, "yplot_emit_body: yplot_render", dlr);
    }
    struct yetty_ydraw_draw_list *dl = dlr.value;
    const void *bytes = yetty_ydraw_draw_list_data(dl);
    size_t size = yetty_ydraw_draw_list_size(dl);
    struct yetty_ycore_void_result fr = YETTY_OK_VOID();
    if (bytes && size > 0) {
        if (size > 0xFFFFFFFFu) {
            yetty_ydraw_draw_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void,
                             "yplot_emit_body: rendered draw_list exceeds wire u32 length");
        }
        /* Receiver-side ygrid APPENDS instances/prims per body — same
         * accumulation bug as yimage. Prefix CMD_ZERO so the figure's
         * ygrid resets before consuming the fresh yplot record. See
         * yimage.c for the full rationale. */
        struct yetty_ydraw_draw_list_result zlr = yetty_ydraw_draw_list_config_buffer_create(NULL);
        if (YETTY_IS_ERR(zlr)) {
            yetty_ydraw_draw_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void, "yplot_emit_body: prefix list create", zlr);
        }
        struct yetty_ydraw_draw_list *zl = zlr.value;
        struct yetty_ycore_void_result zr = yetty_ydraw_draw_list_add_cmd_zero(zl);
        if (YETTY_IS_ERR(zr)) {
            yetty_ydraw_draw_list_destroy(zl);
            yetty_ydraw_draw_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void, "yplot_emit_body: CMD_ZERO append", zr);
        }
        const void *zbytes = yetty_ydraw_draw_list_data(zl);
        size_t zsize = yetty_ydraw_draw_list_size(zl);
        if (zsize > 0xFFFFFFFFu - size) {
            yetty_ydraw_draw_list_destroy(zl);
            yetty_ydraw_draw_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void, "yplot_emit_body: prefixed body would overflow u32");
        }
        size_t total = zsize + size;
        uint8_t *combined = malloc(total);
        if (!combined) {
            yetty_ydraw_draw_list_destroy(zl);
            yetty_ydraw_draw_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void, "yplot_emit_body: combined oom");
        }
        if (zbytes && zsize > 0) {
            memcpy(combined, zbytes, zsize);
        }
        memcpy(combined + zsize, bytes, size);
        fr = yetty_ygui_emit_figure_body(ctx, yetty_ygui_object_id(obj), combined, (uint32_t)total);
        free(combined);
        yetty_ydraw_draw_list_destroy(zl);
    }
    yetty_ydraw_draw_list_destroy(dl);
    return fr;
}

struct yetty_ycore_void_result yetty_ygui_yplot_set_source(struct yetty_ygui_object *obj,
                                                           const char *source)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yplot_set_source: NULL obj");
    }
    struct yplot_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_yplot_class_get(), "yetty_ygui_yplot_class_get"));
    free(d->source);
    d->source = NULL;
    d->source_len = 0;
    if (source) {
        size_t n = strlen(source);
        d->source = malloc(n + 1);
        if (!d->source) {
            return YETTY_ERR(yetty_ycore_void, "yplot_set_source: malloc");
        }
        memcpy(d->source, source, n + 1);
        d->source_len = n;
    }
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_yplot_set_config(
    struct yetty_ygui_object *obj, const struct yetty_ygui_yplot_config *cfg)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yplot_set_config: NULL obj");
    }
    struct yplot_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_yplot_class_get(), "yetty_ygui_yplot_class_get"));
    if (cfg) {
        d->cfg = *cfg;
        d->has_cfg = 1;
    } else {
        d->has_cfg = 0;
    }
    return yetty_ygui_object_set_dirty(obj);
}

#include "yplot.gen.c"
