# ygui C demos — each builds a tiny program that uses the ygui widget API.
# ygui accumulates primitives into a ydraw-core buffer and ships them as
# base64-encoded OSC sequences to a running yetty instance, which decodes
# them via yetty_ydraw_core_buffer_create_from_base64 and renders.
# See src/yetty/ygui/ydraw-capi.gen.{h,c} for the ydraw buffer shim.

# Shared support object: ytrace + ymutex/term platform glue. Compiled once
# and linked into each demo exec so we avoid static-archive ordering issues
# (ygui/ydraw-core reference these symbols; they need to be in the final
# exec object list, not hidden deeper in the archive chain).
add_library(demo_ygui_support OBJECT
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)
target_include_directories(demo_ygui_support PUBLIC ${YETTY_ROOT}/include)

add_subdirectory(01_button_test)
add_subdirectory(02_coord_debug)
add_subdirectory(03_all_widgets)
add_subdirectory(04_edge_test)
add_subdirectory(05_debug_events)
add_subdirectory(06_hello_button)
add_subdirectory(07_label_and_button)
add_subdirectory(08_slider)
add_subdirectory(09_checkbox)
add_subdirectory(10_panel_layout)
add_subdirectory(11_progress_bar)
add_subdirectory(12_calculator)
add_subdirectory(13_color_mixer)
add_subdirectory(14_todo_list)
add_subdirectory(15_dashboard)
add_subdirectory(16_new_widgets)
add_subdirectory(17_flex_row)
add_subdirectory(18_flex_column)
add_subdirectory(19_flex_dashboard)
add_subdirectory(20_tree_view)
add_subdirectory(21_tree_with_panes)
add_subdirectory(22_tree_complex)
add_subdirectory(23_rich_tabbar)
