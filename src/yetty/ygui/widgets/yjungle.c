/* ygui-yjungle.c — wraps yetty_yjungle_render into a ydraw_embed.
 *
 * Same shape as ygui-ymaze.c / ygui-yzoo.c: the widget owns a
 * yetty_yjungle and, on each emit_body, advances + renders the full chain
 * for the current time into a fresh ydraw buffer (flat prim list, via
 * yetty_yjungle_render) and hands it to the ydraw_embed base. Self-dirties
 * after every emit so the framework keeps re-emitting — that animates the
 * growing chain. */
#include "../internal.h"
#include <yetty/ygui/widgets/ydraw_embed.h>
#include <yetty/ygui/widgets/yjungle.h>
#include <yetty/yjungle/yjungle.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yplatform/time.h>

struct [[clang::annotate("class@ygui:yjungle")]] [[clang::annotate("parent@ygui:ydraw_embed")]]
yjungle_data {
    struct yetty_yjungle *jungle;
    double start_time;
    float scene_w;
    float scene_h;
};

[[clang::annotate("override@ygui:yjungle:constructor")]]
static struct yetty_ycore_void_result yjungle_ctor(struct yetty_yclass_ctx *yclass_ctx,
                                                   struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj,
        yetty_ygui_yjungle_class_get().value,
        (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yjungle_ctor: super");
    struct yjungle_data *d = yetty_ygui_data_get(
        obj,
        yetty_ygui_yjungle_class_get().value);

    struct yetty_yjungle_config cfg = yetty_yjungle_config_default();
    struct yetty_yjungle_ptr_result jr = yetty_yjungle_create(&cfg, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, jr, "yjungle_ctor: jungle create");
    d->jungle = jr.value;
    d->start_time = yetty_yplatform_ytime_monotonic_sec();
    d->scene_w = 0.0f;
    d->scene_h = 0.0f;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:yjungle:destructor")]]
static struct yetty_ycore_void_result yjungle_dtor(struct yetty_yclass_ctx *yclass_ctx,
                                                   struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yjungle_data *d = yetty_ygui_data_get(
        obj,
        yetty_ygui_yjungle_class_get().value);
    yetty_yjungle_destroy(d->jungle);
    d->jungle = NULL;
    return yetty_ygui_super_void(
        obj,
        yetty_ygui_yjungle_class_get().value,
        (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:yjungle:widget_emit_body")]]
static struct yetty_ycore_void_result yjungle_emit_body(struct yetty_yclass_ctx *yclass_ctx,
                                                        struct yetty_yclass_object *yclass_obj,
                                                        struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yjungle_data *d = yetty_ygui_data_get(
        obj,
        yetty_ygui_yjungle_class_get().value);

    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (d->jungle && w > 0.0f && h > 0.0f) {
        if (w != d->scene_w || h != d->scene_h) {
            (void)yetty_yjungle_set_scene_size(d->jungle, w, h);
            d->scene_w = w;
            d->scene_h = h;
        }
        struct yetty_ydraw_draw_list_config bcfg = {
            .scene_min_x = 0.0f, .scene_min_y = 0.0f, .scene_max_x = w, .scene_max_y = h};
        struct yetty_ydraw_draw_list_result br = yetty_ydraw_draw_list_config_buffer_create(&bcfg);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "yjungle_emit_body: buffer create");
        uint64_t now_ms =
            (uint64_t)((yetty_yplatform_ytime_monotonic_sec() - d->start_time) * 1000.0);
        struct yetty_ycore_void_result rr = yetty_yjungle_render(d->jungle, br.value, now_ms);
        if (YETTY_IS_ERR(rr)) {
            yetty_ydraw_draw_list_destroy(br.value);
            return YETTY_ERR(yetty_ycore_void, "yjungle_emit_body: render", rr);
        }
        struct yetty_ycore_void_result sb = yetty_ygui_ydraw_embed_set_buffer(obj, br.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sb, "yjungle_emit_body: set_buffer");
        (void)yetty_ygui_object_set_dirty(obj);
    }

    yetty_yclass_method_slot slot =
        yetty_ygui_method_slot_get((yetty_yclass_method_id_t)yetty_ygui_widget_emit_body);
    yetty_yclass_impl_t impl = yetty_ygui_dispatch_lookup_super(
        yetty_ygui_yjungle_class_get().value,
        slot);
    if (!impl) {
        return YETTY_OK_VOID();
    }
    typedef struct yetty_ycore_void_result (*fn_t)(
        struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);
    return ((fn_t)impl)(NULL, yclass_obj, ctx);
}

#include "yjungle.gen.c"
