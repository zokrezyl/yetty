/* ygui-ymarkdown.c — wraps yetty_ymarkdown_render into a ydraw_embed.
 *
 * The widget's rect is unknown at set_source time (layout runs later
 * during emit). We stash the source bytes and re-render in emit_body
 * once the current rect is settled. A cached (w, h) gates re-render
 * so a stationary widget pays the markdown cost only once. */
#include "../internal.h"
#include <yetty/ygui/widgets/ydraw_embed.h>
#include <yetty/ygui/widgets/ymarkdown.h>
#include <yetty/ymarkdown/ymarkdown.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ymarkdown_data {
    char *source;
    size_t source_len;
    float rendered_w;
    float rendered_h;
};

static struct yetty_ycore_void_result ymd_constructor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_ymarkdown_class_get(),
                              (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ymarkdown_ctor: super");
    struct ymarkdown_data *d = yetty_ygui_data_get(obj, yetty_ygui_ymarkdown_class_get());
    d->source = NULL;
    d->source_len = 0;
    d->rendered_w = 0.0f;
    d->rendered_h = 0.0f;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ymd_destructor(struct yetty_ygui_object *obj)
{
    struct ymarkdown_data *d = yetty_ygui_data_get(obj, yetty_ygui_ymarkdown_class_get());
    free(d->source);
    d->source = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_ymarkdown_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_void_result ymd_render(struct yetty_ygui_object *obj, float w, float h)
{
    struct ymarkdown_data *d = yetty_ygui_data_get(obj, yetty_ygui_ymarkdown_class_get());
    if (!d->source || d->source_len == 0) return YETTY_OK_VOID();
    if (w <= 0.0f || h <= 0.0f) return YETTY_OK_VOID();
    struct yetty_ymarkdown_render_config cfg = {.cell_width = 8,
                                                .cell_height = 16,
                                                .width_cells = (uint32_t)(w / 8.0f),
                                                .height_cells = (uint32_t)(h / 16.0f)};
    struct yetty_ymarkdown_render_result rr =
        yetty_ymarkdown_render(d->source, d->source_len, NULL, 0, &cfg);
    if (YETTY_IS_ERR(rr)) return YETTY_ERR(yetty_ycore_void, "ymarkdown_render", rr);
    struct yetty_ycore_void_result br = yetty_ygui_ydraw_embed_set_buffer(obj, rr.value.buffer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "ymarkdown_render: set_buffer");
    d->rendered_w = w;
    d->rendered_h = h;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ymd_emit_body(struct yetty_ygui_object *obj,
                                                    struct yetty_ygui_emit_ctx *ctx)
{
    struct ymarkdown_data *d = yetty_ygui_data_get(obj, yetty_ygui_ymarkdown_class_get());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (d->source && (w != d->rendered_w || h != d->rendered_h)) {
        struct yetty_ycore_void_result rr = ymd_render(obj, w, h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "ymarkdown_emit_body: render");
    }
    /* Forward to super's emit_body (primitive_widget → paint → ydraw_embed). */
    yetty_ygui_method_slot slot =
        yetty_ygui_method_slot_get((yetty_ygui_method_id_t)yetty_ygui_widget_emit_body);
    yetty_ygui_impl_t impl =
        yetty_ygui_class_dispatch_lookup_super(yetty_ygui_ymarkdown_class_get(), slot);
    if (!impl) return YETTY_OK_VOID();
    typedef struct yetty_ycore_void_result (*fn_t)(struct yetty_ygui_object *,
                                                   struct yetty_ygui_emit_ctx *);
    return ((fn_t)impl)(obj, ctx);
}

struct yetty_ycore_void_result yetty_ygui_ymarkdown_set_source(struct yetty_ygui_object *obj,
                                                               const char *src, size_t len)
{
    if (!obj || !src) return YETTY_ERR(yetty_ycore_void, "ymarkdown_set_source: NULL");
    struct ymarkdown_data *d = yetty_ygui_data_get(obj, yetty_ygui_ymarkdown_class_get());
    char *buf = malloc(len);
    if (len > 0 && !buf) return YETTY_ERR(yetty_ycore_void, "ymarkdown_set_source: malloc");
    if (len > 0) memcpy(buf, src, len);
    free(d->source);
    d->source = buf;
    d->source_len = len;
    /* Force re-render at the next emit even if rect hasn't moved. */
    d->rendered_w = 0.0f;
    d->rendered_h = 0.0f;
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_ymarkdown_set_file(struct yetty_ygui_object *obj,
                                                             const char *path)
{
    if (!obj || !path) return YETTY_ERR(yetty_ycore_void, "ymarkdown_set_file: NULL");
    FILE *f = fopen(path, "rb");
    if (!f) return YETTY_ERR(yetty_ycore_void, "ymarkdown_set_file: open");
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


static const struct yetty_ygui_op ymarkdown_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, ymd_constructor),
    YETTY_YGUI_OP(yetty_ygui_destructor, ymd_destructor),
    YETTY_YGUI_OP(yetty_ygui_widget_emit_body, ymd_emit_body),
};

static const struct yetty_ygui_class_descriptor ymarkdown_desc = {
    .name = "yetty_ygui_ymarkdown",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct ymarkdown_data),
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_ymarkdown_class_get, &ymarkdown_desc, ymarkdown_ops, yetty_ygui_ydraw_embed_class_get(), NULL)
