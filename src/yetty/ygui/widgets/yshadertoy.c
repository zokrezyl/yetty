/* ygui-yshadertoy.c — hosts a YETTY_YFIGURE_KIND_YSHADERTOY child figure.
 *
 * Same shape as ygui-yvideo.c: the widget emits a sibling figure of the
 * yshadertoy kind into the parent container (emit_container) and ships
 * its WGSL shader text to that figure as a body (emit_body). The figure
 * — src/yetty/yshadertoy/figure.c — owns the GPU pipeline, injects the
 * Shadertoy inputs (iResolution/iTime/iTimeDelta/iFrame/iMouse), and
 * animates itself; the widget only carries the source text.
 *
 * Unlike yvideo (whose body changes every frame), the shader text is
 * static, so it is shipped only when `dirty` — i.e. after
 * set_source — to avoid making the figure recompile every frame.
 */
#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * yshadertoy.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_yshadertoy_ptr, struct yetty_ygui_yshadertoy *);
struct yetty_yclass_ptr_result yetty_ygui_yshadertoy_class_get(void);
struct yetty_ygui_yshadertoy_ptr_result yetty_ygui_yshadertoy_from(struct yetty_yclass_object *obj);
#include <yetty/yfigure/wire.h>
#include <stdlib.h>
#include <string.h>

struct YETTY_ANNOTATE("class@ygui:yshadertoy") YETTY_ANNOTATE("parent@ygui:widget")
    yetty_ygui_yshadertoy {
    char *src;
    size_t len;
    int dirty; /* source changed since last ship → re-emit body */
};

YETTY_ANNOTATE("override@ygui:yshadertoy:constructor")
static struct yetty_ycore_void_result ctor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_yshadertoy_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yshadertoy: super");
    struct yetty_ygui_yshadertoy_ptr_result d_r = yetty_ygui_yshadertoy_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_r, "ctor: data");
    struct yetty_ygui_yshadertoy *d = d_r.value;
    d->src = NULL;
    d->len = 0;
    d->dirty = 0;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui:yshadertoy:destructor")
static struct yetty_ycore_void_result dtor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_yshadertoy_ptr_result d_r = yetty_ygui_yshadertoy_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_r, "dtor: data");
    struct yetty_ygui_yshadertoy *d = d_r.value;
    free(d->src);
    return yetty_ygui_super_void(obj, yetty_ygui_yshadertoy_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

YETTY_ANNOTATE("override@ygui:yshadertoy:widget_emit_container")
static struct yetty_ycore_void_result emit_container(struct yetty_yclass_object *yclass_obj,
                                                     struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    return yetty_ygui_emit_ensure_child(ctx, yetty_ygui_widget_id(obj),
                                        YETTY_YFIGURE_KIND_YSHADERTOY, r.min.x, r.min.y, r.max.x,
                                        r.max.y, NULL, 0);
}

YETTY_ANNOTATE("override@ygui:yshadertoy:widget_emit_body")
static struct yetty_ycore_void_result emit_body(struct yetty_yclass_object *yclass_obj,
                                                struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_yshadertoy_ptr_result d_r = yetty_ygui_yshadertoy_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_r, "emit_body: data");
    struct yetty_ygui_yshadertoy *d = d_r.value;
    /* Ship the WGSL only when it changed — the figure recompiles on
     * receipt, so re-sending every frame would thrash the pipeline. The
     * figure runs its own animation timer once it has a shader. */
    if (!d->dirty || !d->src || d->len == 0) {
        return YETTY_OK_VOID();
    }
    if (d->len > 0xFFFFFFFFu) {
        return YETTY_ERR(yetty_ycore_void, "yshadertoy_emit_body: shader text exceeds u32 length");
    }
    struct yetty_ycore_void_result er = yetty_ygui_emit_figure_body(
        ctx, yetty_ygui_widget_id(obj), (const uint8_t *)d->src, (uint32_t)d->len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "yshadertoy_emit_body: emit_figure_body");
    d->dirty = 0;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_yshadertoy_set_source(struct yetty_yclass_object *obj,
                                                                const char *src, size_t len)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yshadertoy_set_source: NULL");
    }
    struct yetty_ygui_yshadertoy_ptr_result d_r = yetty_ygui_yshadertoy_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_r, "yetty_ygui_yshadertoy_set_source: data");
    struct yetty_ygui_yshadertoy *d = d_r.value;
    free(d->src);
    d->src = NULL;
    d->len = 0;
    d->dirty = 0;
    if (src && len) {
        d->src = malloc(len);
        if (!d->src) {
            return YETTY_ERR(yetty_ycore_void, "yshadertoy_set_source: malloc");
        }
        memcpy(d->src, src, len);
        d->len = len;
        d->dirty = 1;
    }
    return yetty_ygui_widget_set_dirty(obj);
}

#include "yshadertoy.gen.c"
