/* ygui-tree_node.c — collapsible nested node (same pattern as
 * collapsing_header but with smaller header strip + indent). */
#include "paint-helpers.h"
#include <yetty/ygui/widgets/tree_node.h>
#include <yetty/ygui/widgets/vbox.h>
#include <stdlib.h>

#define HEADER_H 22.0f
#define INDENT 16.0f
#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_CHEV 0xFFA8A79Fu

struct [[clang::annotate("class@ygui:tree_node")]] [[clang::annotate("parent@ygui:vbox")]] tn_data {
    char *label;
    int open;
};

[[clang::annotate("override@ygui:tree_node:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *_yc_ctx,
                                           struct yetty_yclass_object *_yc_obj)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_tree_node_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "tree_node: super");
    struct tn_data *d = yetty_ygui_data_get(obj, yetty_ygui_tree_node_class_get().value);
    d->label = NULL;
    d->open = 1;
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.padding_top = HEADER_H;
    l.padding_left = INDENT;
    l.gap = 2.0f;
    return yetty_ygui_widget_layout_set(obj, &l);
}

[[clang::annotate("override@ygui:tree_node:destructor")]]
static struct yetty_ycore_void_result dtor(struct yetty_yclass_ctx *_yc_ctx,
                                           struct yetty_yclass_object *_yc_obj)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    struct tn_data *d = yetty_ygui_data_get(obj, yetty_ygui_tree_node_class_get().value);
    free(d->label);
    return yetty_ygui_super_void(obj, yetty_ygui_tree_node_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:tree_node:widget_on_press")]]
static struct yetty_ycore_int_result on_press(struct yetty_yclass_ctx *_yc_ctx,
                                              struct yetty_yclass_object *_yc_obj, float x, float y,
                                              int btn)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    (void)x;
    (void)btn;
    struct tn_data *d = yetty_ygui_data_get(obj, yetty_ygui_tree_node_class_get().value);
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    if (y - r.min.y > HEADER_H) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    d->open = !d->open;
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    if (!d->open) {
        l.height = HEADER_H;
    } else {
        l.height = -1.0f;
    }
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &l);
    if (YETTY_IS_ERR(lr)) {
        return YETTY_ERR(yetty_ycore_int, "tree_node: layout", lr);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:tree_node:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *_yc_ctx,
                                            struct yetty_yclass_object *_yc_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "tree_node paint: NULL ctx");
    }
    struct tn_data *d = yetty_ygui_data_get(obj, yetty_ygui_tree_node_class_get().value);
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float fs = 13.0f;
    float ty = r.min.y + (HEADER_H + fs) * 0.5f - 3;
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yguix_text(ctx, d->open ? "v" : ">", r.min.x + 4, ty, fs, COLOR_CHEV),
                        "tree_node: chev");
    if (d->label) {
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_text(ctx, d->label, r.min.x + 20, ty, fs, COLOR_TEXT),
                            "tree_node: label");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_tree_node_set_label(struct yetty_ygui_object *obj,
                                                              const char *label)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "tn_set_label: NULL");
    }
    struct tn_data *d = yetty_ygui_data_get(obj, yetty_ygui_tree_node_class_get().value);
    free(d->label);
    d->label = NULL;
    if (label) {
        size_t n = strlen(label);
        d->label = malloc(n + 1);
        if (!d->label) {
            return YETTY_ERR(yetty_ycore_void, "tn_set_label: malloc");
        }
        memcpy(d->label, label, n + 1);
    }
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_tree_node_set_open(struct yetty_ygui_object *obj, int o)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "tn_set_open: NULL");
    }
    struct tn_data *d = yetty_ygui_data_get(obj, yetty_ygui_tree_node_class_get().value);
    d->open = o ? 1 : 0;
    return yetty_ygui_object_set_dirty(obj);
}

int yetty_ygui_tree_node_is_open(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return 0;
    }
    return ((struct tn_data *)yetty_ygui_data_get((struct yetty_ygui_object *)obj,
                                                  yetty_ygui_tree_node_class_get().value))
        ->open;
}

#include "tree_node.gen.c"
