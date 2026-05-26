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
 * same id resets the YIMAGE figure's draw_list, keeping its GPU
 * pipeline cache hot (see yfigure/figure.h reset_content contract).
 */

#include "../internal.h"

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ygui/widgets/yimage.h>
#include <yetty/yimage/yimage.h>
#include <stdlib.h>
#include <string.h>

struct yimage_data {
    uint8_t *bytes;
    size_t len;
};

static struct yetty_ycore_void_result yimage_constructor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_yimage_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yimage_constructor: super");
    struct yimage_data *d = yetty_ygui_data_get(obj, yetty_ygui_yimage_class_get());
    d->bytes = NULL;
    d->len = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yimage_destructor(struct yetty_ygui_object *obj)
{
    struct yimage_data *d = yetty_ygui_data_get(obj, yetty_ygui_yimage_class_get());
    free(d->bytes);
    d->bytes = NULL;
    d->len = 0;
    return yetty_ygui_super_void(obj, yetty_ygui_yimage_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

/* Mint the receiver-side YIMAGE figure on first emit; on subsequent
 * emits the helper switches to SET_CHILD_RECT so the binder cache
 * stays warm. The kind is hardcoded here — the framework class
 * system doesn't know about figure kinds. */
static struct yetty_ycore_void_result yimage_emit_container(struct yetty_ygui_object *obj,
                                                            struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    return yetty_ygui_emit_ensure_child(ctx, yetty_ygui_object_id(obj), YETTY_YFIGURE_KIND_YIMAGE,
                                        r.min.x, r.min.y, r.max.x, r.max.y,
                                        /*init_payload=*/NULL, /*init_payload_bytes=*/0);
}

static struct yetty_ycore_void_result yimage_emit_body(struct yetty_ygui_object *obj,
                                                       struct yetty_ygui_emit_ctx *ctx)
{
    struct yimage_data *d = yetty_ygui_data_get(obj, yetty_ygui_yimage_class_get());
    if (!d->bytes || d->len == 0) {
        /* Nothing to ship — figure stays alive at its current rect
         * with empty content. */
        return YETTY_OK_VOID();
    }
    /* The receiver-side YIMAGE figure is in fact a ygrid (the platform
     * registers ygrid's factory under YETTY_YFIGURE_KIND_YIMAGE so all
     * producer widgets share one renderer), so the body bytes must be
     * a ydraw draw_list containing one yimage complex prim — NOT the
     * raw JPEG/PNG bytes. yetty_yimage_render builds that draw_list. */
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    struct yetty_yimage_render_config cfg = {
        .bounds_x = 0.0f,
        .bounds_y = 0.0f,
        .bounds_w = r.max.x - r.min.x,
        .bounds_h = r.max.y - r.min.y,
    };
    struct yetty_ydraw_draw_list_result dlr = yetty_yimage_render(d->bytes, d->len, &cfg);
    if (YETTY_IS_ERR(dlr)) {
        return YETTY_ERR(yetty_ycore_void, "yimage_emit_body: yimage_render", dlr);
    }
    struct yetty_ydraw_draw_list *dl = dlr.value;
    const void *bytes = yetty_ydraw_draw_list_data(dl);
    size_t size = yetty_ydraw_draw_list_size(dl);
    struct yetty_ycore_void_result er = YETTY_OK_VOID();
    if (bytes && size > 0 && size <= 0xFFFFFFFFu) {
        er = yetty_ygui_emit_figure_body(ctx, yetty_ygui_object_id(obj), (const uint8_t *)bytes,
                                         (uint32_t)size);
    }
    yetty_ydraw_draw_list_destroy(dl);
    return er;
}

struct yetty_ycore_void_result yetty_ygui_yimage_set_bytes(struct yetty_ygui_object *obj,
                                                           const uint8_t *bytes, size_t len)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_yimage_set_bytes: NULL obj");
    }
    struct yimage_data *d = yetty_ygui_data_get(obj, yetty_ygui_yimage_class_get());
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

const uint8_t *yetty_ygui_yimage_bytes(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return NULL;
    }
    struct yimage_data *d =
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, yetty_ygui_yimage_class_get());
    return d->bytes;
}

size_t yetty_ygui_yimage_bytes_len(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return 0;
    }
    struct yimage_data *d =
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, yetty_ygui_yimage_class_get());
    return d->len;
}


static const struct yetty_ygui_op yimage_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, yimage_constructor),
    YETTY_YGUI_OP(yetty_ygui_destructor, yimage_destructor),
    YETTY_YGUI_OP(yetty_ygui_widget_emit_container, yimage_emit_container),
    YETTY_YGUI_OP(yetty_ygui_widget_emit_body, yimage_emit_body),
};

static const struct yetty_ygui_class_descriptor yimage_desc = {
    .name = "yetty_ygui_yimage",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct yimage_data),
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_yimage_class_get, &yimage_desc, yimage_ops, yetty_ygui_widget_class_get(), NULL)
