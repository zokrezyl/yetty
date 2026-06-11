/* ygui-stepper.c — wizard step indicator. */
#include "paint-helpers.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * stepper.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_stepper_ptr, struct yetty_ygui_stepper *);
struct yetty_yclass_ptr_result yetty_ygui_stepper_class_get(void);
struct yetty_ygui_stepper_ptr_result yetty_ygui_stepper_from(struct yetty_yclass_object *obj);
#include <yetty/ygui/primitive-widget.h>
#include <stdio.h>
#include <stdlib.h>

#define COLOR_LINE 0xFF474A36u
#define COLOR_DONE 0xFF92A86Bu
#define COLOR_TODO 0xFF2C261Eu
#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_NUM 0xFF14100Bu

struct [[clang::annotate("class@ygui:stepper")]] [[clang::annotate("parent@ygui:primitive_widget")]]
yetty_ygui_stepper {
    char **labels;
    int n;
    int cap;
    int current;
};

[[clang::annotate("override@ygui:stepper:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_stepper_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "stepper: super");
    struct yetty_ygui_stepper_ptr_result d_dr = yetty_ygui_stepper_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ctor: data_get");
    struct yetty_ygui_stepper *d = d_dr.value;
    d->labels = NULL;
    d->n = 0;
    d->cap = 0;
    d->current = 0;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:stepper:destructor")]]
static struct yetty_ycore_void_result dtor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_stepper_ptr_result d_dr = yetty_ygui_stepper_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "dtor: data_get");
    struct yetty_ygui_stepper *d = d_dr.value;
    for (int i = 0; i < d->n; i++) {
        free(d->labels[i]);
    }
    free(d->labels);
    return yetty_ygui_super_void(obj, yetty_ygui_stepper_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:stepper:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "stepper paint: NULL ctx");
    }
    struct yetty_ygui_stepper_ptr_result d_dr = yetty_ygui_stepper_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "paint: data_get");
    struct yetty_ygui_stepper *d = d_dr.value;
    if (d->n <= 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) {
        return YETTY_OK_VOID();
    }
    float cr = 14.0f;
    float cy = r.min.y + cr + 2;
    float gap = (w - 2 * cr * d->n) / (float)(d->n > 1 ? d->n - 1 : 1);
    if (gap < 0) {
        gap = 0;
    }
    float cx = r.min.x + cr;
    /* Lines first */
    for (int i = 0; i < d->n - 1; i++) {
        float x0 = cx + (cr + gap * 0.5f) * (i == 0 ? 1 : 0) + i * (2 * cr + gap) + cr;
        (void)x0;
        float lx = r.min.x + cr + i * (2 * cr + gap) + cr;
        struct yetty_ycore_void_result result_98 = yguix_box(
            ctx, lx, cy - 1, 2 * cr + gap, 2, i < d->current ? COLOR_DONE : COLOR_LINE, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_98, "stepper: line");
    }
    for (int i = 0; i < d->n; i++) {
        uint32_t col = i <= d->current ? COLOR_DONE : COLOR_TODO;
        struct yetty_ycore_void_result result_105 = yguix_circle(ctx, cx, cy, cr, col);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_105, "stepper: c");
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", i + 1);
        struct yetty_ycore_void_result result_108 =
            yguix_text(ctx, buf, cx - 4, cy + 5, 13.0f, COLOR_NUM);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_108, "stepper: num");
        if (d->labels[i]) {
            struct yetty_ycore_void_result result_111 =
                yguix_text(ctx, d->labels[i], cx - 24, cy + cr + 14, 12.0f, COLOR_TEXT);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, result_111, "stepper: label");
        }
        cx += 2 * cr + gap;
    }
    return YETTY_OK_VOID();
}

static int grow(struct yetty_ygui_stepper *d, int n)
{
    if (n <= d->cap) {
        return 1;
    }
    int c = d->cap ? d->cap * 2 : 4;
    while (c < n) {
        c *= 2;
    }
    char **nl = realloc(d->labels, (size_t)c * sizeof(*nl));
    if (!nl) {
        return 0;
    }
    d->labels = nl;
    d->cap = c;
    return 1;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_stepper_add_step(struct yetty_yclass_object *obj,
                                                           const char *label)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "stepper_add_step: NULL");
    }
    struct yetty_ygui_stepper_ptr_result d_dr = yetty_ygui_stepper_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_stepper_add_step: data_get");
    struct yetty_ygui_stepper *d = d_dr.value;
    if (!grow(d, d->n + 1)) {
        return YETTY_ERR(yetty_ycore_void, "stepper_add_step: realloc");
    }
    if (label) {
        size_t n = strlen(label);
        d->labels[d->n] = malloc(n + 1);
        if (!d->labels[d->n]) {
            return YETTY_ERR(yetty_ycore_void, "stepper_add_step: malloc");
        }
        memcpy(d->labels[d->n], label, n + 1);
    } else {
        d->labels[d->n] = NULL;
    }
    d->n++;
    return yetty_ygui_widget_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_stepper_set_current(struct yetty_yclass_object *obj,
                                                              int i)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "stepper_set_current: NULL");
    }
    struct yetty_ygui_stepper_ptr_result d_dr = yetty_ygui_stepper_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_stepper_set_current: data_get");
    struct yetty_ygui_stepper *d = d_dr.value;
    d->current = i;
    return yetty_ygui_widget_set_dirty(obj);
}

#include "stepper.gen.c"
