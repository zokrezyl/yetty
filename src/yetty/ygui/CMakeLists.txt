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
    widgets/button.c
    widgets/checkbox.c
    widgets/dialog.c
    widgets/hbox.c
    widgets/menubar.c
    widgets/progress.c
    widgets/statusbar.c
    widgets/separator.c
    widgets/slider.c
    widgets/toggle.c
    widgets/vbox.c
    widgets/panel.c
    widgets/popup_menu.c
    widgets/tabbar.c
    widgets/tooltip.c
    widgets/yimage.c
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
)

set_target_properties(ygui PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

install(TARGETS ygui ARCHIVE DESTINATION lib)
install(DIRECTORY ${YETTY_ROOT}/include/yetty/ygui/
        DESTINATION include/yetty/ygui
        FILES_MATCHING PATTERN "*.h")
