/* ygui-ymaze.c — wraps yetty_ymaze_render into a ydraw_embed.
 *
 * Mirrors ygui-ymarkdown.c: the widget owns a yetty_ymaze and, on each
 * emit_body, renders the maze for the current time into a fresh ydraw
 * buffer and hands it to the ydraw_embed base (which paints it into the
 * hosting ygrid). The widget re-marks itself dirty after every emit so the
 * framework keeps re-emitting — that is what animates the actor. */
#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * ymaze.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_ymaze_ptr, struct yetty_ygui_ymaze *);
struct yetty_yclass_ptr_result yetty_ygui_ymaze_class_get(void);
struct yetty_ygui_ymaze_ptr_result yetty_ygui_ymaze_from(struct yetty_yclass_object *obj);
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/ydraw_embed.h>
#include <yetty/ymaze/ymaze.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yplatform/time.h>

struct [[clang::annotate("class@ygui:ymaze")]] [[clang::annotate("parent@ygui:ydraw_embed")]]
yetty_ygui_ymaze {
    struct yetty_ymaze *maze;
    double start_time;
    float scene_w;
    float scene_h;
};

[[clang::annotate("override@ygui:ymaze:constructor")]]
static struct yetty_ycore_void_result ymz_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_ymaze_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ymaze_ctor: super");
    struct yetty_ygui_ymaze_ptr_result d_dr = yetty_ygui_ymaze_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ymz_constructor: data_get");
    struct yetty_ygui_ymaze *d = d_dr.value;

    struct yetty_ymaze_config cfg = yetty_ymaze_config_default();
    struct yetty_ymaze_ptr_result mr = yetty_ymaze_create(&cfg, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mr, "ymaze_ctor: maze create");
    d->maze = mr.value;
    d->start_time = yetty_yplatform_ytime_monotonic_sec();
    d->scene_w = 0.0f;
    d->scene_h = 0.0f;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:ymaze:destructor")]]
static struct yetty_ycore_void_result ymz_destructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_ymaze_ptr_result d_dr = yetty_ygui_ymaze_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ymz_destructor: data_get");
    struct yetty_ygui_ymaze *d = d_dr.value;
    yetty_ymaze_destroy(d->maze);
    d->maze = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_ymaze_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:ymaze:widget_emit_body")]]
static struct yetty_ycore_void_result ymz_emit_body(struct yetty_yclass_object *yclass_obj,
                                                    struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_ymaze_ptr_result d_dr = yetty_ygui_ymaze_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ymz_emit_body: data_get");
    struct yetty_ygui_ymaze *d = d_dr.value;

    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (d->maze && w > 0.0f && h > 0.0f) {
        if (w != d->scene_w || h != d->scene_h) {
            (void)yetty_ymaze_set_scene_size(d->maze, w, h);
            d->scene_w = w;
            d->scene_h = h;
        }
        struct yetty_ydraw_drawable_list_config bcfg = {
            .scene_min_x = 0.0f,
            .scene_min_y = 0.0f,
            .scene_max_x = w,
            .scene_max_y = h,
        };
        struct yetty_ydraw_drawable_list_result br =
            yetty_ydraw_drawable_list_config_buffer_create(&bcfg);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "ymaze_emit_body: buffer create");
        float t = (float)(yetty_yplatform_ytime_monotonic_sec() - d->start_time);
        struct yetty_ycore_void_result rr = yetty_ymaze_render(d->maze, br.value, t, NULL);
        if (YETTY_IS_ERR(rr)) {
            yetty_ydraw_drawable_list_destroy(br.value);
            return YETTY_ERR(yetty_ycore_void, "ymaze_emit_body: render", rr);
        }
        /* set_buffer takes ownership of the buffer. */
        struct yetty_ycore_void_result sb = yetty_ygui_ydraw_embed_set_buffer(obj, br.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sb, "ymaze_emit_body: set_buffer");
        /* Keep the frame pump alive — the maze is animated. */
        (void)yetty_ygui_widget_set_dirty(obj);
    }

    /* Forward to super's emit_body (ydraw_embed paints the buffer). */
    struct yetty_yclass_method_slot_result slot_result =
        yetty_ygui_method_slot_get((yetty_yclass_method_id_t)yetty_ygui_widget_emit_body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_result, "ymaze_emit_body: slot");
    yetty_yclass_method_slot slot = slot_result.value;
    struct yetty_yclass_impl_t_result impl_result =
        yetty_ygui_dispatch_lookup_super(yetty_ygui_ymaze_class_get().value, slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, impl_result, "ymaze_emit_body: dispatch");
    yetty_yclass_impl_t impl = impl_result.value;
    if (!impl) {
        return YETTY_OK_VOID();
    }
    typedef struct yetty_ycore_void_result (*fn_t)(struct yetty_yclass_object *,
                                                   struct yetty_ygui_emit_ctx *);
    return ((fn_t)impl)(yclass_obj, ctx);
}

#include "ymaze.gen.c"
