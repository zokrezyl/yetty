/* ygui-yzoo.c — wraps yetty_yzoo_render into a ydraw_embed.
 *
 * Same shape as ygui-ymaze.c: the widget owns a yetty_yzoo and, on each
 * emit_body, renders the zoo for the current time into a fresh ydraw
 * buffer and hands it to the ydraw_embed base (which paints it into the
 * hosting ygrid). Self-dirties after every emit so the framework keeps
 * re-emitting — that animates the critters. */
#include "../internal.h"
#include <yetty/ygui/widgets/ydraw_embed.h>
#include <yetty/ygui/widgets/yzoo.h>
#include <yetty/yzoo/yzoo.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yplatform/time.h>

struct [[clang::annotate("class@ygui:yzoo")]] [[clang::annotate("parent@ygui:ydraw_embed")]]
yzoo_data {
    struct yetty_yzoo *zoo;
    double start_time;
    float scene_w;
    float scene_h;
};

[[clang::annotate("override@ygui:yzoo:constructor")]]
static struct yetty_ycore_void_result yzoo_ctor(struct yetty_yclass_ctx *yclass_ctx,
                                                struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_yzoo_class_get().value,
        (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yzoo_ctor: super");
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_yzoo_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yzoo_ctor: data_get");
    struct yzoo_data *d = d_dr.value;

    struct yetty_yzoo_config cfg = yetty_yzoo_config_default();
    struct yetty_yzoo_ptr_result zr = yetty_yzoo_create(&cfg, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, zr, "yzoo_ctor: zoo create");
    d->zoo = zr.value;
    d->start_time = yetty_yplatform_ytime_monotonic_sec();
    d->scene_w = 0.0f;
    d->scene_h = 0.0f;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:yzoo:destructor")]]
static struct yetty_ycore_void_result yzoo_dtor(struct yetty_yclass_ctx *yclass_ctx,
                                                struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_yzoo_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yzoo_dtor: data_get");
    struct yzoo_data *d = d_dr.value;
    yetty_yzoo_destroy(d->zoo);
    d->zoo = NULL;
    return yetty_ygui_super_void(
        obj, yetty_ygui_yzoo_class_get().value,
        (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:yzoo:widget_emit_body")]]
static struct yetty_ycore_void_result yzoo_emit_body(struct yetty_yclass_ctx *yclass_ctx,
                                                     struct yetty_yclass_object *yclass_obj,
                                                     struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_yzoo_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yzoo_emit_body: data_get");
    struct yzoo_data *d = d_dr.value;

    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (d->zoo && w > 0.0f && h > 0.0f) {
        if (w != d->scene_w || h != d->scene_h) {
            (void)yetty_yzoo_set_scene_size(d->zoo, w, h);
            d->scene_w = w;
            d->scene_h = h;
        }
        struct yetty_ydraw_draw_list_config bcfg = {
            .scene_min_x = 0.0f, .scene_min_y = 0.0f, .scene_max_x = w, .scene_max_y = h};
        struct yetty_ydraw_draw_list_result br = yetty_ydraw_draw_list_config_buffer_create(&bcfg);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "yzoo_emit_body: buffer create");
        float t = (float)(yetty_yplatform_ytime_monotonic_sec() - d->start_time);
        struct yetty_ycore_void_result rr = yetty_yzoo_render(d->zoo, br.value, t);
        if (YETTY_IS_ERR(rr)) {
            yetty_ydraw_draw_list_destroy(br.value);
            return YETTY_ERR(yetty_ycore_void, "yzoo_emit_body: render", rr);
        }
        struct yetty_ycore_void_result sb = yetty_ygui_ydraw_embed_set_buffer(obj, br.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sb, "yzoo_emit_body: set_buffer");
        (void)yetty_ygui_object_set_dirty(obj);
    }

    yetty_yclass_method_slot slot =
        yetty_ygui_method_slot_get((yetty_yclass_method_id_t)yetty_ygui_widget_emit_body);
    yetty_yclass_impl_t impl = yetty_ygui_dispatch_lookup_super(
        yetty_ygui_yzoo_class_get().value, slot);
    if (!impl) {
        return YETTY_OK_VOID();
    }
    typedef struct yetty_ycore_void_result (*fn_t)(
        struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);
    return ((fn_t)impl)(NULL, yclass_obj, ctx);
}

#include "yzoo.gen.c"
