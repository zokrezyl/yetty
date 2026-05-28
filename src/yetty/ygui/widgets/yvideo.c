/* ygui-yvideo.c — same shape as yimage, figure_kind = YVIDEO. */
#include "../internal.h"
#include <yetty/yfigure/wire.h>
#include <yetty/ygui/widgets/yvideo.h>
#include <stdlib.h>
#include <string.h>

struct [[clang::annotate("class@ygui:yvideo")]]
       [[clang::annotate("parent@ygui:widget")]] yvideo_data {
    uint8_t *bytes;
    size_t len;
};

[[clang::annotate("override@ygui:yvideo:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_yvideo_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yvideo: super");
    struct yvideo_data *d = yetty_ygui_data_get(obj, yetty_ygui_yvideo_class_get().value);
    d->bytes = NULL;
    d->len = 0;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:yvideo:destructor")]]
static struct yetty_ycore_void_result dtor(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    struct yvideo_data *d = yetty_ygui_data_get(obj, yetty_ygui_yvideo_class_get().value);
    free(d->bytes);
    return yetty_ygui_super_void(obj, yetty_ygui_yvideo_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:yvideo:widget_emit_container")]]
static struct yetty_ycore_void_result emit_container(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj,
                                                     struct yetty_ygui_emit_ctx *ctx)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    return yetty_ygui_emit_ensure_child(ctx, yetty_ygui_object_id(obj), YETTY_YFIGURE_KIND_YVIDEO,
                                        r.min.x, r.min.y, r.max.x, r.max.y, NULL, 0);
}

[[clang::annotate("override@ygui:yvideo:widget_emit_body")]]
static struct yetty_ycore_void_result emit_body(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj,
                                                struct yetty_ygui_emit_ctx *ctx)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    struct yvideo_data *d = yetty_ygui_data_get(obj, yetty_ygui_yvideo_class_get().value);
    if (!d->bytes || d->len == 0) return YETTY_OK_VOID();
    if (d->len > 0xFFFFFFFFu) return YETTY_ERR(yetty_ycore_void, "yvideo: too large");
    return yetty_ygui_emit_figure_body(ctx, yetty_ygui_object_id(obj), d->bytes,
                                       (uint32_t)d->len);
}

struct yetty_ycore_void_result yetty_ygui_yvideo_set_bytes(struct yetty_ygui_object *obj,
                                                           const uint8_t *bytes, size_t len)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yvideo_set_bytes: NULL");
    struct yvideo_data *d = yetty_ygui_data_get(obj, yetty_ygui_yvideo_class_get().value);
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

#include "yvideo.gen.c"
