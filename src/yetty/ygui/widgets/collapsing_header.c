/* collapsing_header.c — togglable vbox section with a header strip.
 *
 * Layout shape: a flex-column vbox whose `padding_top` reserves the
 * header strip, so children flow in the body below it. Painting draws,
 * bottom-to-top: a framed body box behind the children (only when open),
 * the header strip, a chevron triangle (▼ open / ▶ closed), the title,
 * and an accent hover outline over the strip.
 *
 * Collapse model: the new toolkit has no intrinsic content sizing, so a
 * section's open height is authored by the app. On close we remember
 * that height, shrink to the header strip, and mark every child
 * `hidden` so the layout + emit passes fold them away. On open we
 * restore the remembered height and clear the children's `hidden` flag.
 */
#include "paint-helpers.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * collapsing_header.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_collapsing_header_ptr, struct yetty_ygui_collapsing_header *);
struct yetty_yclass_ptr_result yetty_ygui_collapsing_header_class_get(void);
struct yetty_ygui_collapsing_header_ptr_result yetty_ygui_collapsing_header_from(
    struct yetty_yclass_object *obj);
#include <yetty/ygui/widgets/vbox.h>
#include <stdlib.h>

#define HEADER_H 28.0f
#define CH_RADIUS 6.0f
#define CH_CHEVRON_SIZE 9.0f

/* Brand palette, packed 0xAABBGGRR to match the paint pipeline. */
#define CH_BG_BODY 0xFF1F1A14u   /* BRAND_BG_LIFTED   (20, 26, 31)  */
#define CH_BG_HEADER 0xFF2C261Eu /* BRAND_BG_ROW      (30, 38, 44)  */
#define CH_BORDER 0xFF474A36u    /* BRAND_BORDER      (54, 74, 71)  */
#define CH_TITLE 0xFFE4E5E0u     /* BRAND_TEXT_PRIMARY (224,229,228) */
#define CH_CHEVRON 0xFF92A86Bu   /* BRAND_ACCENT      (107,168,146) */
#define CH_HOVER 0xFFA5C574u     /* BRAND_ACCENT_BRIGHT (116,197,165) */

struct [[clang::annotate("class@ygui:collapsing_header")]] [[clang::annotate("parent@ygui:vbox")]]
yetty_ygui_collapsing_header {
    char *title;
    int open;
    /* Authored full height, captured the first time the section is
     * collapsed so reopening restores it. -1 = not yet known. */
    float open_height;
};

YETTY_EXTERNAL_CALLBACK
static const struct yetty_yclass *ch_class(void)
{
    return yetty_ygui_collapsing_header_class_get().value;
}

/* Fill + optional stroke rounded box. */
static struct yetty_ycore_void_result ch_rounded(struct yetty_ygui_emit_ctx *ctx, float x, float y,
                                                 float w, float h, uint32_t fill, uint32_t stroke,
                                                 float stroke_w, float radius)
{
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    if (radius > w * 0.5f) {
        radius = w * 0.5f;
    }
    if (radius > h * 0.5f) {
        radius = h * 0.5f;
    }
    struct yetty_ysdf_rounded_box g = {.center_x = x + w * 0.5f,
                                       .center_y = y + h * 0.5f,
                                       .half_width = w * 0.5f,
                                       .half_height = h * 0.5f,
                                       .radius_top_right = radius,
                                       .radius_bottom_right = radius,
                                       .radius_top_left = radius,
                                       .radius_bottom_left = radius};
    return yetty_ydraw_drawable_list_add_cmd_add_rounded_box(ctx->ygrid_drawable_list, 0, 0, fill,
                                                             stroke, stroke_w, &g);
}

/* Chevron: down-pointing triangle when open, right-pointing when closed,
 * centred on (cx, cy). */
static struct yetty_ycore_void_result ch_chevron(struct yetty_ygui_emit_ctx *ctx, float cx,
                                                 float cy, int open)
{
    float s = CH_CHEVRON_SIZE;
    struct yetty_ysdf_triangle t;
    if (open) {
        t.vertex_a_x = cx - s * 0.5f;
        t.vertex_a_y = cy - s * 0.35f;
        t.vertex_b_x = cx + s * 0.5f;
        t.vertex_b_y = cy - s * 0.35f;
        t.vertex_c_x = cx;
        t.vertex_c_y = cy + s * 0.45f;
    } else {
        t.vertex_a_x = cx - s * 0.35f;
        t.vertex_a_y = cy - s * 0.5f;
        t.vertex_b_x = cx - s * 0.35f;
        t.vertex_b_y = cy + s * 0.5f;
        t.vertex_c_x = cx + s * 0.45f;
        t.vertex_c_y = cy;
    }
    return yetty_ydraw_drawable_list_add_cmd_add_triangle(ctx->ygrid_drawable_list, 0, 0,
                                                          CH_CHEVRON, 0, 0, &t);
}

/* Fold the children's subtrees in/out of layout + emit. */
static struct yetty_ycore_void_result ch_set_children_hidden(struct yetty_yclass_object *obj,
                                                             int hidden)
{
    for (struct yetty_yclass_object *c = yetty_ygui_widget_first_child(obj); c;
         c = yetty_ygui_widget_next_sibling(c)) {
        struct yetty_ygui_layout cl = *yetty_ygui_widget_layout_get(c);
        if (cl.hidden == hidden) {
            continue;
        }
        cl.hidden = hidden;
        struct yetty_ycore_void_result r = yetty_ygui_widget_layout_set(c, &cl);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "collapsing_header: child hidden");
    }
    return YETTY_OK_VOID();
}

/* Reconcile own height + children visibility with d->open. */
static struct yetty_ycore_void_result ch_apply_open(struct yetty_yclass_object *obj,
                                                    struct yetty_ygui_collapsing_header *d)
{
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    if (d->open) {
        if (d->open_height > 0.0f) {
            l.height = d->open_height;
        }
    } else {
        if (l.height > HEADER_H) {
            d->open_height = l.height; /* remember authored full height */
        }
        l.height = HEADER_H;
    }
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &l);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "collapsing_header: layout");
    return ch_set_children_hidden(obj, d->open ? 0 : 1);
}

[[clang::annotate("override@ygui:collapsing_header:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, ch_class(), (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "collapsing_header: super");
    struct yetty_ygui_collapsing_header_ptr_result d_dr = yetty_ygui_collapsing_header_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ctor: data_get");
    struct yetty_ygui_collapsing_header *d = d_dr.value;
    d->title = NULL;
    d->open = 1;
    d->open_height = -1.0f;
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.padding_top = HEADER_H + 4.0f;
    l.padding_bottom = 4.0f;
    l.padding_left = 8.0f;
    l.padding_right = 8.0f;
    l.gap = 4.0f;
    return yetty_ygui_widget_layout_set(obj, &l);
}

[[clang::annotate("override@ygui:collapsing_header:destructor")]]
static struct yetty_ycore_void_result dtor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_collapsing_header_ptr_result d_dr = yetty_ygui_collapsing_header_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "dtor: data_get");
    struct yetty_ygui_collapsing_header *d = d_dr.value;
    free(d->title);
    return yetty_ygui_super_void(obj, ch_class(), (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:collapsing_header:widget_on_press")]]
static struct yetty_ycore_int_result on_press(struct yetty_yclass_object *yclass_obj, float x,
                                              float y, int btn)
{
    (void)x;
    (void)btn;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_collapsing_header_ptr_result d_dr = yetty_ygui_collapsing_header_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "on_press: data_get");
    struct yetty_ygui_collapsing_header *d = d_dr.value;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    /* Only the header strip toggles; a press below it belongs to a child. */
    if (y - r.min.y > HEADER_H) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    d->open = !d->open;
    struct yetty_ycore_void_result lr = ch_apply_open(obj, d);
    if (YETTY_IS_ERR(lr)) {
        return YETTY_ERR(yetty_ycore_int, "collapsing_header: toggle", lr);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:collapsing_header:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "collapsing_header paint: NULL ctx");
    }
    struct yetty_ygui_collapsing_header_ptr_result d_dr = yetty_ygui_collapsing_header_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "paint: data_get");
    struct yetty_ygui_collapsing_header *d = d_dr.value;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f) {
        return YETTY_OK_VOID();
    }

    /* Framed body behind the children, painted first so the strip and
     * its contents sit on top. */
    if (d->open && h > HEADER_H + 0.5f) {
        struct yetty_ycore_void_result result_221 =
            ch_rounded(ctx, r.min.x, r.min.y + HEADER_H, w, h - HEADER_H, CH_BG_BODY, CH_BORDER,
                       1.0f, CH_RADIUS);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_221, "ch: body");
    }

    /* Header strip. */
    struct yetty_ycore_void_result result_228 =
        ch_rounded(ctx, r.min.x, r.min.y, w, HEADER_H, CH_BG_HEADER, 0, 0.0f, CH_RADIUS);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_228, "ch: header");

    struct yetty_ycore_void_result result_233 =
        ch_chevron(ctx, r.min.x + 14.0f, r.min.y + HEADER_H * 0.5f, d->open);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_233, "ch: chevron");

    if (d->title) {
        float fs = 14.0f;
        float ty = r.min.y + (HEADER_H + fs) * 0.5f - 3.0f;
        struct yetty_ycore_void_result result_240 =
            yguix_text(ctx, d->title, r.min.x + 26.0f, ty, fs, CH_TITLE);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_240, "ch: title");
    }

    /* Accent outline over the strip while hovered. */
    if (yetty_ygui_widget_is_hovered(obj)) {
        struct yetty_ycore_void_result result_247 =
            ch_rounded(ctx, r.min.x, r.min.y, w, HEADER_H, 0u, CH_HOVER, 1.5f, CH_RADIUS);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_247, "ch: hover");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_collapsing_header_set_title(
    struct yetty_yclass_object *obj, const char *title)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "ch_set_title: NULL");
    }
    struct yetty_ygui_collapsing_header_ptr_result d_dr = yetty_ygui_collapsing_header_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_collapsing_header_set_title: data_get");
    struct yetty_ygui_collapsing_header *d = d_dr.value;
    free(d->title);
    d->title = NULL;
    if (title) {
        size_t n = strlen(title);
        d->title = malloc(n + 1);
        if (!d->title) {
            return YETTY_ERR(yetty_ycore_void, "ch_set_title: malloc");
        }
        memcpy(d->title, title, n + 1);
    }
    return yetty_ygui_widget_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_collapsing_header_set_open(
    struct yetty_yclass_object *obj, int open)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "ch_set_open: NULL");
    }
    struct yetty_ygui_collapsing_header_ptr_result d_dr = yetty_ygui_collapsing_header_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_collapsing_header_set_open: data_get");
    struct yetty_ygui_collapsing_header *d = d_dr.value;
    d->open = open ? 1 : 0;
    return ch_apply_open(obj, d);
}

[[clang::annotate("expose")]]
int yetty_ygui_collapsing_header_is_open(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return 0;
    }
    return ((struct yetty_ygui_collapsing_header *)yetty_ygui_collapsing_header_from(
                (struct yetty_yclass_object *)obj)
                .value)
        ->open;
}

#include "collapsing_header.gen.c"
