/* GENERATED — do not edit. */
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yclass/class.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_ycore_void_result yetty_demoygui_00_simple_register(void);
struct yetty_ycore_void_result yetty_demoygui_01_button_test_register(void);
struct yetty_ycore_void_result yetty_demoygui_02_coord_debug_register(void);
struct yetty_ycore_void_result yetty_demoygui_03_all_widgets_register(void);
struct yetty_ycore_void_result yetty_demoygui_04_edge_test_register(void);
struct yetty_ycore_void_result yetty_demoygui_05_debug_events_register(void);
struct yetty_ycore_void_result yetty_demoygui_06_hello_button_register(void);
struct yetty_ycore_void_result yetty_demoygui_07_label_and_button_register(void);
struct yetty_ycore_void_result yetty_demoygui_08_slider_register(void);
struct yetty_ycore_void_result yetty_demoygui_09_checkbox_register(void);
struct yetty_ycore_void_result yetty_demoygui_10_panel_layout_register(void);
struct yetty_ycore_void_result yetty_demoygui_11_progress_bar_register(void);
struct yetty_ycore_void_result yetty_demoygui_12_calculator_register(void);
struct yetty_ycore_void_result yetty_demoygui_13_color_mixer_register(void);
struct yetty_ycore_void_result yetty_demoygui_14_todo_list_register(void);
struct yetty_ycore_void_result yetty_demoygui_15_dashboard_register(void);
struct yetty_ycore_void_result yetty_demoygui_16_new_widgets_register(void);
struct yetty_ycore_void_result yetty_demoygui_17_flex_row_register(void);
struct yetty_ycore_void_result yetty_demoygui_18_flex_column_register(void);
struct yetty_ycore_void_result yetty_demoygui_19_flex_dashboard_register(void);
struct yetty_ycore_void_result yetty_demoygui_20_tree_view_register(void);
struct yetty_ycore_void_result yetty_demoygui_21_tree_with_panes_register(void);
struct yetty_ycore_void_result yetty_demoygui_22_tree_complex_register(void);
struct yetty_ycore_void_result yetty_demoygui_23_rich_tabbar_register(void);
struct yetty_ycore_void_result yetty_demoygui_24_ymarkdown_register(void);
struct yetty_ycore_void_result yetty_demoygui_25_ypdf_register(void);
struct yetty_ycore_void_result yetty_demoygui_26_ybrowser_register(void);
struct yetty_ycore_void_result yetty_demoygui_27_yjungle_register(void);
struct yetty_ycore_void_result yetty_demoygui_28_yzoo_register(void);
struct yetty_ycore_void_result yetty_demoygui_29_yimage_register(void);
struct yetty_ycore_void_result yetty_demoygui_30_radio_register(void);
struct yetty_ycore_void_result yetty_demoygui_31_spinner_register(void);
struct yetty_ycore_void_result yetty_demoygui_32_splitter_register(void);
struct yetty_ycore_void_result yetty_demoygui_33_dialog_register(void);
struct yetty_ycore_void_result yetty_demoygui_34_textarea_register(void);
struct yetty_ycore_void_result yetty_demoygui_35_collapsing_header_open_register(void);
struct yetty_ycore_void_result yetty_demoygui_36_ydiagram_register(void);
struct yetty_ycore_void_result yetty_demoygui_37_scrollarea_register(void);
struct yetty_ycore_void_result yetty_demoygui_38_ynodes_register(void);
struct yetty_ycore_void_result yetty_demoygui_40_ymaze_register(void);
struct yetty_ycore_void_result yetty_demoygui_41_yshadertoy_register(void);
struct yetty_ycore_void_result yetty_demoygui_42_window_chrome_register(void);
struct yetty_ycore_void_result yetty_demoygui_43_tree_showcase_register(void);
struct yetty_ycore_void_result yetty_demoygui_44_textinput_register(void);
struct yetty_ycore_void_result yetty_demoygui_register(void);


/* ---- demoygui: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_demoygui_register(void)
{
    static bool registered = false;
    if (registered)
        return YETTY_OK_VOID();

    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_00_simple_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_00_simple");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_01_button_test_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_01_button_test");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_02_coord_debug_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_02_coord_debug");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_03_all_widgets_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_03_all_widgets");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_04_edge_test_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_04_edge_test");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_05_debug_events_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_05_debug_events");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_06_hello_button_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_06_hello_button");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_07_label_and_button_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_07_label_and_button");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_08_slider_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_08_slider");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_09_checkbox_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_09_checkbox");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_10_panel_layout_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_10_panel_layout");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_11_progress_bar_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_11_progress_bar");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_12_calculator_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_12_calculator");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_13_color_mixer_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_13_color_mixer");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_14_todo_list_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_14_todo_list");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_15_dashboard_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_15_dashboard");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_16_new_widgets_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_16_new_widgets");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_17_flex_row_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_17_flex_row");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_18_flex_column_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_18_flex_column");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_19_flex_dashboard_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_19_flex_dashboard");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_20_tree_view_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_20_tree_view");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_21_tree_with_panes_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_21_tree_with_panes");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_22_tree_complex_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_22_tree_complex");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_23_rich_tabbar_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_23_rich_tabbar");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_24_ymarkdown_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_24_ymarkdown");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_25_ypdf_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_25_ypdf");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_26_ybrowser_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_26_ybrowser");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_27_yjungle_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_27_yjungle");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_28_yzoo_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_28_yzoo");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_29_yimage_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_29_yimage");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_30_radio_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_30_radio");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_31_spinner_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_31_spinner");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_32_splitter_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_32_splitter");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_33_dialog_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_33_dialog");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_34_textarea_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_34_textarea");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_35_collapsing_header_open_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_35_collapsing_header_open");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_36_ydiagram_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_36_ydiagram");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_37_scrollarea_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_37_scrollarea");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_38_ynodes_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_38_ynodes");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_40_ymaze_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_40_ymaze");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_41_yshadertoy_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_41_yshadertoy");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_42_window_chrome_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_42_window_chrome");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_43_tree_showcase_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_43_tree_showcase");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_demoygui_44_textinput_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_demoygui_register: submodule demoygui_44_textinput");
    }
    registered = true;
    return YETTY_OK_VOID();
}
