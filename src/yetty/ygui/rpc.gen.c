/* GENERATED — do not edit. */
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yclass/class.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Forward decls — these accessors and skels are defined in each
 * class's own <stem>.gen.c; the lookup tables below name them
 * across translation units. */
struct yetty_yclass_ptr_result yetty_ygui_clickable_mixin_get(void);
struct yetty_yclass_ptr_result yetty_ygui_draggable_mixin_get(void);
struct yetty_yclass_ptr_result yetty_ygui_primitive_widget_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_widget_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_breadcrumbs_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_button_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_checkbox_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_chip_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_choicebox_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_collapsing_header_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_colorpicker_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_combobox_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_datepicker_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_dialog_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_dropdown_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_filepicker_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_hbox_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_label_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_list_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_menubar_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_panel_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_popup_menu_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_progress_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_radio_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_rich_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_scrollarea_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_selectable_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_separator_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_slider_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_spinner_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_splitter_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_statusbar_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_stepper_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_tabbar_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_table_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_textarea_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_textinput_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_toggle_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_tooltip_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_tree_node_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_vbox_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_window_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_ybrowser_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_ydiagram_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_ydraw_embed_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_yimage_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_yjungle_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_ymarkdown_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_ymaze_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_ynode_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_ynodes_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_ypdf_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_yplot_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_yrich_view_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_yshadertoy_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_yvideo_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui_yzoo_class_get(void);
size_t yetty_ygui_widget_on_press_skel(const void *, size_t, void *, size_t);
size_t yetty_ygui_widget_on_release_skel(const void *, size_t, void *, size_t);
size_t yetty_ygui_widget_on_motion_skel(const void *, size_t, void *, size_t);
size_t yetty_ygui_constructor_skel(const void *, size_t, void *, size_t);
size_t yetty_ygui_destructor_skel(const void *, size_t, void *, size_t);
size_t yetty_ygui_widget_on_scroll_skel(const void *, size_t, void *, size_t);
struct yetty_ycore_void_result yetty_ygui_register(void);

/* ---- ygui: class name → accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ygui_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ygui_clickable") == 0) {
        return yetty_ygui_clickable_mixin_get();
    }
    if (strcmp(name, "yetty_ygui_draggable") == 0) {
        return yetty_ygui_draggable_mixin_get();
    }
    if (strcmp(name, "yetty_ygui_primitive_widget") == 0) {
        return yetty_ygui_primitive_widget_class_get();
    }
    if (strcmp(name, "yetty_ygui_widget") == 0) {
        return yetty_ygui_widget_class_get();
    }
    if (strcmp(name, "yetty_ygui_breadcrumbs") == 0) {
        return yetty_ygui_breadcrumbs_class_get();
    }
    if (strcmp(name, "yetty_ygui_button") == 0) {
        return yetty_ygui_button_class_get();
    }
    if (strcmp(name, "yetty_ygui_checkbox") == 0) {
        return yetty_ygui_checkbox_class_get();
    }
    if (strcmp(name, "yetty_ygui_chip") == 0) {
        return yetty_ygui_chip_class_get();
    }
    if (strcmp(name, "yetty_ygui_choicebox") == 0) {
        return yetty_ygui_choicebox_class_get();
    }
    if (strcmp(name, "yetty_ygui_collapsing_header") == 0) {
        return yetty_ygui_collapsing_header_class_get();
    }
    if (strcmp(name, "yetty_ygui_colorpicker") == 0) {
        return yetty_ygui_colorpicker_class_get();
    }
    if (strcmp(name, "yetty_ygui_combobox") == 0) {
        return yetty_ygui_combobox_class_get();
    }
    if (strcmp(name, "yetty_ygui_datepicker") == 0) {
        return yetty_ygui_datepicker_class_get();
    }
    if (strcmp(name, "yetty_ygui_dialog") == 0) {
        return yetty_ygui_dialog_class_get();
    }
    if (strcmp(name, "yetty_ygui_dropdown") == 0) {
        return yetty_ygui_dropdown_class_get();
    }
    if (strcmp(name, "yetty_ygui_filepicker") == 0) {
        return yetty_ygui_filepicker_class_get();
    }
    if (strcmp(name, "yetty_ygui_hbox") == 0) {
        return yetty_ygui_hbox_class_get();
    }
    if (strcmp(name, "yetty_ygui_label") == 0) {
        return yetty_ygui_label_class_get();
    }
    if (strcmp(name, "yetty_ygui_list") == 0) {
        return yetty_ygui_list_class_get();
    }
    if (strcmp(name, "yetty_ygui_menubar") == 0) {
        return yetty_ygui_menubar_class_get();
    }
    if (strcmp(name, "yetty_ygui_panel") == 0) {
        return yetty_ygui_panel_class_get();
    }
    if (strcmp(name, "yetty_ygui_popup_menu") == 0) {
        return yetty_ygui_popup_menu_class_get();
    }
    if (strcmp(name, "yetty_ygui_progress") == 0) {
        return yetty_ygui_progress_class_get();
    }
    if (strcmp(name, "yetty_ygui_radio") == 0) {
        return yetty_ygui_radio_class_get();
    }
    if (strcmp(name, "yetty_ygui_rich") == 0) {
        return yetty_ygui_rich_class_get();
    }
    if (strcmp(name, "yetty_ygui_scrollarea") == 0) {
        return yetty_ygui_scrollarea_class_get();
    }
    if (strcmp(name, "yetty_ygui_selectable") == 0) {
        return yetty_ygui_selectable_class_get();
    }
    if (strcmp(name, "yetty_ygui_separator") == 0) {
        return yetty_ygui_separator_class_get();
    }
    if (strcmp(name, "yetty_ygui_slider") == 0) {
        return yetty_ygui_slider_class_get();
    }
    if (strcmp(name, "yetty_ygui_spinner") == 0) {
        return yetty_ygui_spinner_class_get();
    }
    if (strcmp(name, "yetty_ygui_splitter") == 0) {
        return yetty_ygui_splitter_class_get();
    }
    if (strcmp(name, "yetty_ygui_statusbar") == 0) {
        return yetty_ygui_statusbar_class_get();
    }
    if (strcmp(name, "yetty_ygui_stepper") == 0) {
        return yetty_ygui_stepper_class_get();
    }
    if (strcmp(name, "yetty_ygui_tabbar") == 0) {
        return yetty_ygui_tabbar_class_get();
    }
    if (strcmp(name, "yetty_ygui_table") == 0) {
        return yetty_ygui_table_class_get();
    }
    if (strcmp(name, "yetty_ygui_textarea") == 0) {
        return yetty_ygui_textarea_class_get();
    }
    if (strcmp(name, "yetty_ygui_textinput") == 0) {
        return yetty_ygui_textinput_class_get();
    }
    if (strcmp(name, "yetty_ygui_toggle") == 0) {
        return yetty_ygui_toggle_class_get();
    }
    if (strcmp(name, "yetty_ygui_tooltip") == 0) {
        return yetty_ygui_tooltip_class_get();
    }
    if (strcmp(name, "yetty_ygui_tree_node") == 0) {
        return yetty_ygui_tree_node_class_get();
    }
    if (strcmp(name, "yetty_ygui_vbox") == 0) {
        return yetty_ygui_vbox_class_get();
    }
    if (strcmp(name, "yetty_ygui_window") == 0) {
        return yetty_ygui_window_class_get();
    }
    if (strcmp(name, "yetty_ygui_ybrowser") == 0) {
        return yetty_ygui_ybrowser_class_get();
    }
    if (strcmp(name, "yetty_ygui_ydiagram") == 0) {
        return yetty_ygui_ydiagram_class_get();
    }
    if (strcmp(name, "yetty_ygui_ydraw_embed") == 0) {
        return yetty_ygui_ydraw_embed_class_get();
    }
    if (strcmp(name, "yetty_ygui_yimage") == 0) {
        return yetty_ygui_yimage_class_get();
    }
    if (strcmp(name, "yetty_ygui_yjungle") == 0) {
        return yetty_ygui_yjungle_class_get();
    }
    if (strcmp(name, "yetty_ygui_ymarkdown") == 0) {
        return yetty_ygui_ymarkdown_class_get();
    }
    if (strcmp(name, "yetty_ygui_ymaze") == 0) {
        return yetty_ygui_ymaze_class_get();
    }
    if (strcmp(name, "yetty_ygui_ynode") == 0) {
        return yetty_ygui_ynode_class_get();
    }
    if (strcmp(name, "yetty_ygui_ynodes") == 0) {
        return yetty_ygui_ynodes_class_get();
    }
    if (strcmp(name, "yetty_ygui_ypdf") == 0) {
        return yetty_ygui_ypdf_class_get();
    }
    if (strcmp(name, "yetty_ygui_yplot") == 0) {
        return yetty_ygui_yplot_class_get();
    }
    if (strcmp(name, "yetty_ygui_yrich_view") == 0) {
        return yetty_ygui_yrich_view_class_get();
    }
    if (strcmp(name, "yetty_ygui_yshadertoy") == 0) {
        return yetty_ygui_yshadertoy_class_get();
    }
    if (strcmp(name, "yetty_ygui_yvideo") == 0) {
        return yetty_ygui_yvideo_class_get();
    }
    if (strcmp(name, "yetty_ygui_yzoo") == 0) {
        return yetty_ygui_yzoo_class_get();
    }
    /* "Not mine": OK with NULL value — yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ygui: slot → skel, name-keyed static data --------------- */

struct yetty_ygui_skel_row {
    const char *name;
    yetty_yclass_rpc_skel_fn fn;
};

static const struct yetty_ygui_skel_row yetty_ygui_skel_rows[] = {
    {"yetty_ygui_widget_on_press", yetty_ygui_widget_on_press_skel},
    {"yetty_ygui_widget_on_release", yetty_ygui_widget_on_release_skel},
    {"yetty_ygui_widget_on_motion", yetty_ygui_widget_on_motion_skel},
    {"yetty_ygui_constructor", yetty_ygui_constructor_skel},
    {"yetty_ygui_destructor", yetty_ygui_destructor_skel},
    {"yetty_ygui_widget_on_scroll", yetty_ygui_widget_on_scroll_skel}};

/* Signature is dictated by the skel-lookup hook contract (registered as a
 * fn-pointer via yetty_yclass_rpc_add_skel_lookup); a slot-name lookup
 * failure is absorbed into a NULL return at this boundary. */
YETTY_EXTERNAL_CALLBACK
static yetty_yclass_rpc_skel_fn yetty_ygui_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) {
        yetty_ycore_error_destroy(slot_name_r.error);
        return NULL;
    }
    const char *name = slot_name_r.value;
    for (size_t i = 0; i < sizeof(yetty_ygui_skel_rows) / sizeof(yetty_ygui_skel_rows[0]); ++i) {
        if (strcmp(yetty_ygui_skel_rows[i].name, name) == 0) {
            return yetty_ygui_skel_rows[i].fn;
        }
    }
    return NULL;
}

/* ---- ygui: explicit yclass-RPC hook registration ------------- */

/* Installs this module's server-side discovery hooks: the accessor
 * lookup feeds yetty_yclass_by_name()'s registry-miss path, and (when
 * the module exposes wire methods) the skel lookup feeds RPC skeleton
 * dispatch. Call once when the yclass RPC / remote-object server is
 * brought up — idempotent, so repeated calls (several hosts, re-init)
 * are no-ops. This replaces the former load-time installer: a module
 * merely being linked no longer mutates global state before main(),
 * and there is no abort() path on a constructor. */
struct yetty_ycore_void_result yetty_ygui_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ygui_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ygui_register: add_accessor_lookup");
    {
        struct yetty_ycore_void_result add_skel_r =
            yetty_yclass_rpc_add_skel_lookup(yetty_ygui_skel_lookup);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_skel_r,
                            "yetty_ygui_register: rpc_add_skel_lookup");
    }
    registered = true;
    return YETTY_OK_VOID();
}
