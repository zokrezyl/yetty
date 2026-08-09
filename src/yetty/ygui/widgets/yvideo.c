/* ygui-yvideo.c — same shape as yimage, figure_kind = YVIDEO. */
#include "../internal.h"
#include "yetty/gen/impl/ygui/widget.h"

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * yvideo.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_yvideo_ptr, struct yetty_ygui_yvideo *);
struct yetty_yclass_ptr_result yetty_ygui_yvideo_class_get(void);
struct yetty_ygui_yvideo_ptr_result yetty_ygui_yvideo_from(struct yetty_yclass_object *obj);
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yfigure/kind.h>
#include <yetty/yvideo/yvideo-mp4.h>
#include <stdlib.h>
#include <string.h>

struct YETTY_ANNOTATE("class@ygui:yvideo") YETTY_ANNOTATE("parent@ygui:widget") yetty_ygui_yvideo {
    uint8_t *bytes;
    size_t len;
    /* Body-emission gate (#457) — same contract as yimage: the body (the
     * whole MP4 wrapped in a drawable_list, plus a full demux to build it)
     * only re-ships when content or baked-in bounds changed. */
    uint64_t content_generation;
    uint64_t emitted_generation;
    struct yetty_ycore_rectangle emitted_rect;
};

YETTY_ANNOTATE("override@ygui:yvideo:constructor")
static struct yetty_ycore_void_result ctor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_yvideo_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yvideo: super");
    struct yetty_ygui_yvideo_ptr_result d_dr = yetty_ygui_yvideo_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ctor: data_get");
    struct yetty_ygui_yvideo *d = d_dr.value;
    d->bytes = NULL;
    d->len = 0;
    d->content_generation = 1; /* differs from emitted_generation=0 → first emit ships */
    d->emitted_generation = 0;
    memset(&d->emitted_rect, 0, sizeof(d->emitted_rect));
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui:yvideo:destructor")
static struct yetty_ycore_void_result dtor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_yvideo_ptr_result d_dr = yetty_ygui_yvideo_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "dtor: data_get");
    struct yetty_ygui_yvideo *d = d_dr.value;
    free(d->bytes);
    return yetty_ygui_super_void(obj, yetty_ygui_yvideo_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

YETTY_ANNOTATE("override@ygui:yvideo:widget_emit_container")
static struct yetty_ycore_void_result emit_container(struct yetty_yclass_object *yclass_obj,
                                                     struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "yvideo emit_container: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    struct yetty_ycore_uint32_result id_res = yetty_ygui_widget_id(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "yvideo emit_container: id");
    /* Content-grid kind, not a content-type kind — see yplot.c (#685). */
    return yetty_ygui_emit_ensure_child(ctx, id_res.value, yetty_yfigure_kind_token("yscroll"),
                                        r.min.x, r.min.y, r.max.x, r.max.y, NULL, 0);
}

YETTY_ANNOTATE("override@ygui:yvideo:widget_emit_body")
static struct yetty_ycore_void_result emit_body(struct yetty_yclass_object *yclass_obj,
                                                struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_yvideo_ptr_result d_dr = yetty_ygui_yvideo_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "emit_body: data_get");
    struct yetty_ygui_yvideo *d = d_dr.value;
    if (!d->bytes || d->len == 0) {
        return YETTY_OK_VOID();
    }
    /* The receiver-side YVIDEO figure is a ygrid (factory registered under
     * YETTY_YFIGURE_KIND_YVIDEO), so the body must be a drawable_list holding
     * one yvideo complex prim — NOT raw MP4 bytes. yvideo-mp4 does the
     * demux + render in one shot. */
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "yvideo_emit_body: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    struct yetty_ycore_uint32_result id_res = yetty_ygui_widget_id(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "yvideo_emit_body: id");
    /* Emission gate (#457) — see yimage_emit_body: skip the multi-MB body
     * (and the full MP4 demux building it) when the receiver already holds
     * this exact content at these exact bounds. */
    if (d->emitted_generation == d->content_generation && d->emitted_rect.min.x == r.min.x &&
        d->emitted_rect.min.y == r.min.y && d->emitted_rect.max.x == r.max.x &&
        d->emitted_rect.max.y == r.max.y && yetty_ygui_emit_child_committed(ctx, id_res.value)) {
        return YETTY_OK_VOID();
    }
    struct yetty_yvideo_render_config cfg = {
        /* FIGURE-LOCAL bounds — see yplot.c: the "yscroll" child is a
         * local-coordinate figure and the receiver already anchors it at its
         * own rect origin, so an absolute rect here double-adds that origin. */
        .bounds_x = 0.0f,
        .bounds_y = 0.0f,
        .bounds_w = r.max.x - r.min.x,
        .bounds_h = r.max.y - r.min.y,
    };
    struct yetty_ydraw_drawable_list_result dlr =
        yetty_yvideo_render_from_mp4_bytes(d->bytes, d->len, &cfg);
    if (YETTY_IS_ERR(dlr)) {
        return YETTY_ERR(yetty_ycore_void, "yvideo_emit_body: render", dlr);
    }
    struct yetty_ydraw_drawable_list *dl = dlr.value;
    const void *bytes = yetty_ydraw_drawable_list_data(dl);
    size_t size = yetty_ydraw_drawable_list_size(dl);
    struct yetty_ycore_void_result er = YETTY_OK_VOID();
    if (bytes && size > 0) {
        if (size > 0xFFFFFFFFu) {
            yetty_ydraw_drawable_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void,
                             "yvideo_emit_body: rendered drawable_list exceeds wire u32 length");
        }
        /* CMD_ZERO prefix so the receiver-side ygrid clears its
         * instance/prim/cell state before consuming the fresh yvideo
         * record. Same reason as yimage_emit_body. */
        struct yetty_ydraw_drawable_list_result zlr =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        if (YETTY_IS_ERR(zlr)) {
            yetty_ydraw_drawable_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void, "yvideo_emit_body: prefix list create", zlr);
        }
        struct yetty_ydraw_drawable_list *zl = zlr.value;
        struct yetty_ycore_void_result zr = yetty_ydraw_drawable_list_add_cmd_zero(zl);
        if (YETTY_IS_ERR(zr)) {
            yetty_ydraw_drawable_list_destroy(zl);
            yetty_ydraw_drawable_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void, "yvideo_emit_body: CMD_ZERO append", zr);
        }
        const void *zbytes = yetty_ydraw_drawable_list_data(zl);
        size_t zsize = yetty_ydraw_drawable_list_size(zl);
        if (zsize > 0xFFFFFFFFu - size) {
            yetty_ydraw_drawable_list_destroy(zl);
            yetty_ydraw_drawable_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void,
                             "yvideo_emit_body: prefixed body would overflow u32");
        }
        size_t total = zsize + size;
        uint8_t *combined = malloc(total);
        if (!combined) {
            yetty_ydraw_drawable_list_destroy(zl);
            yetty_ydraw_drawable_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void, "yvideo_emit_body: combined oom");
        }
        if (zbytes && zsize > 0) {
            memcpy(combined, zbytes, zsize);
        }
        memcpy(combined + zsize, bytes, size);
        er = yetty_ygui_emit_figure_body(ctx, id_res.value, combined, (uint32_t)total);
        free(combined);
        yetty_ydraw_drawable_list_destroy(zl);
        if (YETTY_IS_OK(er)) {
            /* Latch what the receiver now holds — the gate above skips the
             * next emit unless content or bounds move past this point. */
            d->emitted_generation = d->content_generation;
            d->emitted_rect = r;
        }
    }
    yetty_ydraw_drawable_list_destroy(dl);
    return er;
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_yvideo_set_bytes(struct yetty_yclass_object *obj,
                                                           const uint8_t *bytes, size_t len)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yvideo_set_bytes: NULL");
    }
    struct yetty_ygui_yvideo_ptr_result d_dr = yetty_ygui_yvideo_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_yvideo_set_bytes: data_get");
    struct yetty_ygui_yvideo *d = d_dr.value;
    free(d->bytes);
    d->bytes = NULL;
    d->len = 0;
    if (bytes && len) {
        d->bytes = malloc(len);
        if (!d->bytes) {
            return YETTY_ERR(yetty_ycore_void, "yvideo_set_bytes: malloc");
        }
        memcpy(d->bytes, bytes, len);
        d->len = len;
    }
    d->content_generation++; /* new content — the emit gate must re-ship the body */
    return yetty_ygui_widget_set_dirty(obj);
}

#include "yetty/gen/impl/ygui/widgets/yvideo.c"
