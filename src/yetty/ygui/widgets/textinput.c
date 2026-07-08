/*
 * ygui-textinput.c — single-line text input.
 *
 * Inherits primitive_widget + clickable. Click-to-focus; the app reads
 * the focused widget and routes typed chars there. Backspace deletes a
 * char.
 */

#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * textinput.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_textinput_ptr, struct yetty_ygui_textinput *);
struct yetty_yclass_ptr_result yetty_ygui_textinput_class_get(void);
struct yetty_ygui_textinput_ptr_result yetty_ygui_textinput_from(struct yetty_yclass_object *obj);

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yfont/font.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ysdf/funcs.gen.h>

#include <stdlib.h>
#include <string.h>

#define COLOR_BG 0xFF14100Bu
#define COLOR_BORDER 0xFF474A36u
#define COLOR_BORDER_FOCUS 0xFF92A86Bu
#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_PLACEHOLDER 0xFFA8A79Fu
/* Selection band — a muted slate-blue that reads under the near-white text
 * (no alpha compositing, so it must be a solid mid-tone). Colours here are
 * packed 0xAABBGGRR (see COLOR_BORDER_FOCUS == brand mint #6BA892). */
#define COLOR_SELECTION 0xFF7A6033u

struct YETTY_ANNOTATE("class@ygui:textinput") YETTY_ANNOTATE("parent@ygui:primitive_widget")
    yetty_ygui_textinput {
    char *text;
    size_t text_len;
    size_t cap;
    char *placeholder;
    int focused;
    size_t cursor; /* caret byte offset into text (0..text_len) */
    /* Selection is the byte range [min(sel_anchor,cursor), max(...)). When
     * sel_anchor == cursor there is no selection (just the caret). A drag or a
     * shifted caret move grows it from the anchor; a plain click collapses it. */
    size_t sel_anchor;
    int dragging; /* left button held after a press inside the field */
};

/* Font metrics used for caret placement + click-to-position. TEXTINPUT_CHAR_W
 * is only the FALLBACK advance, used when no measurement font is wired into the
 * framework; when one is present the caret and click hit-test measure the real
 * glyphs so they line up exactly with the drawn text (see textinput_font). */
#define TEXTINPUT_FONT_SIZE 14.0f
#define TEXTINPUT_TEXT_PAD 10.0f
#define TEXTINPUT_CHAR_W (TEXTINPUT_FONT_SIZE * 0.55f)

/* The framework's measurement font (the same font the shared ygrid renders text
 * with), or NULL when none is wired — then the callers fall back to CHAR_W. */
static struct yetty_yfont_font *textinput_font(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_object_ptr_result fw_res = yetty_ygui_widget_framework(obj);
    if (YETTY_IS_ERR(fw_res)) {
        yetty_ycore_error_destroy(fw_res.error);
        return NULL;
    }
    return yetty_ygui_framework_font(fw_res.value);
}

/* Pixel width of the first `n` bytes of the field text at the field font size.
 * Uses the real font when available so the caret sits between the drawn glyphs;
 * else falls back to the fixed per-char advance. */
static float textinput_prefix_width(struct yetty_yfont_font *font,
                                    const struct yetty_ygui_textinput *d, size_t n)
{
    if (!d->text || n == 0) {
        return 0.0f;
    }
    if (n > d->text_len) {
        n = d->text_len;
    }
    if (font && font->ops && font->ops->measure_text) {
        struct float_result mr = font->ops->measure_text(font, d->text, n, TEXTINPUT_FONT_SIZE);
        if (YETTY_IS_OK(mr)) {
            return mr.value;
        }
        yetty_ycore_error_destroy(mr.error);
    }
    return (float)n * TEXTINPUT_CHAR_W;
}

/* Byte index of the caret nearest text-relative x `rel` (0 = start of text).
 * Walks real glyph advances so a click lands between the correct characters
 * (including at the very end); falls back to the fixed advance. Field text is
 * ASCII (handle_key only inserts 0x20..0x7e), so byte == codepoint here. */
static size_t textinput_index_at_x(struct yetty_yfont_font *font,
                                   const struct yetty_ygui_textinput *d, float rel)
{
    if (rel <= 0.0f || !d->text || d->text_len == 0) {
        return 0;
    }
    if (font && font->ops && font->ops->get_advance) {
        float acc = 0.0f;
        for (size_t i = 0; i < d->text_len; i++) {
            struct float_result ar = font->ops->get_advance(
                font, (uint32_t)(unsigned char)d->text[i], TEXTINPUT_FONT_SIZE);
            float adv = TEXTINPUT_CHAR_W;
            if (YETTY_IS_OK(ar)) {
                adv = ar.value;
            } else {
                yetty_ycore_error_destroy(ar.error);
            }
            if (rel < acc + adv * 0.5f) {
                return i;
            }
            acc += adv;
        }
        return d->text_len;
    }
    long idx = (long)((rel / TEXTINPUT_CHAR_W) + 0.5f);
    if (idx < 0) {
        idx = 0;
    }
    if ((size_t)idx > d->text_len) {
        idx = (long)d->text_len;
    }
    return (size_t)idx;
}

/* Selection bounds as a half-open byte range [lo, hi). */
static size_t sel_lo(const struct yetty_ygui_textinput *d)
{
    return d->sel_anchor < d->cursor ? d->sel_anchor : d->cursor;
}
static size_t sel_hi(const struct yetty_ygui_textinput *d)
{
    return d->sel_anchor < d->cursor ? d->cursor : d->sel_anchor;
}
static int has_selection(const struct yetty_ygui_textinput *d)
{
    return d->sel_anchor != d->cursor;
}

/* Delete the current selection (if any), leaving the caret at its start and no
 * selection. Returns 1 if text changed. */
static int delete_selection(struct yetty_ygui_textinput *d)
{
    if (!has_selection(d)) {
        return 0;
    }
    size_t lo = sel_lo(d), hi = sel_hi(d);
    memmove(d->text + lo, d->text + hi, d->text_len - hi);
    d->text_len -= (hi - lo);
    d->text[d->text_len] = '\0';
    d->cursor = lo;
    d->sel_anchor = lo;
    return 1;
}

/* Byte index under a widget-relative x pixel (accounts for the text padding). */
static size_t textinput_index_at_press(struct yetty_yclass_object *obj,
                                       struct yetty_ygui_textinput *d, float px)
{
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    if (YETTY_IS_ERR(rect_res)) {
        yetty_ycore_error_destroy(rect_res.error);
        return d->cursor;
    }
    float rel = px - (rect_res.value.min.x + TEXTINPUT_TEXT_PAD);
    return textinput_index_at_x(textinput_font(obj), d, rel);
}

/* Mouse handling is done with the widget's own press/motion/release virtuals
 * rather than the clickable+draggable mixins: those two mixins BOTH override
 * widget_on_press/on_release, so using them together makes the drag lose its
 * press capture and selection never starts. Owning the three virtuals here
 * gives a clean press→drag→release selection gesture and the button code (so a
 * right-click can raise a context menu). */
YETTY_ANNOTATE("override@ygui:textinput:widget_on_press")
static struct yetty_ycore_int_result textinput_on_press(struct yetty_yclass_object *yclass_obj,
                                                        float x, float y, int button)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    (void)y;
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "textinput_on_press: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    d->focused = 1;
    if (button == 0) { /* left — place caret and begin a (possibly empty) selection */
        d->cursor = textinput_index_at_press(obj, d, x);
        d->sel_anchor = d->cursor;
        d->dragging = 1;
    }
    struct yetty_ycore_void_result dr = yetty_ygui_widget_set_dirty(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dr, "textinput_on_press: dirty");
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("override@ygui:textinput:widget_on_motion")
static struct yetty_ycore_int_result textinput_on_motion(struct yetty_yclass_object *yclass_obj,
                                                         float x, float y)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    (void)y;
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "textinput_on_motion: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    if (!d->dragging) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    /* Extend the selection: the anchor stays put, the caret tracks the cursor. */
    d->cursor = textinput_index_at_press(obj, d, x);
    struct yetty_ycore_void_result dr = yetty_ygui_widget_set_dirty(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dr, "textinput_on_motion: dirty");
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("override@ygui:textinput:widget_on_release")
static struct yetty_ycore_int_result textinput_on_release(struct yetty_yclass_object *yclass_obj,
                                                          float x, float y, int button)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    (void)x;
    (void)y;
    (void)button;
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "textinput_on_release: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    d->dragging = 0;
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("override@ygui:textinput:constructor")
static struct yetty_ycore_void_result textinput_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_textinput_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "textinput_constructor: super");
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "textinput_constructor: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    d->text = NULL;
    d->text_len = 0;
    d->cap = 0;
    d->placeholder = NULL;
    d->focused = 0;
    d->cursor = 0;
    d->sel_anchor = 0;
    d->dragging = 0;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui:textinput:destructor")
static struct yetty_ycore_void_result textinput_destructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "textinput_destructor: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    free(d->text);
    free(d->placeholder);
    d->text = NULL;
    d->placeholder = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_textinput_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_void_result paint_box(struct yetty_ygui_emit_ctx *ctx, float x, float y,
                                                float w, float h, uint32_t fill, float radius)
{
    if (radius <= 0.0f) {
        struct yetty_ysdf_box geom = {
            .center_x = x + w * 0.5f,
            .center_y = y + h * 0.5f,
            .half_width = w * 0.5f,
            .half_height = h * 0.5f,
        };
        return yetty_ydraw_drawable_list_add_cmd_add_box(ctx->ygrid_drawable_list, 0u, 0u, fill, 0u,
                                                         0.0f, &geom);
    }
    if (radius > w * 0.5f) {
        radius = w * 0.5f;
    }
    if (radius > h * 0.5f) {
        radius = h * 0.5f;
    }
    struct yetty_ysdf_rounded_box geom = {
        .center_x = x + w * 0.5f,
        .center_y = y + h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
        .radius_top_right = radius,
        .radius_bottom_right = radius,
        .radius_top_left = radius,
        .radius_bottom_left = radius,
    };
    return yetty_ydraw_drawable_list_add_cmd_add_rounded_box(ctx->ygrid_drawable_list, 0u, 0u, fill,
                                                             0u, 0.0f, &geom);
}

YETTY_ANNOTATE("override@ygui:textinput:widget_paint")
static struct yetty_ycore_void_result textinput_paint(struct yetty_yclass_object *yclass_obj,
                                                      struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "textinput_paint: NULL ctx");
    }
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "textinput_paint: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "textinput_paint: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    /* Outline = 1.5px frame: paint the border-colored rounded box, then
     * inset the fill on top so a thin ring remains. Without a visible frame
     * the field is invisible whenever its fill matches the surrounding
     * surface (e.g. a toolbar on the same brand background). */
    uint32_t border = d->focused ? COLOR_BORDER_FOCUS : COLOR_BORDER;
    struct yetty_ycore_void_result rr = paint_box(ctx, r.min.x, r.min.y, w, h, border, 4.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "textinput_paint: border");
    const float inset = 1.5f;
    if (w > 2.0f * inset && h > 2.0f * inset) {
        rr = paint_box(ctx, r.min.x + inset, r.min.y + inset, w - 2.0f * inset, h - 2.0f * inset,
                       COLOR_BG, 3.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "textinput_paint: bg");
    }
    /* Selection highlight — a filled rounded band behind the selected glyphs,
     * painted before the text so the glyphs read on top. Measured against the
     * real font so it lines up with the drawn characters. */
    if (d->focused && has_selection(d) && d->text && d->text[0]) {
        struct yetty_yfont_font *font = textinput_font(obj);
        float x0 = textinput_prefix_width(font, d, sel_lo(d));
        float x1 = textinput_prefix_width(font, d, sel_hi(d));
        float sx = r.min.x + TEXTINPUT_TEXT_PAD + x0;
        float sw = x1 - x0;
        if (sw > 0.0f) {
            struct yetty_ysdf_box geom = {
                .center_x = sx + sw * 0.5f,
                .center_y = r.min.y + h * 0.5f,
                .half_width = sw * 0.5f,
                .half_height = (h - 8.0f) * 0.5f,
            };
            rr = yetty_ydraw_drawable_list_add_cmd_add_box(ctx->ygrid_drawable_list, 0u, 0u,
                                                           COLOR_SELECTION, 0u, 0.0f, &geom);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "textinput_paint: selection");
        }
    }
    const char *text = (d->text && d->text[0]) ? d->text : d->placeholder;
    uint32_t color = (d->text && d->text[0]) ? COLOR_TEXT : COLOR_PLACEHOLDER;
    if (text && text[0]) {
        float fs = 14.0f;
        float ty = r.min.y + (h + fs) * 0.5f - 3.0f;
        struct yetty_ycore_buffer tb = {
            .data = (uint8_t *)text, .capacity = strlen(text), .size = strlen(text)};
        rr = yetty_ydraw_drawable_list_add_text(ctx->ygrid_drawable_list, r.min.x + 10.0f, ty, &tb,
                                                fs, color, 0, -1, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "textinput_paint: text");
    }
    if (d->focused) {
        /* Caret — thin vertical bar at the cursor, measured against the real
         * font so it sits between the drawn glyphs. */
        float text_w = textinput_prefix_width(textinput_font(obj), d, d->cursor);
        struct yetty_ysdf_box geom = {
            .center_x = r.min.x + TEXTINPUT_TEXT_PAD + text_w + 1.0f,
            .center_y = r.min.y + h * 0.5f,
            .half_width = 1.0f,
            .half_height = (h - 8.0f) * 0.5f,
        };
        rr = yetty_ydraw_drawable_list_add_cmd_add_box(ctx->ygrid_drawable_list, 0u, 0u,
                                                       COLOR_BORDER_FOCUS, 0u, 0.0f, &geom);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "textinput_paint: caret");
    }
    return YETTY_OK_VOID();
}

static int ensure_cap(struct yetty_ygui_textinput *d, size_t need)
{
    if (need <= d->cap) {
        return 1;
    }
    size_t cap = d->cap ? d->cap * 2 : 32;
    while (cap < need) {
        cap *= 2;
    }
    char *nb = realloc(d->text, cap);
    if (!nb) {
        return 0;
    }
    d->text = nb;
    d->cap = cap;
    return 1;
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_textinput_set_text(struct yetty_yclass_object *obj,
                                                             const char *text)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "textinput_set_text: NULL obj");
    }
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_textinput_set_text: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    size_t n = text ? strlen(text) : 0;
    if (!ensure_cap(d, n + 1)) {
        return YETTY_ERR(yetty_ycore_void, "textinput_set_text: realloc");
    }
    if (text) {
        memcpy(d->text, text, n);
    }
    if (d->text) {
        d->text[n] = '\0';
    }
    d->text_len = n;
    d->cursor = n; /* caret to end on programmatic set */
    d->sel_anchor = n;
    return yetty_ygui_widget_set_dirty(obj);
}

/* Selected substring as a fresh NUL-terminated heap string (caller frees), or
 * NULL when nothing is selected. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_char_ptr_result yetty_ygui_textinput_get_selection(
    const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_char_ptr, "textinput_get_selection: NULL obj");
    }
    struct yetty_ygui_textinput_ptr_result d_dr =
        yetty_ygui_textinput_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, d_dr, "textinput_get_selection: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    if (!has_selection(d) || !d->text) {
        return YETTY_OK(yetty_ycore_char_ptr, NULL);
    }
    size_t lo = sel_lo(d), hi = sel_hi(d);
    size_t len = hi - lo;
    char *out = malloc(len + 1);
    if (!out) {
        return YETTY_ERR(yetty_ycore_char_ptr, "textinput_get_selection: malloc");
    }
    memcpy(out, d->text + lo, len);
    out[len] = '\0';
    return YETTY_OK(yetty_ycore_char_ptr, out);
}

/* Select the whole field (Ctrl-A / context-menu "Select All"). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_textinput_select_all(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "textinput_select_all: NULL obj");
    }
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "textinput_select_all: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    d->sel_anchor = 0;
    d->cursor = d->text_len;
    d->focused = 1;
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_const_char_ptr_result yetty_ygui_textinput_get_text(
    const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ygui_textinput_get_text: invalid args");
    }
    struct yetty_ygui_textinput_ptr_result d_dr =
        yetty_ygui_textinput_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, d_dr,
                        "yetty_ygui_textinput_get_text: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    return YETTY_OK(yetty_ycore_const_char_ptr, d->text ? d->text : "");
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_textinput_set_placeholder(struct yetty_yclass_object *obj,
                                                                    const char *placeholder)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "textinput_set_placeholder: NULL obj");
    }
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_textinput_set_placeholder: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    free(d->placeholder);
    if (!placeholder) {
        d->placeholder = NULL;
    } else {
        size_t n = strlen(placeholder);
        d->placeholder = malloc(n + 1);
        if (!d->placeholder) {
            return YETTY_ERR(yetty_ycore_void, "textinput_set_placeholder: malloc");
        }
        memcpy(d->placeholder, placeholder, n + 1);
    }
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_textinput_set_focus(struct yetty_yclass_object *obj,
                                                              int focused)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "textinput_set_focus: NULL obj");
    }
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_textinput_set_focus: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    d->focused = focused ? 1 : 0;
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_textinput_handle_key(struct yetty_yclass_object *obj,
                                                              uint32_t key)
{
    if (!obj) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_yclass_ptr_result class_result = yetty_ygui_textinput_class_get();
    YETTY_RETURN_IF_ERR(yetty_ycore_int, class_result, "yetty_ygui_textinput_handle_key: class");
    struct yetty_yclass_void_ptr_result d_dr = yetty_yclass_object_data(obj, class_result.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "yetty_ygui_textinput_handle_key: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    if (!d->focused) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    if (d->cursor > d->text_len) {
        d->cursor = d->text_len;
    }
    if (d->sel_anchor > d->text_len) {
        d->sel_anchor = d->text_len;
    }
    int changed = 0;
    int consumed = 1;
    switch (key) {
    case 0x08:
    case 0x7F: /* backspace — delete the selection, else the char before caret */
        if (delete_selection(d)) {
            changed = 1;
        } else if (d->cursor > 0) {
            memmove(d->text + d->cursor - 1, d->text + d->cursor, d->text_len - d->cursor);
            d->cursor--;
            d->text_len--;
            d->text[d->text_len] = '\0';
            changed = 1;
        }
        break;
    case YETTY_YGUI_KEY_DELETE: /* delete the selection, else the char at caret */
        if (delete_selection(d)) {
            changed = 1;
        } else if (d->cursor < d->text_len) {
            memmove(d->text + d->cursor, d->text + d->cursor + 1, d->text_len - d->cursor - 1);
            d->text_len--;
            d->text[d->text_len] = '\0';
            changed = 1;
        }
        break;
    case YETTY_YGUI_KEY_ARROW_LEFT:
        /* Collapse a selection to its left edge; otherwise step the caret. */
        if (has_selection(d)) {
            d->cursor = sel_lo(d);
            changed = 1;
        } else if (d->cursor > 0) {
            d->cursor--;
            changed = 1;
        }
        d->sel_anchor = d->cursor;
        break;
    case YETTY_YGUI_KEY_ARROW_RIGHT:
        if (has_selection(d)) {
            d->cursor = sel_hi(d);
            changed = 1;
        } else if (d->cursor < d->text_len) {
            d->cursor++;
            changed = 1;
        }
        d->sel_anchor = d->cursor;
        break;
    case YETTY_YGUI_KEY_HOME:
        if (d->cursor != 0 || has_selection(d)) {
            d->cursor = 0;
            changed = 1;
        }
        d->sel_anchor = d->cursor;
        break;
    case YETTY_YGUI_KEY_END:
        if (d->cursor != d->text_len || has_selection(d)) {
            d->cursor = d->text_len;
            changed = 1;
        }
        d->sel_anchor = d->cursor;
        break;
    default:
        if (key >= 32 && key < 127) { /* insert printable — replacing any selection */
            delete_selection(d);
            if (!ensure_cap(d, d->text_len + 2)) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
            memmove(d->text + d->cursor + 1, d->text + d->cursor, d->text_len - d->cursor);
            d->text[d->cursor] = (char)key;
            d->cursor++;
            d->text_len++;
            d->text[d->text_len] = '\0';
            d->sel_anchor = d->cursor;
            changed = 1;
        } else {
            consumed = 0;
        }
        break;
    }
    if (changed) {
        struct yetty_ycore_void_result r = yetty_ygui_widget_set_dirty(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, r, "yetty_ygui_textinput_handle_key: dirty");
    }
    return YETTY_OK(yetty_ycore_int, consumed);
}

#include "textinput.gen.c"
