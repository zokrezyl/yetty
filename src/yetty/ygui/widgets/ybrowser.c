/* ygui-ybrowser.c — wraps ylexbor into a ydraw_embed.
 *
 * Rendering is deferred to emit_body so the widget rect (which only
 * exists after layout runs inside emit) is known. set_html stashes the
 * bytes; the next emit triggers a render and feeds the buffer into the
 * ydraw_embed base. Cached (w, h) gates re-renders. */
#include "../internal.h"
#include <yetty/ybrowser/ybrowser.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/widgets/ybrowser.h>
#include <yetty/ygui/widgets/ydraw_embed.h>

#include <stdlib.h>
#include <string.h>

struct ybrowser_data {
    char *html;
    size_t html_len;
    float rendered_w;
    float rendered_h;
};

static struct yetty_ycore_void_result ybr_constructor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_ybrowser_class_get(),
                              (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ybrowser_ctor: super");
    struct ybrowser_data *d = yetty_ygui_data_get(obj, yetty_ygui_ybrowser_class_get());
    d->html = NULL;
    d->html_len = 0;
    d->rendered_w = 0.0f;
    d->rendered_h = 0.0f;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ybr_destructor(struct yetty_ygui_object *obj)
{
    struct ybrowser_data *d = yetty_ygui_data_get(obj, yetty_ygui_ybrowser_class_get());
    free(d->html);
    d->html = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_ybrowser_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_void_result ybr_render(struct yetty_ygui_object *obj, float w, float h)
{
    struct ybrowser_data *d = yetty_ygui_data_get(obj, yetty_ygui_ybrowser_class_get());
    if (!d->html || d->html_len == 0) return YETTY_OK_VOID();
    if (w <= 0.0f || h <= 0.0f) return YETTY_OK_VOID();
    struct yetty_ylexbor_config cfg = {.viewport_width = (int)w,
                                       .viewport_height = (int)h,
                                       .default_font_size = 14.0f};
    struct yetty_ylexbor_ptr_result lr = yetty_ylexbor_create(&cfg);
    if (YETTY_IS_ERR(lr)) return YETTY_ERR(yetty_ycore_void, "ybrowser_render: create", lr);
    struct yetty_ylexbor *lx = lr.value;
    struct yetty_ycore_void_result hr = yetty_ylexbor_load_html(lx, d->html, d->html_len);
    if (YETTY_IS_ERR(hr)) {
        yetty_ylexbor_destroy(lx);
        return YETTY_ERR(yetty_ycore_void, "ybrowser_render: load_html", hr);
    }
    struct yetty_ydraw_draw_list_result dlr = yetty_ydraw_draw_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(dlr)) {
        yetty_ylexbor_destroy(lx);
        return YETTY_ERR(yetty_ycore_void, "ybrowser_render: dl_create", dlr);
    }
    struct yetty_ycore_void_result rr = yetty_ylexbor_render(lx, dlr.value);
    yetty_ylexbor_destroy(lx);
    if (YETTY_IS_ERR(rr)) {
        yetty_ydraw_draw_list_destroy(dlr.value);
        return YETTY_ERR(yetty_ycore_void, "ybrowser_render: render", rr);
    }
    struct yetty_ycore_void_result br = yetty_ygui_ydraw_embed_set_buffer(obj, dlr.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "ybrowser_render: set_buffer");
    d->rendered_w = w;
    d->rendered_h = h;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ybr_emit_body(struct yetty_ygui_object *obj,
                                                    struct yetty_ygui_emit_ctx *ctx)
{
    struct ybrowser_data *d = yetty_ygui_data_get(obj, yetty_ygui_ybrowser_class_get());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (d->html && (w != d->rendered_w || h != d->rendered_h)) {
        struct yetty_ycore_void_result rr = ybr_render(obj, w, h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "ybrowser_emit_body: render");
    }
    yetty_ygui_method_slot slot =
        yetty_ygui_method_slot_get((yetty_ygui_method_id_t)yetty_ygui_widget_emit_body);
    yetty_ygui_impl_t impl =
        yetty_ygui_class_dispatch_lookup_super(yetty_ygui_ybrowser_class_get(), slot);
    if (!impl) return YETTY_OK_VOID();
    typedef struct yetty_ycore_void_result (*fn_t)(struct yetty_ygui_object *,
                                                   struct yetty_ygui_emit_ctx *);
    return ((fn_t)impl)(obj, ctx);
}

struct yetty_ycore_void_result yetty_ygui_ybrowser_set_html(struct yetty_ygui_object *obj,
                                                            const char *html, size_t len)
{
    if (!obj || !html) return YETTY_ERR(yetty_ycore_void, "ybrowser_set_html: NULL");
    struct ybrowser_data *d = yetty_ygui_data_get(obj, yetty_ygui_ybrowser_class_get());
    char *buf = malloc(len);
    if (len > 0 && !buf) return YETTY_ERR(yetty_ycore_void, "ybrowser_set_html: malloc");
    if (len > 0) memcpy(buf, html, len);
    free(d->html);
    d->html = buf;
    d->html_len = len;
    d->rendered_w = 0.0f;
    d->rendered_h = 0.0f;
    return yetty_ygui_object_set_dirty(obj);
}


static const struct yetty_ygui_op ybrowser_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, ybr_constructor),
    YETTY_YGUI_OP(yetty_ygui_destructor, ybr_destructor),
    YETTY_YGUI_OP(yetty_ygui_widget_emit_body, ybr_emit_body),
};

static const struct yetty_ygui_class_descriptor ybrowser_desc = {
    .name = "yetty_ygui_ybrowser",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct ybrowser_data),
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_ybrowser_class_get, &ybrowser_desc, ybrowser_ops, yetty_ygui_ydraw_embed_class_get(), NULL)
