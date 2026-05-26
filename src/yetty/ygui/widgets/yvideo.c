/* ygui-yvideo.c — same shape as yimage, figure_kind = YVIDEO. */
#include "../internal.h"
#include <yetty/yfigure/wire.h>
#include <yetty/ygui/widgets/yvideo.h>
#include <stdlib.h>
#include <string.h>

struct yvideo_data {
    uint8_t *bytes;
    size_t len;
};

static struct yetty_ycore_void_result ctor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_yvideo_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yvideo: super");
    struct yvideo_data *d = yetty_ygui_data_get(obj, yetty_ygui_yvideo_class_get());
    d->bytes = NULL;
    d->len = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result dtor(struct yetty_ygui_object *obj)
{
    struct yvideo_data *d = yetty_ygui_data_get(obj, yetty_ygui_yvideo_class_get());
    free(d->bytes);
    return yetty_ygui_super_void(obj, yetty_ygui_yvideo_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_void_result emit_container(struct yetty_ygui_object *obj,
                                                     struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    return yetty_ygui_emit_ensure_child(ctx, yetty_ygui_object_id(obj), YETTY_YFIGURE_KIND_YVIDEO,
                                        r.min.x, r.min.y, r.max.x, r.max.y, NULL, 0);
}

static struct yetty_ycore_void_result emit_body(struct yetty_ygui_object *obj,
                                                struct yetty_ygui_emit_ctx *ctx)
{
    struct yvideo_data *d = yetty_ygui_data_get(obj, yetty_ygui_yvideo_class_get());
    if (!d->bytes || d->len == 0) return YETTY_OK_VOID();
    if (d->len > 0xFFFFFFFFu) return YETTY_ERR(yetty_ycore_void, "yvideo: too large");
    return yetty_ygui_emit_figure_body(ctx, yetty_ygui_object_id(obj), d->bytes,
                                       (uint32_t)d->len);
}

struct yetty_ycore_void_result yetty_ygui_yvideo_set_bytes(struct yetty_ygui_object *obj,
                                                           const uint8_t *bytes, size_t len)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yvideo_set_bytes: NULL");
    struct yvideo_data *d = yetty_ygui_data_get(obj, yetty_ygui_yvideo_class_get());
    free(d->bytes);
    d->bytes = NULL;
    d->len = 0;
    if (bytes && len) {
        d->bytes = malloc(len);
        if (!d->bytes) return YETTY_ERR(yetty_ycore_void, "yvideo_set_bytes: malloc");
        memcpy(d->bytes, bytes, len);
        d->len = len;
    }
    return yetty_ygui_object_set_dirty(obj);
}


static const struct yetty_ygui_op yvideo_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, ctor),
    YETTY_YGUI_OP(yetty_ygui_destructor, dtor),
    YETTY_YGUI_OP(yetty_ygui_widget_emit_container, emit_container),
    YETTY_YGUI_OP(yetty_ygui_widget_emit_body, emit_body),
};

static const struct yetty_ygui_class_descriptor yvideo_desc = {
    .name = "yetty_ygui_yvideo",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct yvideo_data),
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_yvideo_class_get, &yvideo_desc, yvideo_ops, yetty_ygui_widget_class_get(), NULL)
