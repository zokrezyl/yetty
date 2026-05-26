/* ygui-collapsing_header.c — togglable vbox section. */
#include "paint-helpers.h"
#include <yetty/ygui/widgets/collapsing_header.h>
#include <yetty/ygui/widgets/vbox.h>
#include <stdlib.h>

#define HEADER_H 28.0f
#define COLOR_BG 0xFF1F1A14u
#define COLOR_TITLE 0xFFE4E5E0u
#define COLOR_CHEVRON 0xFFA8A79Fu

struct ch_data {
    char *title;
    int open;
};

static struct yetty_ycore_void_result ctor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_collapsing_header_class_get(),
        (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "collapsing_header: super");
    struct ch_data *d = yetty_ygui_data_get(obj, yetty_ygui_collapsing_header_class_get());
    d->title = NULL;
    d->open = 1;
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.padding_top = HEADER_H + 4.0f;
    l.padding_bottom = 4.0f;
    l.padding_left = 8.0f;
    l.padding_right = 8.0f;
    l.gap = 4.0f;
    return yetty_ygui_widget_layout_set(obj, &l);
}

static struct yetty_ycore_void_result dtor(struct yetty_ygui_object *obj)
{
    struct ch_data *d = yetty_ygui_data_get(obj, yetty_ygui_collapsing_header_class_get());
    free(d->title);
    return yetty_ygui_super_void(obj, yetty_ygui_collapsing_header_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_int_result on_press(struct yetty_ygui_object *obj, float x, float y,
                                              int btn)
{
    (void)x;
    (void)btn;
    struct ch_data *d = yetty_ygui_data_get(obj, yetty_ygui_collapsing_header_class_get());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    /* Only the header strip toggles. */
    if (y - r.min.y > HEADER_H) return YETTY_OK(yetty_ycore_int, 0);
    d->open = !d->open;
    /* Hide children by toggling their layout height to 0 via a flag
     * on the parent — simplest: layout padding_top stays HEADER_H, but
     * we collapse the parent's height when closed by setting it to
     * just HEADER_H. */
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    if (!d->open) {
        l.padding_top = HEADER_H;
        l.padding_bottom = 0;
        l.height = HEADER_H;
    } else {
        l.padding_top = HEADER_H + 4.0f;
        l.padding_bottom = 4.0f;
        l.height = -1.0f;
    }
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &l);
    if (YETTY_IS_ERR(lr)) return YETTY_ERR(yetty_ycore_int, "collapsing_header: layout", lr);
    return YETTY_OK(yetty_ycore_int, 1);
}

static struct yetty_ycore_void_result paint(struct yetty_ygui_object *obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    if (!ctx) return YETTY_ERR(yetty_ycore_void, "collapsing_header paint: NULL ctx");
    struct ch_data *d = yetty_ygui_data_get(obj, yetty_ygui_collapsing_header_class_get());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    if (w <= 0) return YETTY_OK_VOID();
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yguix_box(ctx, r.min.x, r.min.y, w, HEADER_H, COLOR_BG, 4),
                        "ch: header_bg");
    float fs = 14.0f;
    float ty = r.min.y + (HEADER_H + fs) * 0.5f - 3.0f;
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yguix_text(ctx, d->open ? "v" : ">", r.min.x + 8, ty, fs, COLOR_CHEVRON),
                        "ch: chev");
    if (d->title)
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_text(ctx, d->title, r.min.x + 26, ty, fs, COLOR_TITLE),
                            "ch: title");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_collapsing_header_set_title(struct yetty_ygui_object *obj,
                                                                      const char *title)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "ch_set_title: NULL");
    struct ch_data *d = yetty_ygui_data_get(obj, yetty_ygui_collapsing_header_class_get());
    free(d->title);
    d->title = NULL;
    if (title) {
        size_t n = strlen(title);
        d->title = malloc(n + 1);
        if (!d->title) return YETTY_ERR(yetty_ycore_void, "ch_set_title: malloc");
        memcpy(d->title, title, n + 1);
    }
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_collapsing_header_set_open(struct yetty_ygui_object *obj,
                                                                     int open)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "ch_set_open: NULL");
    struct ch_data *d = yetty_ygui_data_get(obj, yetty_ygui_collapsing_header_class_get());
    d->open = open ? 1 : 0;
    return yetty_ygui_object_set_dirty(obj);
}

int yetty_ygui_collapsing_header_is_open(const struct yetty_ygui_object *obj)
{
    if (!obj) return 0;
    return ((struct ch_data *)yetty_ygui_data_get((struct yetty_ygui_object *)obj,
                                                  yetty_ygui_collapsing_header_class_get()))
        ->open;
}


static const struct yetty_ygui_op collapsing_header_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, ctor),
    YETTY_YGUI_OP(yetty_ygui_destructor, dtor),
    YETTY_YGUI_OP(yetty_ygui_widget_paint, paint),
    YETTY_YGUI_OP(yetty_ygui_widget_on_press, on_press),
};

static const struct yetty_ygui_class_descriptor collapsing_header_desc = {
    .name = "yetty_ygui_collapsing_header",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct ch_data),
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_collapsing_header_class_get, &collapsing_header_desc, collapsing_header_ops, yetty_ygui_vbox_class_get(), NULL)
