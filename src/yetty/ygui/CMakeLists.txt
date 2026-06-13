# ygui — pure-C widget toolkit, object-system based.
#
# Single static library. The framework is libuv-driven by design — no
# libuv-free variant. Standalone tools (ygreeter, ytop) link `ygui`
# directly. Embedders that want to host the framework on top of their
# own event loop pass an existing uv_loop_t into yetty_ygui_framework_create.

# Annotated sources processed by yclass codegen. Every widget under
# widgets/ plus the framework base (widget, primitive-widget,
# mixins/clickable) carries `[[clang::annotate("class@ygui:…")]]` on
# its data struct and `override@ygui:<class>:<slot>` on each impl.
# Codegen runs as a standalone `make codegen` target at the repo
# root, NOT as part of any platform build. It emits each class's public
# header include/yetty/ygui/<name>.h (accessor decl + the public stubs
# and `<slot>_fn` typedefs for the slots that class introduces) plus
# rpc.h, and the internal sources next to the annotated .c files
# (methods.gen.c, rpc.gen.c, and per-class <stem>.gen.c). The .c files
# include their per-class `<stem>.gen.c` at the foot to pull in the
# accessor body. All generated files are committed to git; platform
# builds compile what is in the tree.
set(YGUI_ANNOTATED
    ${CMAKE_CURRENT_SOURCE_DIR}/widget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/primitive-widget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mixins/clickable.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mixins/draggable.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/breadcrumbs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/button.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/checkbox.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/chip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/choicebox.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/collapsing_header.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/colorpicker.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/combobox.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/datepicker.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/dialog.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/dropdown.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/filepicker.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/hbox.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/label.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/list.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/menubar.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/panel.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/popup_menu.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/progress.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/radio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/rich.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/scrollarea.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/selectable.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/separator.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/slider.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/spinner.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/splitter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/statusbar.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/stepper.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/tabbar.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/table.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/textarea.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/textinput.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/toggle.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/tooltip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/tree_node.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/vbox.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/window.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/ybrowser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/ydiagram.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/ydraw_embed.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/yimage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/yjungle.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/ymarkdown.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/ymaze.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/ynode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/ynodes.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/ypdf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/yplot.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/yrich_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/yshadertoy.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/yvideo.c
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/yzoo.c
)
set(YGUI_SOURCES
    dispatch.c
    framework.c
    event.c
    layout.c
    theme.c
    ${YGUI_ANNOTATED}
)

add_library(yetty_ygui STATIC ${YGUI_SOURCES})

target_include_directories(yetty_ygui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (methods.gen.c, rpc.gen.c, per-class
# <stem>.gen.c) sit under src/yetty/ygui/. The src tree is a PRIVATE -I
# prefix for this target so those sources resolve their siblings without
# the path leaking to consumers; public callers go through the per-class
# headers + rpc.h under include/yetty/ygui/.
target_include_directories(yetty_ygui PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_ygui PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_ydraw_core
    yetty_yface
    yetty_yfigure
    yetty_yplatform_core
    yetty_ysdf
    yetty_yplot_core
    yetty_yimage_core
    yetty_ymarkdown
    yetty_yrich
    yetty_ymaze
    yetty_yzoo
    yetty_yjungle
    yetty_yvideo_core
)

# The ypdf and ybrowser widgets wrap optional backends (pdfio, lexbor) that
# aren't available on every platform — webasm, for instance, has no
# pdfio/lexbor prebuilts. The widget sources compile unconditionally so the
# codegen-emitted class accessors (referenced from rpc.gen.c / methods.gen.c)
# resolve on every platform; their rendering paths are guarded by these
# definitions, and the backend libraries are only linked where the feature
# is actually enabled.
if(YETTY_ENABLE_FEATURE_YPDF)
    target_link_libraries(yetty_ygui PUBLIC yetty_ypdf)
    target_compile_definitions(yetty_ygui PRIVATE YETTY_YGUI_HAVE_YPDF=1)
endif()
if(YETTY_ENABLE_FEATURE_YBROWSER)
    target_link_libraries(yetty_ygui PUBLIC yetty_ybrowser)
    target_compile_definitions(yetty_ygui PRIVATE YETTY_YGUI_HAVE_YBROWSER=1)
endif()

# The ydiagram widget wraps the Mermaid → ydraw renderer. The widget source
# compiles unconditionally so its codegen-emitted class accessor resolves on
# every platform; the render path is guarded by this definition and the
# library is only linked where the feature is enabled.
if(YETTY_ENABLE_FEATURE_YDIAGRAM)
    target_link_libraries(yetty_ygui PUBLIC yetty_ydiagram)
    target_compile_definitions(yetty_ygui PRIVATE YETTY_YGUI_HAVE_YDIAGRAM=1)
endif()

# C2x so `[[clang::annotate(...)]]` attribute syntax parses (yclass
# codegen reads these annotations off the AST). Switched up from C11.
#
# MSVC can't express the C23 dialect through CMake's C_STANDARD
# abstraction (CMake fails generation with "compiler does not support
# this"), but the Windows build already passes /std:clatest globally
# (build-tools/yetty/platform/windows/cmake.cmake), which parses the
# same `[[...]]` attributes — so apply the property only off-MSVC.
if(NOT MSVC)
    set_target_properties(yetty_ygui PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()

install(TARGETS yetty_ygui ARCHIVE DESTINATION lib)
install(DIRECTORY ${YETTY_ROOT}/include/yetty/ygui/
        DESTINATION include/yetty/ygui
        FILES_MATCHING PATTERN "*.h")
