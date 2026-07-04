/*
 * yimage.c — figure widget wrapping the YIMAGE figure kind.
 *
 * Subclass of the base widget (NOT primitive_widget): yimage mints
 * its own receiver-side figure via CREATE_CHILD(kind=YIMAGE) in
 * emit_container, and ships its decoded-image bytes as the figure
 * body in emit_body. The kind code lives privately inside this
 * file — the framework's class system stays generic.
 *
 * In-place reset on the receiver: a fresh body record targeting the
 * same id resets the YIMAGE figure's drawable_list, keeping its GPU
 * pipeline cache hot (see yfigure/figure.h reset_content contract).
 */

#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * yimage.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_yimage_ptr, struct yetty_ygui_yimage *);
struct yetty_yclass_ptr_result yetty_ygui_yimage_class_get(void);
struct yetty_ygui_yimage_ptr_result yetty_ygui_yimage_from(struct yetty_yclass_object *obj);

#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yfigure/wire.h>
#include <yetty/yimage/yimage.h>
#include <stdlib.h>
#include <string.h>

struct YETTY_ANNOTATE("class@ygui:yimage") YETTY_ANNOTATE("parent@ygui:widget") yetty_ygui_yimage {
    uint8_t *bytes;
    size_t len;
    /* Body-emission gate (#457). The dirty flag fires for paint reasons too
     * (hover enter/leave marks widgets dirty), but the figure BODY — the full
     * decoded image as a drawable_list, megabytes on the wire — only needs
     * re-shipping when the content or the baked-in bounds changed.
     * content_generation is bumped by set_bytes; emitted_* record what the
     * last emitted body was built from. */
    uint64_t content_generation;
    uint64_t emitted_generation;
    struct yetty_ycore_rectangle emitted_rect;
};

YETTY_ANNOTATE("override@ygui:yimage:constructor")
static struct yetty_ycore_void_result yimage_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_yimage_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yimage_constructor: super");
    struct yetty_ygui_yimage_ptr_result d_dr = yetty_ygui_yimage_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yimage_constructor: data_get");
    struct yetty_ygui_yimage *d = d_dr.value;
    d->bytes = NULL;
    d->len = 0;
    d->content_generation = 1; /* differs from emitted_generation=0 → first emit ships */
    d->emitted_generation = 0;
    memset(&d->emitted_rect, 0, sizeof(d->emitted_rect));
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui:yimage:destructor")
static struct yetty_ycore_void_result yimage_destructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_yimage_ptr_result d_dr = yetty_ygui_yimage_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yimage_destructor: data_get");
    struct yetty_ygui_yimage *d = d_dr.value;
    free(d->bytes);
    d->bytes = NULL;
    d->len = 0;
    return yetty_ygui_super_void(obj, yetty_ygui_yimage_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

/* Mint the receiver-side YIMAGE figure on first emit; on subsequent
 * emits the helper switches to SET_CHILD_RECT so the binder cache
 * stays warm. The kind is hardcoded here — the framework class
 * system doesn't know about figure kinds. */
YETTY_ANNOTATE("override@ygui:yimage:widget_emit_container")
static struct yetty_ycore_void_result yimage_emit_container(struct yetty_yclass_object *yclass_obj,
                                                            struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "yimage_emit_container: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    struct yetty_ycore_uint32_result id_res = yetty_ygui_widget_id(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "yimage_emit_container: id");
    return yetty_ygui_emit_ensure_child(ctx, id_res.value, yetty_yfigure_kind_token("yimage"),
                                        r.min.x, r.min.y, r.max.x, r.max.y,
                                        /*init_payload=*/NULL, /*init_payload_bytes=*/0);
}

YETTY_ANNOTATE("override@ygui:yimage:widget_emit_body")
static struct yetty_ycore_void_result yimage_emit_body(struct yetty_yclass_object *yclass_obj,
                                                       struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_yimage_ptr_result d_dr = yetty_ygui_yimage_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yimage_emit_body: data_get");
    struct yetty_ygui_yimage *d = d_dr.value;
    if (!d->bytes || d->len == 0) {
        /* Nothing to ship — figure stays alive at its current rect
         * with empty content. */
        return YETTY_OK_VOID();
    }
    /* The receiver-side YIMAGE figure is in fact a ygrid (the platform
     * registers ygrid's factory under YETTY_YFIGURE_KIND_YIMAGE so all
     * producer widgets share one renderer), so the body bytes must be
     * a ydraw drawable_list containing one yimage complex prim — NOT the
     * raw JPEG/PNG bytes. yetty_yimage_render builds that drawable_list. */
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "yimage_emit_body: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    struct yetty_ycore_uint32_result id_res = yetty_ygui_widget_id(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "yimage_emit_body: id");
    /* Emission gate (#457): the dirty flag also fires for pure paint reasons
     * (hover churn), but the receiver already holds this exact body when the
     * content generation and the baked-in bounds are unchanged since the last
     * emit AND the figure's mint was committed by an earlier successful flush.
     * Without this gate every hover transition re-rendered and re-shipped the
     * full multi-MB body, ballooning the wire queue until the app appeared
     * frozen. (After a FAILED flush the mint stays uncommitted, so first-body
     * delivery is never skipped; a failed flush is surfaced as an emit error
     * and the client treats the link as broken.) */
    if (d->emitted_generation == d->content_generation && d->emitted_rect.min.x == r.min.x &&
        d->emitted_rect.min.y == r.min.y && d->emitted_rect.max.x == r.max.x &&
        d->emitted_rect.max.y == r.max.y && yetty_ygui_emit_child_committed(ctx, id_res.value)) {
        return YETTY_OK_VOID();
    }
    struct yetty_yimage_render_config cfg = {
        /* Absolute widget rect — see yplot.c (producer figure is absolute in
         * the ygui chrome, content scaled by content_scale). */
        .bounds_x = r.min.x,
        .bounds_y = r.min.y,
        .bounds_w = r.max.x - r.min.x,
        .bounds_h = r.max.y - r.min.y,
    };
    struct yetty_ydraw_drawable_list_result dlr = yetty_yimage_render(d->bytes, d->len, &cfg);
    if (YETTY_IS_ERR(dlr)) {
        return YETTY_ERR(yetty_ycore_void, "yimage_emit_body: yimage_render", dlr);
    }
    struct yetty_ydraw_drawable_list *dl = dlr.value;
    const void *bytes = yetty_ydraw_drawable_list_data(dl);
    size_t size = yetty_ydraw_drawable_list_size(dl);
    struct yetty_ycore_void_result er = YETTY_OK_VOID();
    if (bytes && size > 0) {
        if (size > 0xFFFFFFFFu) {
            yetty_ydraw_drawable_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void,
                             "yimage_emit_body: rendered drawable_list exceeds wire u32 length");
        }
        /* Receiver-side YIMAGE figure is a ygrid whose process_bytes
         * APPENDS composite instances on every body — without a
         * CMD_ZERO prefix every emit stacks a fresh 800x800 yimage
         * texture on top of the previous one, exhausting GPU memory
         * and leaving stale instances drawn underneath the current
         * image. Prefix CMD_ZERO so the receiver's ygrid clears its
         * instance/prim/cell state before consuming the fresh yimage
         * record. (The chrome ygrid's stream already starts with
         * CMD_ZERO via framework_emit; figure bodies need the same.) */
        struct yetty_ydraw_drawable_list_result zlr =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        if (YETTY_IS_ERR(zlr)) {
            yetty_ydraw_drawable_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void, "yimage_emit_body: prefix list create", zlr);
        }
        struct yetty_ydraw_drawable_list *zl = zlr.value;
        struct yetty_ycore_void_result zr = yetty_ydraw_drawable_list_add_cmd_zero(zl);
        if (YETTY_IS_ERR(zr)) {
            yetty_ydraw_drawable_list_destroy(zl);
            yetty_ydraw_drawable_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void, "yimage_emit_body: CMD_ZERO append", zr);
        }
        const void *zbytes = yetty_ydraw_drawable_list_data(zl);
        size_t zsize = yetty_ydraw_drawable_list_size(zl);
        if (zsize > 0xFFFFFFFFu - size) {
            yetty_ydraw_drawable_list_destroy(zl);
            yetty_ydraw_drawable_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void,
                             "yimage_emit_body: prefixed body would overflow u32");
        }
        size_t total = zsize + size;
        uint8_t *combined = malloc(total);
        if (!combined) {
            yetty_ydraw_drawable_list_destroy(zl);
            yetty_ydraw_drawable_list_destroy(dl);
            return YETTY_ERR(yetty_ycore_void, "yimage_emit_body: combined oom");
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
struct yetty_ycore_void_result yetty_ygui_yimage_set_bytes(struct yetty_yclass_object *obj,
                                                           const uint8_t *bytes, size_t len)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_yimage_set_bytes: NULL obj");
    }
    struct yetty_ygui_yimage_ptr_result d_dr = yetty_ygui_yimage_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_yimage_set_bytes: data_get");
    struct yetty_ygui_yimage *d = d_dr.value;
    free(d->bytes);
    d->bytes = NULL;
    d->len = 0;
    if (bytes && len > 0) {
        d->bytes = malloc(len);
        if (!d->bytes) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_yimage_set_bytes: malloc");
        }
        memcpy(d->bytes, bytes, len);
        d->len = len;
    }
    d->content_generation++; /* new content — the emit gate must re-ship the body */
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_const_uint8_ptr_result yetty_ygui_yimage_bytes(
    const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_uint8_ptr, "yetty_ygui_yimage_bytes: invalid args");
    }
    struct yetty_ygui_yimage_ptr_result d_dr =
        yetty_ygui_yimage_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_uint8_ptr, d_dr, "yetty_ygui_yimage_bytes: data_get");
    struct yetty_ygui_yimage *d = d_dr.value;
    return YETTY_OK(yetty_ycore_const_uint8_ptr, d->bytes);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_size_result yetty_ygui_yimage_bytes_len(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_size, "yetty_ygui_yimage_bytes_len: invalid args");
    }
    struct yetty_ygui_yimage_ptr_result d_dr =
        yetty_ygui_yimage_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, d_dr, "yetty_ygui_yimage_bytes_len: data_get");
    struct yetty_ygui_yimage *d = d_dr.value;
    return YETTY_OK(yetty_ycore_size, d->len);
}

#include "yimage.gen.c"
