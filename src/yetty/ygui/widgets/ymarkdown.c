/* ygui-ymarkdown.c — wraps yetty_ymarkdown_render into a ydraw_embed.
 *
 * The widget's rect is unknown at set_source time (layout runs later
 * during emit). We stash the source bytes and re-render in emit_body
 * once the current rect is settled. A cached (w, h) gates re-render
 * so a stationary widget pays the markdown cost only once. */
#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * ymarkdown.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_ymarkdown_ptr, struct yetty_ygui_ymarkdown *);
struct yetty_yclass_ptr_result yetty_ygui_ymarkdown_class_get(void);
struct yetty_ygui_ymarkdown_ptr_result yetty_ygui_ymarkdown_from(struct yetty_yclass_object *obj);
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/ydraw_embed.h>
#include <yetty/ymarkdown/ymarkdown.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct [[clang::annotate("class@ygui:ymarkdown")]] [[clang::annotate("parent@ygui:ydraw_embed")]]
yetty_ygui_ymarkdown {
    char *source;
    size_t source_len;
    float rendered_w;
    float rendered_h;
};

[[clang::annotate("override@ygui:ymarkdown:constructor")]]
static struct yetty_ycore_void_result ymd_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                      struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_ymarkdown_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ymarkdown_ctor: super");
    struct yetty_ygui_ymarkdown_ptr_result d_dr = yetty_ygui_ymarkdown_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ymd_constructor: data_get");
    struct yetty_ygui_ymarkdown *d = d_dr.value;
    d->source = NULL;
    d->source_len = 0;
    d->rendered_w = 0.0f;
    d->rendered_h = 0.0f;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:ymarkdown:destructor")]]
static struct yetty_ycore_void_result ymd_destructor(struct yetty_yclass_ctx *yclass_ctx,
                                                     struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_ymarkdown_ptr_result d_dr = yetty_ygui_ymarkdown_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ymd_destructor: data_get");
    struct yetty_ygui_ymarkdown *d = d_dr.value;
    free(d->source);
    d->source = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_ymarkdown_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_void_result ymd_render(struct yetty_yclass_ctx *yclass_ctx,
                                                 struct yetty_yclass_object *yclass_obj, float w,
                                                 float h)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_ymarkdown_ptr_result d_dr = yetty_ygui_ymarkdown_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ymd_render: data_get");
    struct yetty_ygui_ymarkdown *d = d_dr.value;
    if (!d->source || d->source_len == 0) {
        return YETTY_OK_VOID();
    }
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_ymarkdown_render_config cfg = {.cell_width = 8,
                                                .cell_height = 16,
                                                .width_cells = (uint32_t)(w / 8.0f),
                                                .height_cells = (uint32_t)(h / 16.0f)};
    struct yetty_ymarkdown_render_result rr =
        yetty_ymarkdown_render(d->source, d->source_len, NULL, 0, &cfg);
    if (YETTY_IS_ERR(rr)) {
        return YETTY_ERR(yetty_ycore_void, "ymarkdown_render", rr);
    }
    struct yetty_ycore_void_result br = yetty_ygui_ydraw_embed_set_buffer(obj, rr.value.buffer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "ymarkdown_render: set_buffer");
    d->rendered_w = w;
    d->rendered_h = h;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:ymarkdown:widget_emit_body")]]
static struct yetty_ycore_void_result ymd_emit_body(struct yetty_yclass_ctx *yclass_ctx,
                                                    struct yetty_yclass_object *yclass_obj,
                                                    struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_ymarkdown_ptr_result d_dr = yetty_ygui_ymarkdown_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ymd_emit_body: data_get");
    struct yetty_ygui_ymarkdown *d = d_dr.value;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (d->source && (w != d->rendered_w || h != d->rendered_h)) {
        struct yetty_ycore_void_result rr = ymd_render(NULL, yclass_obj, w, h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "ymarkdown_emit_body: render");
    }
    /* Forward to super's emit_body (primitive_widget → paint → ydraw_embed). */
    struct yetty_yclass_method_slot_result slot_result =
        yetty_ygui_method_slot_get((yetty_yclass_method_id_t)yetty_ygui_widget_emit_body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_result, "ymarkdown_emit_body: slot");
    yetty_yclass_method_slot slot = slot_result.value;
    struct yetty_yclass_impl_t_result impl_result =
        yetty_ygui_dispatch_lookup_super(yetty_ygui_ymarkdown_class_get().value, slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, impl_result, "ymarkdown_emit_body: dispatch");
    yetty_yclass_impl_t impl = impl_result.value;
    if (!impl) {
        return YETTY_OK_VOID();
    }
    typedef struct yetty_ycore_void_result (*fn_t)(
        struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);
    return ((fn_t)impl)(NULL, yclass_obj, ctx);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_ymarkdown_set_source(struct yetty_yclass_object *obj,
                                                               const char *src, size_t len)
{
    if (!obj || !src) {
        return YETTY_ERR(yetty_ycore_void, "ymarkdown_set_source: NULL");
    }
    struct yetty_ygui_ymarkdown_ptr_result d_dr = yetty_ygui_ymarkdown_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_ymarkdown_set_source: data_get");
    struct yetty_ygui_ymarkdown *d = d_dr.value;
    char *buf = malloc(len);
    if (len > 0 && !buf) {
        return YETTY_ERR(yetty_ycore_void, "ymarkdown_set_source: malloc");
    }
    if (len > 0) {
        memcpy(buf, src, len);
    }
    free(d->source);
    d->source = buf;
    d->source_len = len;
    /* Force re-render at the next emit even if rect hasn't moved. */
    d->rendered_w = 0.0f;
    d->rendered_h = 0.0f;
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_ymarkdown_set_file(struct yetty_yclass_object *obj,
                                                             const char *path)
{
    if (!obj || !path) {
        return YETTY_ERR(yetty_ycore_void, "ymarkdown_set_file: NULL");
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return YETTY_ERR(yetty_ycore_void, "ymarkdown_set_file: open");
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return YETTY_ERR(yetty_ycore_void, "ymarkdown_set_file: empty");
    }
    char *buf = malloc((size_t)n);
    if (!buf) {
        fclose(f);
        return YETTY_ERR(yetty_ycore_void, "ymarkdown_set_file: malloc");
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    struct yetty_ycore_void_result r = yetty_ygui_ymarkdown_set_source(obj, buf, got);
    free(buf);
    return r;
}

#include "ymarkdown.gen.c"
