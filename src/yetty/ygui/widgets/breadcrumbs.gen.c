/* GENERATED — do not edit. */
#include "yetty/ygui/primitive-widget.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h> /* NULL, size_t */
/* The folded-in public stubs, rpc skeletons + create() and the
 * registration hooks (formerly methods.gen.c / rpc.gen.c) need
 * these. All header-guarded, so re-including what the hand-written
 * .c already pulled in is harmless; the class's OWN header is
 * still never included (that would redefine its expose'd types). */
#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> /* malloc/free for buffer-arg marshalling */
#include <string.h>

struct yetty_ycore_void_result;
struct yetty_ygui_emit_ctx;
struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_widget_paint(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       struct yetty_ygui_emit_ctx *emit_ctx);
typedef struct yetty_ycore_void_result (*yetty_ygui_constructor_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui_destructor_fn)(struct yetty_yclass_ctx *,
                                                                   struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_paint_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *,
                                                                     struct yetty_ygui_emit_ctx *);

/* ===== class accessors ===== */

[[maybe_unused]]
static yetty_ygui_constructor_fn yetty_ygui_breadcrumbs_yetty_ygui_constructor_check = ctor;
[[maybe_unused]]
static yetty_ygui_destructor_fn yetty_ygui_breadcrumbs_yetty_ygui_destructor_check = dtor;
[[maybe_unused]]
static yetty_ygui_widget_paint_fn yetty_ygui_breadcrumbs_yetty_ygui_widget_paint_check = paint;

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

struct yetty_yclass_object *yetty_ygui_breadcrumbs_to(struct yetty_ygui_breadcrumbs *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_ygui_breadcrumbs_class_get();
    if (YETTY_IS_ERR(class_r)) {
        yetty_ycore_error_destroy(class_r.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    if (YETTY_IS_ERR(offset_r)) {
        yetty_ycore_error_destroy(offset_r.error);
        return NULL;
    }
    return (struct yetty_yclass_object *)((char *)data - offset_r.value);
}

/* ===== rpc skeletons + create (was rpc.gen.c) ===== */

struct yetty_yclass_object_ptr_result yetty_ygui_breadcrumbs_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_breadcrumbs");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ygui_breadcrumbs_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_breadcrumbs_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) {
            return alloc_r;
        }
        struct yetty_ycore_void_result ctor_r = yetty_ygui_constructor(ctx, alloc_r.value);
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

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_breadcrumbs");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(
                stderr,
                "yetty_ygui_breadcrumbs_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_ygui_breadcrumbs";
    struct yetty_ycore_size_result create_call_r =
        yetty_yclass_rpc_call(ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name,
                              strlen(class_name), &handle, sizeof(handle));
    if (YETTY_IS_ERR(create_call_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_breadcrumbs_create: CREATE call failed", create_call_r);
    }
    if (create_call_r.value != sizeof(handle) || !handle) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_breadcrumbs_create: CREATE returned no/invalid handle");
    }

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_breadcrumbs_create: calloc(proxy) failed");
    }
    proxy->header.klass = klass;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

/* ---- ygui/breadcrumbs: class name -> accessor ---------------------- */
static struct yetty_yclass_ptr_result yetty_ygui_breadcrumbs_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ygui_breadcrumbs") == 0) {
        return yetty_ygui_breadcrumbs_class_get();
    }
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

struct yetty_ycore_void_result yetty_ygui_breadcrumbs_register_hooks(void)
{
    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ygui_breadcrumbs_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ygui_breadcrumbs_register_hooks: accessor");
    return YETTY_OK_VOID();
}

/* ===== module registration (was rpc.gen.c) ========================== */
struct yetty_ycore_void_result yetty_ygui_breadcrumbs_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_button_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_checkbox_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_chip_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_choicebox_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_clickable_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_collapsing_header_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_colorpicker_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_combobox_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_datepicker_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_dialog_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_draggable_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_dropdown_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_filepicker_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_hbox_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_label_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_list_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_menubar_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_panel_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_popup_menu_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_primitive_widget_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_progress_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_radio_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_rich_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_scrollarea_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_selectable_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_separator_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_slider_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_spinner_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_splitter_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_statusbar_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_stepper_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_tabbar_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_table_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_textarea_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_textinput_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_toggle_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_tooltip_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_tree_node_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_vbox_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_widget_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_window_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_ybrowser_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_ydiagram_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_ydraw_embed_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_yimage_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_yjungle_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_ymarkdown_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_ymaze_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_ynode_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_ynodes_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_ypdf_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_yplot_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_yrich_view_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_yshadertoy_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_yvideo_register_hooks(void);
struct yetty_ycore_void_result yetty_ygui_yzoo_register_hooks(void);

struct yetty_ycore_void_result yetty_ygui_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_breadcrumbs_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: breadcrumbs");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_button_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: button");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_checkbox_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: checkbox");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_chip_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: chip");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_choicebox_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: choicebox");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_clickable_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: clickable");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_collapsing_header_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: collapsing_header");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_colorpicker_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: colorpicker");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_combobox_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: combobox");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_datepicker_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: datepicker");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_dialog_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: dialog");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_draggable_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: draggable");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_dropdown_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: dropdown");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_filepicker_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: filepicker");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_hbox_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: hbox");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_label_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: label");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_list_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: list");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_menubar_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: menubar");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_panel_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: panel");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_popup_menu_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: popup_menu");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_primitive_widget_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: primitive_widget");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_progress_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: progress");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_radio_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: radio");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_rich_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: rich");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_scrollarea_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: scrollarea");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_selectable_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: selectable");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_separator_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: separator");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_slider_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: slider");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_spinner_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: spinner");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_splitter_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: splitter");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_statusbar_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: statusbar");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_stepper_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: stepper");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_tabbar_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: tabbar");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_table_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: table");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_textarea_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: textarea");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_textinput_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: textinput");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_toggle_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: toggle");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_tooltip_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: tooltip");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_tree_node_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: tree_node");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_vbox_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: vbox");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_widget_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: widget");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_window_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: window");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_ybrowser_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: ybrowser");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_ydiagram_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: ydiagram");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_ydraw_embed_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: ydraw_embed");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_yimage_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: yimage");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_yjungle_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: yjungle");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_ymarkdown_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: ymarkdown");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_ymaze_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: ymaze");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_ynode_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: ynode");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_ynodes_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: ynodes");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_ypdf_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: ypdf");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_yplot_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: yplot");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_yrich_view_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: yrich_view");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_yshadertoy_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: yshadertoy");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_yvideo_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: yvideo");
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_ygui_yzoo_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_ygui_register: yzoo");
    }
    registered = true;
    return YETTY_OK_VOID();
}
