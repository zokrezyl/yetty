/* GENERATED — do not edit. */
#include "yetty/gen/impl/ygui/primitive-widget.h"
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

struct yetty_ycore_void_result;
struct yetty_ygui_emit_ctx;
struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_widget_paint(struct yetty_yclass_object *obj,
                                                       struct yetty_ygui_emit_ctx *emit_ctx);
typedef struct yetty_ycore_void_result (*yetty_ygui_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui_destructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_paint_fn)(struct yetty_yclass_object *,
                                                                     struct yetty_ygui_emit_ctx *);

YETTY_MAYBE_UNUSED
static yetty_ygui_constructor_fn yetty_ygui_breadcrumbs_yetty_ygui_constructor_ctor_check = ctor;
YETTY_MAYBE_UNUSED
static yetty_ygui_destructor_fn yetty_ygui_breadcrumbs_yetty_ygui_destructor_dtor_check = dtor;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_paint_fn yetty_ygui_breadcrumbs_yetty_ygui_widget_paint_paint_check =
    paint;

struct yetty_yclass_ptr_result yetty_ygui_breadcrumbs_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ygui_breadcrumbs");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_breadcrumbs",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ygui_breadcrumbs),
        .data_align = _Alignof(struct yetty_ygui_breadcrumbs),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor,
         (yetty_yclass_impl_t)ctor},
        {"yetty_ygui", "destructor", (yetty_yclass_method_id_t)yetty_ygui_destructor,
         (yetty_yclass_impl_t)dtor},
        {"yetty_ygui", "widget_paint", (yetty_yclass_method_id_t)yetty_ygui_widget_paint,
         (yetty_yclass_impl_t)paint},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ygui_primitive_widget_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ygui_breadcrumbs_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ygui_breadcrumbs_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ygui_breadcrumbs_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ygui_breadcrumbs_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ygui_breadcrumbs_ptr_result yetty_ygui_breadcrumbs_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ygui_breadcrumbs_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ygui_breadcrumbs_ptr, "yetty_ygui_breadcrumbs_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ygui_breadcrumbs_ptr, "yetty_ygui_breadcrumbs_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ygui_breadcrumbs_ptr, (struct yetty_ygui_breadcrumbs *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ygui_breadcrumbs_to(struct yetty_ygui_breadcrumbs *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ygui_breadcrumbs_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ygui_breadcrumbs_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_ygui_breadcrumbs_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_breadcrumbs_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ygui_breadcrumbs_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_breadcrumbs");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ygui_breadcrumbs_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ygui_breadcrumbs_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_breadcrumbs_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    struct yetty_ycore_void_result ctor_r = yetty_ygui_constructor(alloc_r.value);
    if (YETTY_IS_ERR(ctor_r)) {
        struct yetty_ycore_void_result free_r = yetty_yclass_object_free(alloc_r.value);
        if (YETTY_IS_ERR(free_r)) {
            yetty_ycore_error_destroy(free_r.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_breadcrumbs_create: constructor failed", ctor_r);
    }
    return alloc_r;
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_ygui_breadcrumbs_class_get(void);
struct yetty_ycore_void_result yetty_ygui_breadcrumbs_register(void);

/* ---- ygui_breadcrumbs: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ygui_breadcrumbs_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ygui_breadcrumbs") == 0) {
        return yetty_ygui_breadcrumbs_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ygui_breadcrumbs: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ygui_breadcrumbs_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ygui_breadcrumbs_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ygui_breadcrumbs_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_ycore_void_result yetty_ygui_breadcrumbs_register(void);
struct yetty_ycore_void_result yetty_ygui_button_register(void);
struct yetty_ycore_void_result yetty_ygui_checkbox_register(void);
struct yetty_ycore_void_result yetty_ygui_chip_register(void);
struct yetty_ycore_void_result yetty_ygui_choicebox_register(void);
struct yetty_ycore_void_result yetty_ygui_clickable_register(void);
struct yetty_ycore_void_result yetty_ygui_collapsing_header_register(void);
struct yetty_ycore_void_result yetty_ygui_colorpicker_register(void);
struct yetty_ycore_void_result yetty_ygui_combobox_register(void);
struct yetty_ycore_void_result yetty_ygui_datepicker_register(void);
struct yetty_ycore_void_result yetty_ygui_dialog_register(void);
struct yetty_ycore_void_result yetty_ygui_draggable_register(void);
struct yetty_ycore_void_result yetty_ygui_dropdown_register(void);
struct yetty_ycore_void_result yetty_ygui_filepicker_register(void);
struct yetty_ycore_void_result yetty_ygui_framework_register(void);
struct yetty_ycore_void_result yetty_ygui_hbox_register(void);
struct yetty_ycore_void_result yetty_ygui_label_register(void);
struct yetty_ycore_void_result yetty_ygui_list_register(void);
struct yetty_ycore_void_result yetty_ygui_menubar_register(void);
struct yetty_ycore_void_result yetty_ygui_panel_register(void);
struct yetty_ycore_void_result yetty_ygui_popup_menu_register(void);
struct yetty_ycore_void_result yetty_ygui_primitive_widget_register(void);
struct yetty_ycore_void_result yetty_ygui_progress_register(void);
struct yetty_ycore_void_result yetty_ygui_radio_register(void);
struct yetty_ycore_void_result yetty_ygui_rich_register(void);
struct yetty_ycore_void_result yetty_ygui_scrollarea_register(void);
struct yetty_ycore_void_result yetty_ygui_selectable_register(void);
struct yetty_ycore_void_result yetty_ygui_separator_register(void);
struct yetty_ycore_void_result yetty_ygui_slider_register(void);
struct yetty_ycore_void_result yetty_ygui_spinner_register(void);
struct yetty_ycore_void_result yetty_ygui_splitter_register(void);
struct yetty_ycore_void_result yetty_ygui_statusbar_register(void);
struct yetty_ycore_void_result yetty_ygui_stepper_register(void);
struct yetty_ycore_void_result yetty_ygui_tabbar_register(void);
struct yetty_ycore_void_result yetty_ygui_table_register(void);
struct yetty_ycore_void_result yetty_ygui_textarea_register(void);
struct yetty_ycore_void_result yetty_ygui_textinput_register(void);
struct yetty_ycore_void_result yetty_ygui_toggle_register(void);
struct yetty_ycore_void_result yetty_ygui_tooltip_register(void);
struct yetty_ycore_void_result yetty_ygui_tree_node_register(void);
struct yetty_ycore_void_result yetty_ygui_vbox_register(void);
struct yetty_ycore_void_result yetty_ygui_widget_register(void);
struct yetty_ycore_void_result yetty_ygui_window_register(void);
struct yetty_ycore_void_result yetty_ygui_ybrowser_register(void);
struct yetty_ycore_void_result yetty_ygui_ydiagram_register(void);
struct yetty_ycore_void_result yetty_ygui_ydraw_embed_register(void);
struct yetty_ycore_void_result yetty_ygui_yimage_register(void);
struct yetty_ycore_void_result yetty_ygui_yjungle_register(void);
struct yetty_ycore_void_result yetty_ygui_ymarkdown_register(void);
struct yetty_ycore_void_result yetty_ygui_ymaze_register(void);
struct yetty_ycore_void_result yetty_ygui_ynode_register(void);
struct yetty_ycore_void_result yetty_ygui_ynodes_register(void);
struct yetty_ycore_void_result yetty_ygui_ypdf_register(void);
struct yetty_ycore_void_result yetty_ygui_yplot_register(void);
struct yetty_ycore_void_result yetty_ygui_yrich_view_register(void);
struct yetty_ycore_void_result yetty_ygui_yshadertoy_register(void);
struct yetty_ycore_void_result yetty_ygui_yvideo_register(void);
struct yetty_ycore_void_result yetty_ygui_yzoo_register(void);
struct yetty_ycore_void_result yetty_ygui_register(void);

/* ---- ygui: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ygui_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_breadcrumbs_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_breadcrumbs");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_button_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_button");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_checkbox_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_checkbox");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_chip_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_chip");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_choicebox_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_choicebox");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_clickable_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_clickable");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_collapsing_header_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_collapsing_header");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_colorpicker_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_colorpicker");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_combobox_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_combobox");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_datepicker_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_datepicker");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_dialog_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_dialog");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_draggable_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_draggable");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_dropdown_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_dropdown");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_filepicker_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_filepicker");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_framework_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_framework");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_hbox_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_hbox");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_label_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_label");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_list_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_list");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_menubar_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_menubar");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_panel_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_panel");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_popup_menu_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_popup_menu");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_primitive_widget_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_primitive_widget");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_progress_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_progress");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_radio_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_radio");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_rich_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_rich");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_scrollarea_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_scrollarea");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_selectable_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_selectable");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_separator_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_separator");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_slider_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_slider");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_spinner_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_spinner");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_splitter_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_splitter");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_statusbar_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_statusbar");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_stepper_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_stepper");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_tabbar_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_tabbar");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_table_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_table");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_textarea_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_textarea");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_textinput_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_textinput");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_toggle_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_toggle");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_tooltip_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_tooltip");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_tree_node_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_tree_node");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_vbox_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_vbox");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_widget_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_widget");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_window_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_window");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_ybrowser_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_ybrowser");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_ydiagram_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_ydiagram");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_ydraw_embed_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_ydraw_embed");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_yimage_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_yimage");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_yjungle_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_yjungle");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_ymarkdown_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_ymarkdown");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_ymaze_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_ymaze");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_ynode_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_ynode");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_ynodes_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_ynodes");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_ypdf_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_ypdf");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_yplot_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_yplot");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_yrich_view_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_yrich_view");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_yshadertoy_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ygui_register: submodule ygui_yshadertoy");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_yvideo_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_yvideo");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ygui_yzoo_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r, "yetty_ygui_register: submodule ygui_yzoo");
    }
    registered = true;
    return YETTY_OK_VOID();
}
