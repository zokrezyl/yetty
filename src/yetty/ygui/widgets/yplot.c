/*
 * yplot.c — figure widget that wraps yplot's expression DSL.
 *
 * Subclass of the base widget (not primitive_widget): emit_container
 * mints CREATE_CHILD(kind=YPLOT) and emit_body builds the yplot
 * primitive from the widget's source + current rect and ships its
 * bytes as the figure body.
 */

#include "../internal.h"
#include "yetty/gen/impl/ygui/widget.h"

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * yplot.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_yplot_ptr, struct yetty_ygui_yplot *);
struct yetty_yclass_ptr_result yetty_ygui_yplot_class_get(void);
struct yetty_ygui_yplot_ptr_result yetty_ygui_yplot_from(struct yetty_yclass_object *obj);
/* Plot configuration. Defined here in the owning .c; codegen reproduces it
 * into the generated header (consumers fill it by value, so the full
 * definition is published via the `expose` annotation). */
struct YETTY_ANNOTATE("expose") yetty_ygui_yplot_config {
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    uint32_t flags;
};

#include <yetty/ydraw-list/cmds.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/yfigure/kind.h>
#include <yetty/yplot/yplot.h>

#include <stdlib.h>
#include <string.h>

/* One owned data buffer (deep-copied from the caller's input). */
struct yplot_buf_slot {
    float *samples;
    size_t count;
    uint32_t color;
};

struct YETTY_ANNOTATE("class@ygui:yplot") YETTY_ANNOTATE("parent@ygui:widget") yetty_ygui_yplot {
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

static void yplot_free_buffers(struct yetty_ygui_yplot *d)
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

YETTY_ANNOTATE("override@ygui:yplot:constructor")
static struct yetty_ycore_void_result yplot_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_yplot_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yplot_constructor: super");
    struct yetty_ygui_yplot_ptr_result d_dr = yetty_ygui_yplot_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yplot_constructor: data_get");
    struct yetty_ygui_yplot *d = d_dr.value;
    d->source = NULL;
    d->source_len = 0;
    d->has_cfg = 0;
    d->buffers = NULL;
    d->buffer_count = 0;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui:yplot:destructor")
static struct yetty_ycore_void_result yplot_destructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_yplot_ptr_result d_dr = yetty_ygui_yplot_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yplot_destructor: data_get");
    struct yetty_ygui_yplot *d = d_dr.value;
    free(d->source);
    d->source = NULL;
    yplot_free_buffers(d);
    return yetty_ygui_super_void(obj, yetty_ygui_yplot_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

YETTY_ANNOTATE("override@ygui:yplot:widget_emit_container")
static struct yetty_ycore_void_result yplot_emit_container(struct yetty_yclass_object *yclass_obj,
                                                           struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "yplot_emit_container: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    struct yetty_ycore_uint32_result id_res = yetty_ygui_widget_id(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "yplot_emit_container: id");
    /* Kind names the renderer config (host-policy-coords content ygrid), not
     * the content type — the plot itself travels as a complex record in
     * the child's body (#685 Phase 2). "yplot" stays registered only as a
     * deprecated alias for external producers. */
    return yetty_ygui_emit_ensure_child(ctx, id_res.value, yetty_yfigure_kind_token("yscroll"),
                                        r.min.x, r.min.y, r.max.x, r.max.y,
                                        /*init_payload=*/NULL, /*init_payload_bytes=*/0);
}

YETTY_ANNOTATE("override@ygui:yplot:widget_emit_body")
static struct yetty_ycore_void_result yplot_emit_body(struct yetty_yclass_object *yclass_obj,
                                                      struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_yplot_ptr_result d_dr = yetty_ygui_yplot_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yplot_emit_body: data_get");
    struct yetty_ygui_yplot *d = d_dr.value;
    int have_source = d->source && d->source_len > 0;
    int have_buffers = d->buffers && d->buffer_count > 0;
    if (!have_source && !have_buffers) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "yplot_emit_body: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_yplot_render_config cfg = {
        /* FIGURE-LOCAL bounds. emit_container mints this body's child as kind
         * "yscroll", which is a LOCAL-coordinate figure (#685 Phase 2): the
         * receiving scene anchors the complex at the child's own rect origin
         * (yscene scene.c, anchor = (translate - scroll) * view_scale) and the
         * record's bounds are added on top of that anchor. Baking the absolute
         * widget rect in here adds the origin a SECOND time — the plot lands
         * one widget-origin down and to the right of its pane, magnified by
         * content_scale on HiDPI so it also overflows the far edges. */
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
    struct yetty_ydraw_drawable_list_result dlr;
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
    struct yetty_ydraw_drawable_list *dl = dlr.value;
    const void *bytes = yetty_ydraw_drawable_list_data(dl);
    size_t size = yetty_ydraw_drawable_list_size(dl);
    struct yetty_ycore_void_result fr = YETTY_OK_VOID();
    if (bytes && size > 0) {
        if (size > 0xFFFFFFFFu) {
            yetty_ydraw_drawable_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void,
                             "yplot_emit_body: rendered drawable_list exceeds wire u32 length");
        }
        /* Receiver-side ygrid APPENDS instances/prims per body — same
         * accumulation bug as yimage. Prefix CMD_ZERO so the figure's
         * ygrid resets before consuming the fresh yplot record. See
         * yimage.c for the full rationale. */
        struct yetty_ydraw_drawable_list_result zlr =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        if (YETTY_IS_ERR(zlr)) {
            yetty_ydraw_drawable_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void, "yplot_emit_body: prefix list create", zlr);
        }
        struct yetty_ydraw_drawable_list *zl = zlr.value;
        struct yetty_ycore_void_result zr = yetty_ydraw_drawable_list_add_cmd_zero(zl);
        if (YETTY_IS_ERR(zr)) {
            yetty_ydraw_drawable_list_destroy(zl);
            yetty_ydraw_drawable_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void, "yplot_emit_body: CMD_ZERO append", zr);
        }
        const void *zbytes = yetty_ydraw_drawable_list_data(zl);
        size_t zsize = yetty_ydraw_drawable_list_size(zl);
        if (zsize > 0xFFFFFFFFu - size) {
            yetty_ydraw_drawable_list_destroy(zl);
            yetty_ydraw_drawable_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void, "yplot_emit_body: prefixed body would overflow u32");
        }
        size_t total = zsize + size;
        uint8_t *combined = malloc(total);
        if (!combined) {
            yetty_ydraw_drawable_list_destroy(zl);
            yetty_ydraw_drawable_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void, "yplot_emit_body: combined oom");
        }
        if (zbytes && zsize > 0) {
            memcpy(combined, zbytes, zsize);
        }
        memcpy(combined + zsize, bytes, size);
        struct yetty_ycore_uint32_result id_res = yetty_ygui_widget_id(obj);
        if (YETTY_IS_ERR(id_res)) {
            free(combined);
            yetty_ydraw_drawable_list_destroy(zl);
            yetty_ydraw_drawable_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void, "yplot_emit_body: id", id_res);
        }
        fr = yetty_ygui_emit_figure_body(ctx, id_res.value, combined, (uint32_t)total);
        free(combined);
        yetty_ydraw_drawable_list_destroy(zl);
    }
    yetty_ydraw_drawable_list_destroy(dl);
    return fr;
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_yplot_set_source(struct yetty_yclass_object *obj,
                                                           const char *source)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yplot_set_source: NULL obj");
    }
    struct yetty_ygui_yplot_ptr_result d_dr = yetty_ygui_yplot_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_yplot_set_source: data_get");
    struct yetty_ygui_yplot *d = d_dr.value;
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
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_yplot_set_config(
    struct yetty_yclass_object *obj, const struct yetty_ygui_yplot_config *cfg)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yplot_set_config: NULL obj");
    }
    struct yetty_ygui_yplot_ptr_result d_dr = yetty_ygui_yplot_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_yplot_set_config: data_get");
    struct yetty_ygui_yplot *d = d_dr.value;
    if (cfg) {
        d->cfg = *cfg;
        d->has_cfg = 1;
    } else {
        d->has_cfg = 0;
    }
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_yplot_set_buffers(
    struct yetty_yclass_object *obj, const char *source, size_t source_len,
    const struct yetty_yplot_buffer_input *buffers, size_t buffer_count,
    const struct yetty_ygui_yplot_config *config)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yplot_set_buffers: NULL obj");
    }
    struct yetty_ygui_yplot_ptr_result d_dr = yetty_ygui_yplot_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_yplot_set_buffers: data_get");
    struct yetty_ygui_yplot *d = d_dr.value;

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
    return yetty_ygui_widget_set_dirty(obj);
}

#include "yetty/gen/impl/ygui/widgets/yplot.c"
