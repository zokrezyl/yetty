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

#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ygui/widgets/yimage.h>
#include <yetty/yimage/yimage.h>
#include <stdlib.h>
#include <string.h>

struct [[clang::annotate("class@ygui:yimage")]] [[clang::annotate("parent@ygui:widget")]]
yimage_data {
    uint8_t *bytes;
    size_t len;
};

[[clang::annotate("override@ygui:yimage:constructor")]]
static struct yetty_ycore_void_result yimage_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                         struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_yimage_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yimage_constructor: super");
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_yimage_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yimage_constructor: data_get");
    struct yimage_data *d = d_dr.value;
    d->bytes = NULL;
    d->len = 0;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:yimage:destructor")]]
static struct yetty_ycore_void_result yimage_destructor(struct yetty_yclass_ctx *yclass_ctx,
                                                        struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_yimage_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yimage_destructor: data_get");
    struct yimage_data *d = d_dr.value;
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
[[clang::annotate("override@ygui:yimage:widget_emit_container")]]
static struct yetty_ycore_void_result yimage_emit_container(struct yetty_yclass_ctx *yclass_ctx,
                                                            struct yetty_yclass_object *yclass_obj,
                                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    return yetty_ygui_emit_ensure_child(ctx, yetty_ygui_object_id(obj), YETTY_YFIGURE_KIND_YIMAGE,
                                        r.min.x, r.min.y, r.max.x, r.max.y,
                                        /*init_payload=*/NULL, /*init_payload_bytes=*/0);
}

[[clang::annotate("override@ygui:yimage:widget_emit_body")]]
static struct yetty_ycore_void_result yimage_emit_body(struct yetty_yclass_ctx *yclass_ctx,
                                                       struct yetty_yclass_object *yclass_obj,
                                                       struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_yimage_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yimage_emit_body: data_get");
    struct yimage_data *d = d_dr.value;
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
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    struct yetty_yimage_render_config cfg = {
        .bounds_x = 0.0f,
        .bounds_y = 0.0f,
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
        er = yetty_ygui_emit_figure_body(ctx, yetty_ygui_object_id(obj), combined, (uint32_t)total);
        free(combined);
        yetty_ydraw_drawable_list_destroy(zl);
    }
    yetty_ydraw_drawable_list_destroy(dl);
    return er;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_yimage_set_bytes(struct yetty_ygui_object *obj,
                                                           const uint8_t *bytes, size_t len)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_yimage_set_bytes: NULL obj");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_yimage_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_yimage_set_bytes: data_get");
    struct yimage_data *d = d_dr.value;
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
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_const_uint8_ptr_result yetty_ygui_yimage_bytes(
    const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_uint8_ptr, "yetty_ygui_yimage_bytes: invalid args");
    }
    struct yetty_ygui_void_ptr_result d_dr = yetty_ygui_data_get_result(
        (struct yetty_ygui_object *)obj, yetty_ygui_yimage_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_uint8_ptr, d_dr, "yetty_ygui_yimage_bytes: data_get");
    struct yimage_data *d = d_dr.value;
    return YETTY_OK(yetty_ycore_const_uint8_ptr, d->bytes);
}

[[clang::annotate("expose")]]
struct yetty_ycore_size_result yetty_ygui_yimage_bytes_len(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_size, "yetty_ygui_yimage_bytes_len: invalid args");
    }
    struct yetty_ygui_void_ptr_result d_dr = yetty_ygui_data_get_result(
        (struct yetty_ygui_object *)obj, yetty_ygui_yimage_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, d_dr, "yetty_ygui_yimage_bytes_len: data_get");
    struct yimage_data *d = d_dr.value;
    return YETTY_OK(yetty_ycore_size, d->len);
}

#include "yimage.gen.c"
