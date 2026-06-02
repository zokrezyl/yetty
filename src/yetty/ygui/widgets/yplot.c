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

/* One owned data buffer (deep-copied from the caller's input). */
struct yplot_buf_slot {
    float *samples;
    size_t count;
    uint32_t color;
};

struct [[clang::annotate("class@ygui:yplot")]] [[clang::annotate("parent@ygui:widget")]]
yplot_data {
    char *source;
    size_t source_len;
    struct yetty_ygui_yplot_config cfg;
    int has_cfg;
    /* Raw-data plotting: owned deep copies of the caller's buffers,
     * rendered as line plots over x_min..x_max. Empty for expression-
     * only plots. */
    struct yplot_buf_slot *buffers;
    size_t buffer_count;
};

static void yplot_free_buffers(struct yplot_data *d)
{
    if (d->buffers) {
        for (size_t i = 0; i < d->buffer_count; i++) {
            free(d->buffers[i].samples);
        }
        free(d->buffers);
        d->buffers = NULL;
    }
    d->buffer_count = 0;
}

[[clang::annotate("override@ygui:yplot:constructor")]]
static struct yetty_ycore_void_result yplot_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                        struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_yplot_class_get().value,
        (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yplot_constructor: super");
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_yplot_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yplot_constructor: data_get");
    struct yplot_data *d = d_dr.value;
    d->source = NULL;
    d->source_len = 0;
    d->has_cfg = 0;
    d->buffers = NULL;
    d->buffer_count = 0;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:yplot:destructor")]]
static struct yetty_ycore_void_result yplot_destructor(struct yetty_yclass_ctx *yclass_ctx,
                                                       struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_yplot_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yplot_destructor: data_get");
    struct yplot_data *d = d_dr.value;
    free(d->source);
    d->source = NULL;
    yplot_free_buffers(d);
    return yetty_ygui_super_void(
        obj, yetty_ygui_yplot_class_get().value,
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
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_yplot_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yplot_emit_body: data_get");
    struct yplot_data *d = d_dr.value;
    int have_source = d->source && d->source_len > 0;
    int have_buffers = d->buffers && d->buffer_count > 0;
    if (!have_source && !have_buffers) {
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
    const char *src = d->source ? d->source : "";
    struct yetty_ydraw_draw_list_result dlr;
    if (have_buffers) {
        /* Materialise the wire-format buffer_input array yplot expects. */
        struct yetty_yplot_buffer_input *bufs =
            calloc(d->buffer_count, sizeof(struct yetty_yplot_buffer_input));
        if (!bufs) {
            return YETTY_ERR(yetty_ycore_void, "yplot_emit_body: buffer_input oom");
        }
        for (size_t i = 0; i < d->buffer_count; i++) {
            bufs[i].samples = d->buffers[i].samples;
            bufs[i].count = d->buffers[i].count;
            bufs[i].color = d->buffers[i].color;
        }
        dlr = yetty_yplot_render_with_buffers(src, d->source_len, bufs, d->buffer_count, &cfg);
        free(bufs);
    } else {
        dlr = yetty_yplot_render(d->source, d->source_len, &cfg);
    }
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

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_yplot_set_source(struct yetty_ygui_object *obj,
                                                           const char *source)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yplot_set_source: NULL obj");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_yplot_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_yplot_set_source: data_get");
    struct yplot_data *d = d_dr.value;
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

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_yplot_set_config(
    struct yetty_ygui_object *obj, const struct yetty_ygui_yplot_config *cfg)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yplot_set_config: NULL obj");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_yplot_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_yplot_set_config: data_get");
    struct yplot_data *d = d_dr.value;
    if (cfg) {
        d->cfg = *cfg;
        d->has_cfg = 1;
    } else {
        d->has_cfg = 0;
    }
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_yplot_set_buffers(
    struct yetty_ygui_object *obj, const char *source, size_t source_len,
    const struct yetty_yplot_buffer_input *buffers, size_t buffer_count,
    const struct yetty_ygui_yplot_config *config)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yplot_set_buffers: NULL obj");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_yplot_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_yplot_set_buffers: data_get");
    struct yplot_data *d = d_dr.value;

    /* Replace the expression source (NULL clears it — buffer-only plot). */
    free(d->source);
    d->source = NULL;
    d->source_len = 0;
    if (source) {
        size_t n = source_len ? source_len : strlen(source);
        d->source = malloc(n + 1);
        if (!d->source) {
            return YETTY_ERR(yetty_ycore_void, "yplot_set_buffers: source malloc");
        }
        memcpy(d->source, source, n);
        d->source[n] = '\0';
        d->source_len = n;
    }

    /* Deep-copy the data buffers — the widget owns its copies so the
     * caller can free / reuse its arrays after the call. */
    yplot_free_buffers(d);
    if (buffers && buffer_count > 0) {
        struct yplot_buf_slot *slots = calloc(buffer_count, sizeof(struct yplot_buf_slot));
        if (!slots) {
            return YETTY_ERR(yetty_ycore_void, "yplot_set_buffers: slots oom");
        }
        for (size_t i = 0; i < buffer_count; i++) {
            slots[i].count = buffers[i].count;
            slots[i].color = buffers[i].color;
            if (buffers[i].samples && buffers[i].count > 0) {
                slots[i].samples = malloc(buffers[i].count * sizeof(float));
                if (!slots[i].samples) {
                    for (size_t j = 0; j < i; j++) {
                        free(slots[j].samples);
                    }
                    free(slots);
                    return YETTY_ERR(yetty_ycore_void, "yplot_set_buffers: samples oom");
                }
                memcpy(slots[i].samples, buffers[i].samples, buffers[i].count * sizeof(float));
            }
        }
        d->buffers = slots;
        d->buffer_count = buffer_count;
    }

    if (config) {
        d->cfg = *config;
        d->has_cfg = 1;
    }
    return yetty_ygui_object_set_dirty(obj);
}

#include "yplot.gen.c"

#ifdef YCLASS_CODEGEN
#include <stddef.h>
#include <stdint.h>
struct yetty_ygui_object;
struct yetty_ygui_yplot_config {
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    uint32_t flags;
};
struct yetty_yplot_buffer_input;
#endif
