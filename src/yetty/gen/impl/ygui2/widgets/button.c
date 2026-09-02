/* GENERATED — do not edit. */
#include "yetty/gen/impl/ygui2/widget.h"
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h> /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h> /* calloc/free for proxy + buffer marshalling */
#include <string.h> /* memcpy/strcmp/strlen */

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;
struct yetty_ycore_void_result yetty_ygui2_widget_paint(struct yetty_yclass_object *obj,
                                                        struct yetty_ydraw_drawable_list *list);
struct yetty_ycore_int_result yetty_ygui2_widget_on_press(struct yetty_yclass_object *obj,
                                                          float local_x, float local_y, int button,
                                                          int mods);
struct yetty_ycore_int_result yetty_ygui2_widget_on_release(struct yetty_yclass_object *obj,
                                                            float local_x, float local_y,
                                                            int button, int mods);
typedef struct yetty_ycore_void_result (*yetty_ygui2_widget_paint_fn)(
    struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *);
typedef struct yetty_ycore_int_result (*yetty_ygui2_widget_on_press_fn)(
    struct yetty_yclass_object *, float, float, int, int);
typedef struct yetty_ycore_int_result (*yetty_ygui2_widget_on_release_fn)(
    struct yetty_yclass_object *, float, float, int, int);

YETTY_MAYBE_UNUSED
static yetty_ygui2_widget_paint_fn yetty_ygui2_button_yetty_ygui2_widget_paint_button_paint_check =
    button_paint;
YETTY_MAYBE_UNUSED
static yetty_ygui2_widget_on_press_fn
    yetty_ygui2_button_yetty_ygui2_widget_on_press_button_on_press_check = button_on_press;
YETTY_MAYBE_UNUSED
static yetty_ygui2_widget_on_release_fn
    yetty_ygui2_button_yetty_ygui2_widget_on_release_button_on_release_check = button_on_release;

struct yetty_yclass_ptr_result yetty_ygui2_button_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ygui2_button");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui2_button",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ygui2_button),
        .data_align = _Alignof(struct yetty_ygui2_button),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui2", "widget_paint", (yetty_yclass_method_id_t)yetty_ygui2_widget_paint,
         (yetty_yclass_impl_t)button_paint},
        {"yetty_ygui2", "widget_on_press", (yetty_yclass_method_id_t)yetty_ygui2_widget_on_press,
         (yetty_yclass_impl_t)button_on_press},
        {"yetty_ygui2", "widget_on_release",
         (yetty_yclass_method_id_t)yetty_ygui2_widget_on_release,
         (yetty_yclass_impl_t)button_on_release},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ygui2_widget_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ygui2_button_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui2_button_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ygui2_button_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui2_button_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ygui2_button_ptr_result yetty_ygui2_button_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ygui2_button_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ygui2_button_ptr, "yetty_ygui2_button_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ygui2_button_ptr, "yetty_ygui2_button_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ygui2_button_ptr, (struct yetty_ygui2_button *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ygui2_button_to(struct yetty_ygui2_button *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ygui2_button_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ygui2_button_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ygui2_button_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_ygui2_button_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ygui2_button_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui2_button");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ygui2_button_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ygui2_button_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui2_button_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_ygui2_button_class_get(void);
struct yetty_ycore_void_result yetty_ygui2_button_register(void);

/* ---- ygui2_button: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ygui2_button_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ygui2_button") == 0) {
        return yetty_ygui2_button_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ygui2_button: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ygui2_button_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ygui2_button_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ygui2_button_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_ycore_void_result yetty_ygui2_button_register(void);
struct yetty_ycore_void_result yetty_ygui2_checkbox_register(void);
struct yetty_ycore_void_result yetty_ygui2_chip_register(void);
struct yetty_ycore_void_result yetty_ygui2_complex_host_register(void);
struct yetty_ycore_void_result yetty_ygui2_dialog_register(void);
struct yetty_ycore_void_result yetty_ygui2_dropdown_register(void);
struct yetty_ycore_void_result yetty_ygui2_framework_register(void);
struct yetty_ycore_void_result yetty_ygui2_label_register(void);
struct yetty_ycore_void_result yetty_ygui2_panel_register(void);
struct yetty_ycore_void_result yetty_ygui2_plot_register(void);
struct yetty_ycore_void_result yetty_ygui2_popup_menu_register(void);
struct yetty_ycore_void_result yetty_ygui2_progress_register(void);
struct yetty_ycore_void_result yetty_ygui2_radio_register(void);
struct yetty_ycore_void_result yetty_ygui2_scrollarea_register(void);
struct yetty_ycore_void_result yetty_ygui2_separator_register(void);
struct yetty_ycore_void_result yetty_ygui2_slider_register(void);
struct yetty_ycore_void_result yetty_ygui2_spinner_register(void);
struct yetty_ycore_void_result yetty_ygui2_statusbar_register(void);
struct yetty_ycore_void_result yetty_ygui2_stepper_register(void);
struct yetty_ycore_void_result yetty_ygui2_table_register(void);
struct yetty_ycore_void_result yetty_ygui2_textinput_register(void);
struct yetty_ycore_void_result yetty_ygui2_toggle_register(void);
struct yetty_ycore_void_result yetty_ygui2_tooltip_register(void);
struct yetty_ycore_void_result yetty_ygui2_widget_register(void);
struct yetty_ycore_void_result yetty_ygui2_ydraw_embed_register(void);
struct yetty_ycore_void_result yetty_ygui2_register(void);

/* ---- ygui2: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ygui2_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_button_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_button");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_checkbox_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_checkbox");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_chip_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui2_register: submodule ygui2_chip");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_complex_host_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_complex_host");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_dialog_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_dialog");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_dropdown_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_dropdown");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_framework_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_framework");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_label_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui2_register: submodule ygui2_label");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_panel_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui2_register: submodule ygui2_panel");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_plot_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui2_register: submodule ygui2_plot");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_popup_menu_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_popup_menu");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_progress_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_progress");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_radio_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui2_register: submodule ygui2_radio");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_scrollarea_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_scrollarea");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_separator_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_separator");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_slider_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_slider");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_spinner_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_spinner");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_statusbar_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_statusbar");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_stepper_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_stepper");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_table_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui2_register: submodule ygui2_table");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_textinput_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_textinput");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_toggle_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_toggle");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_tooltip_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_tooltip");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_widget_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_widget");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui2_ydraw_embed_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui2_register: submodule ygui2_ydraw_embed");
    }
    registered = true;
    return YETTY_OK_VOID();
}
