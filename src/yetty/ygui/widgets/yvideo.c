/* ygui-yvideo.c — same shape as yimage, figure_kind = YVIDEO. */
#include "../internal.h"
#include <yetty/ygui/widget.h>

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
#include <yetty/yfigure/wire.h>
#include <yetty/yvideo/yvideo-mp4.h>
#include <stdlib.h>
#include <string.h>

struct YETTY_ANNOTATE("class@ygui:yvideo") YETTY_ANNOTATE("parent@ygui:widget") yetty_ygui_yvideo {
    uint8_t *bytes;
    size_t len;
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
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    return yetty_ygui_emit_ensure_child(ctx, yetty_ygui_widget_id(obj), YETTY_YFIGURE_KIND_YVIDEO,
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
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    struct yetty_yvideo_render_config cfg = {
        /* Absolute widget rect — see yplot.c (producer figure is absolute in
         * the ygui chrome, content scaled by content_scale). */
        .bounds_x = r.min.x,
        .bounds_y = r.min.y,
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
        er = yetty_ygui_emit_figure_body(ctx, yetty_ygui_widget_id(obj), combined, (uint32_t)total);
        free(combined);
        yetty_ydraw_drawable_list_destroy(zl);
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
    return yetty_ygui_widget_set_dirty(obj);
}

#include "yvideo.gen.c"
