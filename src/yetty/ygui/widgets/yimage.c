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

#include <yetty/yfigure/wire.h>
#include <yetty/ygui/widgets/yimage.h>
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
    /* Hand-roll the record append because the wrapper's
     * yetty_ygui_emit_figure_body wants uint32_t len; clamp on the way. */
    if (d->len > 0xFFFFFFFFu) {
        return YETTY_ERR(yetty_ycore_void, "yimage_emit_body: payload too large");
    }
    return yetty_ygui_emit_figure_body(ctx, yetty_ygui_object_id(obj), d->bytes, (uint32_t)d->len);
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

const struct yetty_ygui_class *yetty_ygui_yimage_class_get(void)
{
    static const struct yetty_ygui_class *cls = NULL;
    if (cls) return cls;
    static const struct yetty_ygui_op ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, yimage_constructor),
    YETTY_YGUI_OP(yetty_ygui_destructor, yimage_destructor),
    YETTY_YGUI_OP(yetty_ygui_widget_emit_container, yimage_emit_container),
    YETTY_YGUI_OP(yetty_ygui_widget_emit_body, yimage_emit_body),
    };
    static const struct yetty_ygui_class_descriptor desc = {.name = "yetty_ygui_yimage",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct yimage_data),};
    struct yetty_ygui_class_ptr_result r = yetty_ygui_class_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), yetty_ygui_widget_class_get(), NULL, 0);
    if (YETTY_IS_ERR(r)) return NULL;
    cls = r.value;
    return cls;
}
