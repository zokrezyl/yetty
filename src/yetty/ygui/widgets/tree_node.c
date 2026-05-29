/* ygui-tree_node.c — collapsible nested node (same pattern as
 * collapsing_header but with smaller header strip + indent). */
#include "paint-helpers.h"
#include <yetty/ygui/mixins/clickable.h>
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
    yetty_ygui_click_cb on_toggle;
    void *on_toggle_ud;
};

[[clang::annotate("override@ygui:tree_node:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj,
        yetty_ygui_class_expect(yetty_ygui_tree_node_class_get(), "yetty_ygui_tree_node_class_get"),
        (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "tree_node: super");
    struct tn_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_tree_node_class_get(),
                                                         "yetty_ygui_tree_node_class_get"));
    d->label = NULL;
    d->open = 1;
    d->on_toggle = NULL;
    d->on_toggle_ud = NULL;
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.padding_top = HEADER_H;
    l.padding_left = INDENT;
    l.gap = 2.0f;
    return yetty_ygui_widget_layout_set(obj, &l);
}

[[clang::annotate("override@ygui:tree_node:destructor")]]
static struct yetty_ycore_void_result dtor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct tn_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_tree_node_class_get(),
                                                         "yetty_ygui_tree_node_class_get"));
    free(d->label);
    return yetty_ygui_super_void(
        obj,
        yetty_ygui_class_expect(yetty_ygui_tree_node_class_get(), "yetty_ygui_tree_node_class_get"),
        (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:tree_node:widget_on_press")]]
static struct yetty_ycore_int_result on_press(struct yetty_yclass_ctx *yclass_ctx,
                                              struct yetty_yclass_object *yclass_obj, float x,
                                              float y, int btn)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    (void)x;
    (void)btn;
    struct tn_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_tree_node_class_get(),
                                                         "yetty_ygui_tree_node_class_get"));
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
    if (d->on_toggle) {
        struct yetty_ycore_void_result cr = d->on_toggle(NULL, yclass_obj, d->on_toggle_ud);
        if (YETTY_IS_ERR(cr)) {
            return YETTY_ERR(yetty_ycore_int, "tree_node: on_toggle", cr);
        }
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:tree_node:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "tree_node paint: NULL ctx");
    }
    struct tn_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_tree_node_class_get(),
                                                         "yetty_ygui_tree_node_class_get"));
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
    struct tn_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_tree_node_class_get(),
                                                         "yetty_ygui_tree_node_class_get"));
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
    struct tn_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_tree_node_class_get(),
                                                         "yetty_ygui_tree_node_class_get"));
    d->open = o ? 1 : 0;
    return yetty_ygui_object_set_dirty(obj);
}

int yetty_ygui_tree_node_is_open(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return 0;
    }
    return ((struct tn_data *)yetty_ygui_data_get(
                (struct yetty_ygui_object *)obj,
                yetty_ygui_class_expect(yetty_ygui_tree_node_class_get(),
                                        "yetty_ygui_tree_node_class_get")))
        ->open;
}

struct yetty_ycore_void_result yetty_ygui_tree_node_on_toggle(struct yetty_ygui_object *obj,
                                                              yetty_ygui_click_cb cb, void *userdata)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "tn_on_toggle: NULL");
    }
    struct tn_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_tree_node_class_get(),
                                                         "yetty_ygui_tree_node_class_get"));
    d->on_toggle = cb;
    d->on_toggle_ud = userdata;
    return YETTY_OK_VOID();
}

#include "tree_node.gen.c"
