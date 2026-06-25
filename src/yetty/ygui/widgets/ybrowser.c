/* ygui-ybrowser.c — wraps ylexbor into a ydraw_embed.
 *
 * Rendering is deferred to emit_body so the widget rect (which only
 * exists after layout runs inside emit) is known. set_html stashes the
 * bytes; the next emit triggers a render and feeds the buffer into the
 * ydraw_embed base. Cached (w, h) gates re-renders. */
#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * ybrowser.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_ybrowser_ptr, struct yetty_ygui_ybrowser *);
struct yetty_yclass_ptr_result yetty_ygui_ybrowser_class_get(void);
struct yetty_ygui_ybrowser_ptr_result yetty_ygui_ybrowser_from(struct yetty_yclass_object *obj);
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ygui/widgets/ydraw_embed.h>
#if YETTY_YGUI_HAVE_YBROWSER
#include <yetty/ybrowser/ybrowser.h>
#endif

#include <stdlib.h>
#include <string.h>

struct YETTY_ANNOTATE("class@ygui:ybrowser") YETTY_ANNOTATE("parent@ygui:ydraw_embed")
    yetty_ygui_ybrowser {
    char *html;
    size_t html_len;
    float rendered_w;
    float rendered_h;
};

YETTY_ANNOTATE("override@ygui:ybrowser:constructor")
static struct yetty_ycore_void_result ybr_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_ybrowser_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ybrowser_ctor: super");
    struct yetty_ygui_ybrowser_ptr_result d_dr = yetty_ygui_ybrowser_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ybr_constructor: data_get");
    struct yetty_ygui_ybrowser *d = d_dr.value;
    d->html = NULL;
    d->html_len = 0;
    d->rendered_w = 0.0f;
    d->rendered_h = 0.0f;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui:ybrowser:destructor")
static struct yetty_ycore_void_result ybr_destructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_ybrowser_ptr_result d_dr = yetty_ygui_ybrowser_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ybr_destructor: data_get");
    struct yetty_ygui_ybrowser *d = d_dr.value;
    free(d->html);
    d->html = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_ybrowser_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_void_result ybr_render(struct yetty_yclass_object *yclass_obj, float w,
                                                 float h)
{
#if !YETTY_YGUI_HAVE_YBROWSER
    /* No HTML backend on this platform — render nothing, leave the embed
     * empty. set_html still stashes the bytes; they're simply never laid out. */
    (void)yclass_obj;
    (void)w;
    (void)h;
    return YETTY_OK_VOID();
#else
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_ybrowser_ptr_result d_dr = yetty_ygui_ybrowser_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ybr_render: data_get");
    struct yetty_ygui_ybrowser *d = d_dr.value;
    if (!d->html || d->html_len == 0) {
        return YETTY_OK_VOID();
    }
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_ylexbor_config cfg = {
        .viewport_width = (int)w, .viewport_height = (int)h, .default_font_size = 14.0f};
    struct yetty_ylexbor_ptr_result lr = yetty_ylexbor_create(&cfg);
    if (YETTY_IS_ERR(lr)) {
        return YETTY_ERR(yetty_ycore_void, "ybrowser_render: create", lr);
    }
    struct yetty_ylexbor *lx = lr.value;
    struct yetty_ycore_void_result hr = yetty_ylexbor_load_html(lx, d->html, d->html_len);
    if (YETTY_IS_ERR(hr)) {
        (void)yetty_ylexbor_destroy(lx);
        return YETTY_ERR(yetty_ycore_void, "ybrowser_render: load_html", hr);
    }
    struct yetty_ydraw_drawable_list_result dlr =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(dlr)) {
        (void)yetty_ylexbor_destroy(lx);
        return YETTY_ERR(yetty_ycore_void, "ybrowser_render: dl_create", dlr);
    }
    struct yetty_ycore_void_result rr = yetty_ylexbor_render(lx, dlr.value);
    (void)yetty_ylexbor_destroy(lx);
    if (YETTY_IS_ERR(rr)) {
        yetty_ydraw_drawable_list_destroy(dlr.value);
        return YETTY_ERR(yetty_ycore_void, "ybrowser_render: render", rr);
    }
    struct yetty_ycore_void_result br = yetty_ygui_ydraw_embed_set_buffer(obj, dlr.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "ybrowser_render: set_buffer");
    d->rendered_w = w;
    d->rendered_h = h;
    return YETTY_OK_VOID();
#endif
}

YETTY_ANNOTATE("override@ygui:ybrowser:widget_emit_body")
static struct yetty_ycore_void_result ybr_emit_body(struct yetty_yclass_object *yclass_obj,
                                                    struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_ybrowser_ptr_result d_dr = yetty_ygui_ybrowser_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ybr_emit_body: data_get");
    struct yetty_ygui_ybrowser *d = d_dr.value;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (d->html && (w != d->rendered_w || h != d->rendered_h)) {
        struct yetty_ycore_void_result rr = ybr_render(yclass_obj, w, h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "ybrowser_emit_body: render");
    }
    struct yetty_yclass_method_slot_result slot_result =
        yetty_ygui_method_slot_get((yetty_yclass_method_id_t)yetty_ygui_widget_emit_body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_result, "ybrowser_emit_body: slot");
    yetty_yclass_method_slot slot = slot_result.value;
    struct yetty_yclass_impl_t_result impl_result =
        yetty_ygui_dispatch_lookup_super(yetty_ygui_ybrowser_class_get().value, slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, impl_result, "ybrowser_emit_body: dispatch");
    yetty_yclass_impl_t impl = impl_result.value;
    if (!impl) {
        return YETTY_OK_VOID();
    }
    typedef struct yetty_ycore_void_result (*fn_t)(struct yetty_yclass_object *,
                                                   struct yetty_ygui_emit_ctx *);
    return ((fn_t)impl)(yclass_obj, ctx);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_ybrowser_set_html(struct yetty_yclass_object *obj,
                                                            const char *html, size_t len)
{
    if (!obj || !html) {
        return YETTY_ERR(yetty_ycore_void, "ybrowser_set_html: NULL");
    }
    struct yetty_ygui_ybrowser_ptr_result d_dr = yetty_ygui_ybrowser_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_ybrowser_set_html: data_get");
    struct yetty_ygui_ybrowser *d = d_dr.value;
    char *buf = malloc(len);
    if (len > 0 && !buf) {
        return YETTY_ERR(yetty_ycore_void, "ybrowser_set_html: malloc");
    }
    if (len > 0) {
        memcpy(buf, html, len);
    }
    free(d->html);
    d->html = buf;
    d->html_len = len;
    d->rendered_w = 0.0f;
    d->rendered_h = 0.0f;
    return yetty_ygui_widget_set_dirty(obj);
}

#include "ybrowser.gen.c"
