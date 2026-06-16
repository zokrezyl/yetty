/*
 * ygui-yrich_view.c — hosts a yetty_yrich_document inside a ydraw_embed.
 *
 * yrich documents (ydoc rich-text, yspreadsheet, yslides) render
 * themselves into a yetty_ydraw_drawable_list. This widget owns one such
 * document, re-renders it into a fresh draw list whenever its rect
 * changes or the document marks itself dirty, and hands the buffer to
 * the ydraw_embed base — which blits the primitive stream translated
 * to the widget's rect. Pointer input is coordinate-transformed into
 * document space and forwarded to the document's input handlers.
 *
 * The render/re-render-on-resize gate mirrors ymarkdown: layout runs
 * after set_document, so the rect is only known at emit time; a cached
 * (w, h) keeps a stationary, unchanged document from paying the render
 * cost every frame.
 */
#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * yrich_view.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_yrich_view_ptr, struct yetty_ygui_yrich_view *);
struct yetty_yclass_ptr_result yetty_ygui_yrich_view_class_get(void);
struct yetty_ygui_yrich_view_ptr_result yetty_ygui_yrich_view_from(struct yetty_yclass_object *obj);
#include <yetty/ygui/primitive-widget.h>

#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ydraw-core/text-drawable-list.h>
#include <yetty/ygui/theme.h>
#include <yetty/ygui/widgets/ydraw_embed.h>
#include <yetty/yrich/yrich-types.h>

#include <yetty/yrich/document.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <stdint.h>
#include <string.h>

/* Fallback text/page colours (brand off-white on a lifted surface) used
 * when no engine theme is reachable yet. Packed 0xAABBGGRR, matching the
 * theme + yrich colour convention. */
#define YRICH_VIEW_FALLBACK_TEXT 0xFFE4E5E0u
#define YRICH_VIEW_FALLBACK_PAGE 0xFF1F1A14u

#define YRICH_VIEW_TYPE_BASE(t) ((uint32_t)(t) & ~YETTY_YDRAW_HAS_ID_FLAG)

struct [[clang::annotate("class@ygui:yrich_view"),
         clang::annotate("include@yetty/yrich/yrich-types.h")]] [[clang::annotate(
    "parent@ygui:ydraw_embed")]] yetty_ygui_yrich_view {
    struct yetty_yclass_object *doc; /* yrich document object — owned iff `own` */
    int own;
    float rendered_w;
    float rendered_h;
    int pressed; /* a press is in flight → forward motion as drag */
};

YETTY_EXTERNAL_CALLBACK
static const struct yetty_yclass *yrich_view_class(void)
{
    return yetty_ygui_class_expect(yetty_ygui_yrich_view_class_get(),
                                   "yetty_ygui_yrich_view_class_get");
}

[[clang::annotate("override@ygui:yrich_view:constructor")]]
static struct yetty_ycore_void_result yrich_view_ctor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yrich_view_class(), (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yrich_view_ctor: super");
    struct yetty_yclass_void_ptr_result d_dr = yetty_yclass_object_data(obj, yrich_view_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yrich_view_ctor: data_get");
    struct yetty_ygui_yrich_view *d = d_dr.value;
    d->doc = NULL;
    d->own = 0;
    d->rendered_w = 0.0f;
    d->rendered_h = 0.0f;
    d->pressed = 0;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:yrich_view:destructor")]]
static struct yetty_ycore_void_result yrich_view_dtor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_yclass_void_ptr_result d_dr = yetty_yclass_object_data(obj, yrich_view_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yrich_view_dtor: data_get");
    struct yetty_ygui_yrich_view *d = d_dr.value;
    if (d->own && d->doc) {
        /* The ydraw_embed base owns the last render buffer and frees it
         * in its own destructor; the document only borrows it, so tear
         * the document down here without touching the buffer. */
        struct yetty_ycore_void_result buffer_res = yetty_yrich_document_set_buffer(d->doc, NULL);
        if (YETTY_IS_ERR(buffer_res)) {
            yetty_ycore_error_destroy(buffer_res.error);
        }
        struct yetty_ycore_void_result destroy_res = yetty_yrich_document_destroy(d->doc);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
    }
    d->doc = NULL;
    return yetty_ygui_super_void(obj, yrich_view_class(),
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

/* Render the document into a fresh draw list sized (w, h) and install it
 * on the ydraw_embed base. The document keeps a borrowed pointer to the
 * buffer; the base takes ownership and frees the previous one. */
/* Size of one primitive at `prim`, or 0 if malformed / truncated. Mirrors
 * the ydraw_embed walk: SDF prims are fixed-size (+ optional id word);
 * everything else carries a u32 payload_size after the type word. */
static size_t yrich_view_prim_size(const uint32_t *prim, size_t remaining)
{
    if (remaining < sizeof(uint32_t)) {
        return 0;
    }
    uint32_t type = prim[0];
    size_t sdf_bytes = yetty_ysdf_primitive_size(YRICH_VIEW_TYPE_BASE(type));
    if (sdf_bytes > 0) {
        size_t s = sdf_bytes + ((type & YETTY_YDRAW_HAS_ID_FLAG) ? sizeof(uint32_t) : 0);
        return s <= remaining ? s : 0;
    }
    if (remaining < 2 * sizeof(uint32_t)) {
        return 0;
    }
    size_t s = 2 * sizeof(uint32_t) + prim[1];
    return s <= remaining ? s : 0;
}

/* Recolour every TEXT_DRAWABLE_LIST in the freshly-rendered buffer to `color`. The
 * yrich model bakes a fixed (near-black) colour into each text run; this
 * makes content legible against the themed background. TEXT_DRAWABLE_LIST wire
 * layout (text-drawable-list.h): word[6] is the packed colour. */
static void yrich_view_retint_text(struct yetty_ydraw_drawable_list *buf, uint32_t color)
{
    uint8_t *data = (uint8_t *)yetty_ydraw_drawable_list_data(buf);
    size_t size = yetty_ydraw_drawable_list_size(buf);
    if (!data) {
        return;
    }
    size_t off = 0;
    while (off + sizeof(uint32_t) <= size) {
        uint32_t *prim = (uint32_t *)(data + off);
        size_t s = yrich_view_prim_size(prim, size - off);
        if (s == 0) {
            break;
        }
        if (YRICH_VIEW_TYPE_BASE(prim[0]) == YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST &&
            s >= 7 * sizeof(uint32_t)) {
            prim[6] = color;
        }
        off += s;
    }
}

/* Theme text / page colours, falling back to the brand defaults when the
 * widget is not yet attached to an engine. */
static void yrich_view_theme_colors(struct yetty_yclass_object *obj, uint32_t *text, uint32_t *page)
{
    struct yetty_ygui_framework *engine = yetty_ygui_widget_framework(obj);
    if (engine) {
        struct yetty_ygui_theme *theme = yetty_ygui_framework_theme(engine);
        if (theme) {
            *text = theme->text_primary;
            *page = theme->bg_secondary;
        }
    }
}

static struct yetty_ycore_void_result yrich_view_render(struct yetty_yclass_object *obj, float w,
                                                        float h)
{
    struct yetty_yclass_void_ptr_result d_dr = yetty_yclass_object_data(obj, yrich_view_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yrich_view_render: data_get");
    struct yetty_ygui_yrich_view *d = d_dr.value;
    if (!d->doc || w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_ydraw_drawable_list_result br =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "yrich_view_render: buffer create");
    struct yetty_ycore_void_result buffer_res = yetty_yrich_document_set_buffer(d->doc, br.value);
    if (YETTY_IS_ERR(buffer_res)) {
        yetty_ydraw_drawable_list_destroy(br.value);
        return YETTY_ERR(yetty_ycore_void, "yrich_view_render: set_buffer", buffer_res);
    }
    struct yetty_ycore_void_result rr = yetty_yrich_document_render(d->doc);
    if (YETTY_IS_ERR(rr)) {
        struct yetty_ycore_void_result drop_res = yetty_yrich_document_set_buffer(d->doc, NULL);
        if (YETTY_IS_ERR(drop_res)) {
            yetty_ycore_error_destroy(drop_res.error);
        }
        yetty_ydraw_drawable_list_destroy(br.value);
        return YETTY_ERR(yetty_ycore_void, "yrich_view_render: document render", rr);
    }
    struct yetty_ycore_void_result clear_res = yetty_yrich_document_clear_dirty(d->doc);
    if (YETTY_IS_ERR(clear_res)) {
        yetty_ycore_error_destroy(clear_res.error);
    }

    /* Colours come from the engine theme (no live terminal to OSC-query in
     * a windowed app). Retint the document's text so it is legible, and
     * give the surface a themed page background. */
    uint32_t text_color = YRICH_VIEW_FALLBACK_TEXT;
    uint32_t page_color = YRICH_VIEW_FALLBACK_PAGE;
    yrich_view_theme_colors(obj, &text_color, &page_color);
    yrich_view_retint_text(br.value, text_color);
    if (yetty_ygui_widget_bg(obj) != page_color) {
        struct yetty_ycore_void_result bgr = yetty_ygui_widget_set_bg_color(obj, page_color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, bgr, "yrich_view_render: page bg");
    }

    struct yetty_ycore_void_result sb = yetty_ygui_ydraw_embed_set_buffer(obj, br.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sb, "yrich_view_render: set_buffer");
    d->rendered_w = w;
    d->rendered_h = h;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:yrich_view:widget_emit_body")]]
static struct yetty_ycore_void_result yrich_view_emit_body(struct yetty_yclass_object *yclass_obj,
                                                           struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_yclass_void_ptr_result d_dr = yetty_yclass_object_data(obj, yrich_view_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yrich_view_emit_body: data_get");
    struct yetty_ygui_yrich_view *d = d_dr.value;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (d->doc &&
        (w != d->rendered_w || h != d->rendered_h || yetty_yrich_document_is_dirty(d->doc))) {
        struct yetty_ycore_void_result rr = yrich_view_render(obj, w, h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "yrich_view_emit_body: render");
    }
    /* Chain into the ydraw_embed → primitive_widget → paint super body. */
    struct yetty_yclass_method_slot_result slot_result =
        yetty_ygui_method_slot_get((yetty_yclass_method_id_t)yetty_ygui_widget_emit_body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_result, "yrich_view_emit_body: slot");
    yetty_yclass_method_slot slot = slot_result.value;
    struct yetty_yclass_impl_t_result impl_result =
        yetty_ygui_dispatch_lookup_super(yrich_view_class(), slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, impl_result, "yrich_view_emit_body: dispatch");
    yetty_yclass_impl_t impl = impl_result.value;
    if (!impl) {
        return YETTY_OK_VOID();
    }
    typedef struct yetty_ycore_void_result (*fn_t)(struct yetty_yclass_object *,
                                                   struct yetty_ygui_emit_ctx *);
    return ((fn_t)impl)(yclass_obj, ctx);
}

/* Map an absolute viewport point to document content coords. */
static void to_doc_coords(struct yetty_yclass_object *obj, float x, float y, float *dx, float *dy)
{
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    *dx = x - r.min.x;
    *dy = y - r.min.y;
}

[[clang::annotate("override@ygui:yrich_view:widget_on_press")]]
static struct yetty_ycore_int_result yrich_view_on_press(struct yetty_yclass_object *yclass_obj,
                                                         float x, float y, int button)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_yclass_void_ptr_result d_dr = yetty_yclass_object_data(obj, yrich_view_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "yrich_view_on_press: data_get");
    struct yetty_ygui_yrich_view *d = d_dr.value;
    if (!d->doc) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    float dx, dy;
    to_doc_coords(obj, x, y, &dx, &dy);
    struct yetty_ycore_void_result down_res =
        yetty_yrich_document_on_mouse_down(d->doc, dx, dy, (uint32_t)button, 0u);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, down_res, "yrich_view_on_press: mouse_down");
    d->pressed = 1;
    struct yetty_ycore_void_result result_263 = yetty_ygui_widget_set_dirty(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, result_263, "yrich_view_on_press");
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:yrich_view:widget_on_release")]]
static struct yetty_ycore_int_result yrich_view_on_release(struct yetty_yclass_object *yclass_obj,
                                                           float x, float y, int button)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_yclass_void_ptr_result d_dr = yetty_yclass_object_data(obj, yrich_view_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "yrich_view_on_release: data_get");
    struct yetty_ygui_yrich_view *d = d_dr.value;
    if (!d->doc) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    float dx, dy;
    to_doc_coords(obj, x, y, &dx, &dy);
    struct yetty_ycore_void_result up_res =
        yetty_yrich_document_on_mouse_up(d->doc, dx, dy, (uint32_t)button, 0u);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, up_res, "yrich_view_on_release: mouse_up");
    d->pressed = 0;
    struct yetty_ycore_void_result result_285 = yetty_ygui_widget_set_dirty(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, result_285, "yrich_view_on_release");
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:yrich_view:widget_on_motion")]]
static struct yetty_ycore_int_result yrich_view_on_motion(struct yetty_yclass_object *yclass_obj,
                                                          float x, float y)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_yclass_void_ptr_result d_dr = yetty_yclass_object_data(obj, yrich_view_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "yrich_view_on_motion: data_get");
    struct yetty_ygui_yrich_view *d = d_dr.value;
    if (!d->doc || !d->pressed) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    float dx, dy;
    to_doc_coords(obj, x, y, &dx, &dy);
    struct yetty_ycore_void_result drag_res =
        yetty_yrich_document_on_mouse_drag(d->doc, dx, dy, 0, 0u);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, drag_res, "yrich_view_on_motion: mouse_drag");
    struct yetty_ycore_void_result result_306 = yetty_ygui_widget_set_dirty(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, result_306, "yrich_view_on_motion");
    return YETTY_OK(yetty_ycore_int, 1);
}

/*-----------------------------------------------------------------------------
 * Public API — model attachment + host-driven keyboard forwarding.
 *---------------------------------------------------------------------------*/
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_yrich_view_set_document(struct yetty_yclass_object *obj,
                                                                  struct yetty_yclass_object *doc,
                                                                  int own)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yrich_view_set_document: NULL obj");
    }
    struct yetty_yclass_void_ptr_result d_dr = yetty_yclass_object_data(obj, yrich_view_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_yrich_view_set_document: data_get");
    struct yetty_ygui_yrich_view *d = d_dr.value;
    if (d->own && d->doc && d->doc != doc) {
        struct yetty_ycore_void_result buffer_res = yetty_yrich_document_set_buffer(d->doc, NULL);
        if (YETTY_IS_ERR(buffer_res)) {
            yetty_ycore_error_destroy(buffer_res.error);
        }
        struct yetty_ycore_void_result destroy_res = yetty_yrich_document_destroy(d->doc);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
    }
    d->doc = doc;
    d->own = own ? 1 : 0;
    /* Drop the embed buffer + force a render at the next emit. */
    struct yetty_ycore_void_result cb = yetty_ygui_ydraw_embed_set_buffer(obj, NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cb, "yrich_view_set_document: clear buffer");
    d->rendered_w = 0.0f;
    d->rendered_h = 0.0f;
    return yetty_ygui_widget_set_dirty(obj);
}

/* Force a re-render at the next emit even when neither the rect nor the
 * document's own dirty flag changed — used after a host action (toolbar
 * button, programmatic edit) mutates the document. */
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_yrich_view_invalidate(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yrich_view_invalidate: NULL obj");
    }
    struct yetty_yclass_void_ptr_result d_dr = yetty_yclass_object_data(obj, yrich_view_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_yrich_view_invalidate: data_get");
    struct yetty_ygui_yrich_view *d = d_dr.value;
    d->rendered_w = 0.0f;
    d->rendered_h = 0.0f;
    return yetty_ygui_widget_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_yclass_object *yetty_ygui_yrich_view_document(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return NULL;
    }
    struct yetty_ygui_yrich_view *d =
        yetty_yclass_object_data((struct yetty_yclass_object *)obj, yrich_view_class()).value;
    return d->doc;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_yrich_view_content_size(
    const struct yetty_yclass_object *obj, float *w, float *h)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yrich_view_content_size: NULL obj");
    }
    struct yetty_yclass_void_ptr_result d_dr =
        yetty_yclass_object_data((struct yetty_yclass_object *)obj, yrich_view_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_yrich_view_content_size: data_get");
    struct yetty_ygui_yrich_view *d = d_dr.value;
    if (w) {
        *w = 0.0f;
        if (d->doc) {
            struct yetty_ycore_float_result width_res = yetty_yrich_document_content_width(d->doc);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, width_res,
                                "yrich_view_content_size: content_width");
            *w = width_res.value;
        }
    }
    if (h) {
        *h = 0.0f;
        if (d->doc) {
            struct yetty_ycore_float_result height_res =
                yetty_yrich_document_content_height(d->doc);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, height_res,
                                "yrich_view_content_size: content_height");
            *h = height_res.value;
        }
    }
    return YETTY_OK_VOID();
}

/* Re-fit the widget's authored size to the document's content extent so
 * the surrounding scrollarea tracks growth/shrink after edits. */
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_yrich_view_fit_content(struct yetty_yclass_object *obj)
{
    float content_w = 0.0f;
    float content_h = 0.0f;
    struct yetty_ycore_void_result size_res =
        yetty_ygui_yrich_view_content_size(obj, &content_w, &content_h);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, size_res, "yrich_view_fit_content: content_size");
    if (content_w < 1.0f) {
        content_w = 1.0f;
    }
    if (content_h < 1.0f) {
        content_h = 1.0f;
    }
    return yetty_ygui_widget_set_size(obj, content_w, content_h);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_yrich_view_feed_key(struct yetty_yclass_object *obj,
                                                              uint32_t key, uint32_t mods)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yrich_view_feed_key: NULL obj");
    }
    struct yetty_yclass_void_ptr_result d_dr = yetty_yclass_object_data(obj, yrich_view_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_yrich_view_feed_key: data_get");
    struct yetty_ygui_yrich_view *d = d_dr.value;
    if (!d->doc) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result kr = yetty_yrich_document_on_key_down(d->doc, key, mods);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, kr, "yrich_view_feed_key");
    return yetty_ygui_widget_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_yrich_view_feed_text(struct yetty_yclass_object *obj,
                                                               const char *text, size_t len)
{
    if (!obj || !text) {
        return YETTY_ERR(yetty_ycore_void, "yrich_view_feed_text: NULL arg");
    }
    struct yetty_yclass_void_ptr_result d_dr = yetty_yclass_object_data(obj, yrich_view_class());
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_yrich_view_feed_text: data_get");
    struct yetty_ygui_yrich_view *d = d_dr.value;
    if (!d->doc) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_buffer text_buffer = {
        .data = (uint8_t *)text,
        .size = len,
        .capacity = len,
    };
    struct yetty_ycore_void_result text_res =
        yetty_yrich_document_on_text_input(d->doc, text_buffer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "yrich_view_feed_text");
    return yetty_ygui_widget_set_dirty(obj);
}

#include "yrich_view.gen.c"
