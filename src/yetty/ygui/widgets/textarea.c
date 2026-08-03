/* ygui-textarea.c — multi-line text. Paints each '\n'-delimited line. */
#include "paint-helpers.h"
#include "yetty/gen/impl/ygui/widget.h"

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * textarea.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_textarea_ptr, struct yetty_ygui_textarea *);
struct yetty_yclass_ptr_result yetty_ygui_textarea_class_get(void);
struct yetty_ygui_textarea_ptr_result yetty_ygui_textarea_from(struct yetty_yclass_object *obj);
#include "yetty/gen/impl/ygui/primitive-widget.h"
#include <stdlib.h>

#define COLOR_BG 0xFF14100Bu
#define COLOR_BORDER 0xFF474A36u
#define COLOR_TEXT 0xFFE4E5E0u

struct YETTY_ANNOTATE("class@ygui:textarea") YETTY_ANNOTATE("parent@ygui:primitive_widget")
    yetty_ygui_textarea {
    char *text;
};

YETTY_ANNOTATE("override@ygui:textarea:constructor")
static struct yetty_ycore_void_result ctor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_textarea_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "textarea: super");
    struct yetty_ygui_textarea_ptr_result d_dr = yetty_ygui_textarea_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ctor: data_get");
    struct yetty_ygui_textarea *d = d_dr.value;
    d->text = NULL;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui:textarea:destructor")
static struct yetty_ycore_void_result dtor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_textarea_ptr_result d_dr = yetty_ygui_textarea_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "dtor: data_get");
    struct yetty_ygui_textarea *d = d_dr.value;
    free(d->text);
    return yetty_ygui_super_void(obj, yetty_ygui_textarea_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

YETTY_ANNOTATE("override@ygui:textarea:widget_paint")
static struct yetty_ycore_void_result paint(struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "textarea paint: NULL ctx");
    }
    struct yetty_ygui_textarea_ptr_result d_dr = yetty_ygui_textarea_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "paint: data_get");
    struct yetty_ygui_textarea *d = d_dr.value;
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "textarea paint: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result result_71 = yguix_box(ctx, r.min.x, r.min.y, w, h, COLOR_BG, 4);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_71, "textarea: bg");
    if (!d->text || !d->text[0]) {
        return YETTY_OK_VOID();
    }
    float fs = 13.0f;
    float ly = r.min.y + 8 + fs;
    const char *p = d->text;
    while (*p && ly < r.max.y) {
        const char *eol = p;
        while (*eol && *eol != '\n') {
            eol++;
        }
        size_t n = eol - p;
        char tmp[512];
        if (n >= sizeof(tmp)) {
            n = sizeof(tmp) - 1;
        }
        memcpy(tmp, p, n);
        tmp[n] = 0;
        struct yetty_ycore_void_result result_91 =
            yguix_text(ctx, tmp, r.min.x + 8, ly, fs, COLOR_TEXT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_91, "textarea: line");
        ly += fs * 1.3f;
        if (*eol == '\n') {
            eol++;
        }
        p = eol;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_textarea_set_text(struct yetty_yclass_object *obj,
                                                            const char *text)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "textarea_set_text: NULL");
    }
    struct yetty_ygui_textarea_ptr_result d_dr = yetty_ygui_textarea_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_textarea_set_text: data_get");
    struct yetty_ygui_textarea *d = d_dr.value;
    free(d->text);
    d->text = NULL;
    if (text) {
        size_t n = strlen(text);
        d->text = malloc(n + 1);
        if (!d->text) {
            return YETTY_ERR(yetty_ycore_void, "textarea_set_text: malloc");
        }
        memcpy(d->text, text, n + 1);
    }
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_const_char_ptr_result yetty_ygui_textarea_get_text(
    const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK(yetty_ycore_const_char_ptr, "");
    }
    struct yetty_ygui_textarea_ptr_result d_dr =
        yetty_ygui_textarea_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, d_dr, "yetty_ygui_textarea_get_text: data_get");
    struct yetty_ygui_textarea *d = d_dr.value;
    return YETTY_OK(yetty_ycore_const_char_ptr, d->text ? d->text : "");
}

#include "yetty/gen/impl/ygui/widgets/textarea.c"
