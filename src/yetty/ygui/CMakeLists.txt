# ygui — pure-C widget toolkit, object-system based.
#
# Single static library. The framework is libuv-driven by design — no
# libuv-free variant. Standalone tools (ygreeter, ytop) link `ygui`
# directly. Embedders that want to host the framework on top of their
# own event loop pass an existing uv_loop_t into yetty_ygui_framework_create.

set(YGUI_SOURCES
    class.c
    object.c
    widget.c
    primitive-widget.c
    framework.c
    event.c
    layout.c
    mixins/clickable.c
    widgets/label.c
    widgets/breadcrumbs.c
    widgets/button.c
    widgets/checkbox.c
    widgets/chip.c
    widgets/choicebox.c
    widgets/collapsing_header.c
    widgets/colorpicker.c
    widgets/combobox.c
    widgets/dialog.c
    widgets/dropdown.c
    widgets/hbox.c
    widgets/list.c
    widgets/menubar.c
    widgets/popup_menu.c
    widgets/progress.c
    widgets/radio.c
    widgets/rich.c
    widgets/scrollarea.c
    widgets/selectable.c
    widgets/separator.c
    widgets/slider.c
    widgets/spinner.c
    widgets/splitter.c
    widgets/statusbar.c
    widgets/stepper.c
    widgets/tabbar.c
    widgets/table.c
    widgets/textarea.c
    widgets/textinput.c
    widgets/toggle.c
    widgets/tooltip.c
    widgets/tree_node.c
    widgets/vbox.c
    widgets/panel.c
    widgets/ybrowser.c
    widgets/ydraw_embed.c
    widgets/yimage.c
    widgets/yjungle.c
    widgets/ymarkdown.c
    widgets/ypdf.c
    widgets/yplot.c
    widgets/yvideo.c
    widgets/yzoo.c
)

add_library(ygui STATIC ${YGUI_SOURCES})

target_include_directories(ygui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${YETTY_ROOT}/include
)

target_link_libraries(ygui PUBLIC
    yetty_ycore
    yetty_ydraw_core
    yetty_yface
    yetty_yfigure
    yetty_yplatform_core
    yetty_ysdf
    yetty_yplot
    yetty_ymarkdown
    yetty_ybrowser
    yetty_ypdf
)

set_target_properties(ygui PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

install(TARGETS ygui ARCHIVE DESTINATION lib)
install(DIRECTORY ${YETTY_ROOT}/include/yetty/ygui/
        DESTINATION include/yetty/ygui
        FILES_MATCHING PATTERN "*.h")
