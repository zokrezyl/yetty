/* ygui-chip.c — pill label. */
#include "paint-helpers.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * chip.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_chip_data_ptr, struct yetty_ygui_chip *);
struct yetty_yclass_ptr_result yetty_ygui_chip_class_get(void);
struct yetty_ygui_chip_data_ptr_result yetty_ygui_chip_data(struct yetty_ygui_object *obj);
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/primitive-widget.h>
#include <stdlib.h>

#define COLOR_BG 0xFF2C261Eu
#define COLOR_TEXT 0xFFE4E5E0u

struct [[clang::annotate("class@ygui:chip")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] [[clang::annotate("uses@ygui:clickable")]] yetty_ygui_chip {
    char *label;
    int closable;
};

[[clang::annotate("override@ygui:chip:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_chip_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "chip: super");
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_chip_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ctor: data_get");
    struct yetty_ygui_chip *d = d_dr.value;
    d->label = NULL;
    d->closable = 0;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:chip:destructor")]]
static struct yetty_ycore_void_result dtor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_chip_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "dtor: data_get");
    struct yetty_ygui_chip *d = d_dr.value;
    free(d->label);
    return yetty_ygui_super_void(obj, yetty_ygui_chip_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:chip:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "chip paint: NULL ctx");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_chip_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "paint: data_get");
    struct yetty_ygui_chip *d = d_dr.value;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result result_71 =
        yguix_box(ctx, r.min.x, r.min.y, w, h, COLOR_BG, h * 0.5f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_71, "chip: bg");
    if (d->label) {
        float fs = h * 0.55f;
        float ty = r.min.y + (h + fs) * 0.5f - 3.0f;
        struct yetty_ycore_void_result result_76 =
            yguix_text(ctx, d->label, r.min.x + h * 0.5f, ty, fs, COLOR_TEXT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_76, "chip: label");
    }
    if (d->closable) {
        float fs = h * 0.55f;
        float ty = r.min.y + (h + fs) * 0.5f - 3.0f;
        struct yetty_ycore_void_result result_83 =
            yguix_text(ctx, "x", r.max.x - h * 0.5f - 4, ty, fs, COLOR_TEXT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_83, "chip: x");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_chip_set_label(struct yetty_ygui_object *obj,
                                                         const char *label)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "chip_set_label: NULL");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_chip_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_chip_set_label: data_get");
    struct yetty_ygui_chip *d = d_dr.value;
    free(d->label);
    d->label = NULL;
    if (label) {
        size_t n = strlen(label);
        d->label = malloc(n + 1);
        if (!d->label) {
            return YETTY_ERR(yetty_ycore_void, "chip_set_label: malloc");
        }
        memcpy(d->label, label, n + 1);
    }
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_chip_set_closable(struct yetty_ygui_object *obj, int c)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "chip_set_closable: NULL");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_chip_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_chip_set_closable: data_get");
    struct yetty_ygui_chip *d = d_dr.value;
    d->closable = c ? 1 : 0;
    return yetty_ygui_object_set_dirty(obj);
}

#include "chip.gen.c"
