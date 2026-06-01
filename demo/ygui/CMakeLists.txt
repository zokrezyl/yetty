# ygui C demos — each builds a tiny program that uses the new ygui
# widget API. The standalone bring-up (window, GPU, font, container,
# wire SM, event loop) lives in runner.{h,c}; each demo only provides
# build_ui().
#
# Builds gated on Linux + WebGPU (matches ygreeter — the only other
# standalone-window app sharing the same yinit/yframework path).

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

if(NOT YETTY_ENABLE_LIB_WEBGPU)
    return()
endif()

# Platform / yinit / yframework / yconfig sources — same set ygreeter
# inlines. There is no static lib for the yinit layer (window, surface,
# event-pipes), so each standalone-window binary pulls the sources
# directly.
set(DEMO_YGUI_PLATFORM_SOURCES
    ${YETTY_ROOT}/src/yetty/yinit/glfw.c
    ${YETTY_ROOT}/src/yetty/yframework/yframework.c
    ${YETTY_ROOT}/src/yetty/yplatform/os-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/clipboard/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/window/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/window-manager/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/coroutine/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/yworkpool/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/pipe/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/extract-assets/default.c
    ${YETTY_ROOT}/src/yetty/yncbin/incbin-assets.c
    ${YETTY_ROOT}/src/yetty/ypty/forkpty.c
    ${YETTY_ROOT}/src/yetty/ypty/memory-pty.c
    ${YETTY_ROOT}/src/yetty/yconfig/config.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/ynotify/ynotify.c
)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DEMO_FONTCONFIG REQUIRED fontconfig)

# OBJECT library: the runner + platform sources get compiled once and
# linked into each demo executable. (A STATIC archive doesn't work for
# the platform sources — they carry file-static state that the linker
# must keep available; the same trick ygreeter uses.)
add_library(demo_ygui_runner OBJECT
    runner.c
    # Shared Shadertoy gallery (demo 41 / ygreeter Shadertoy tab). Plain
    # string data, no GPU deps — compiled in directly rather than pulled
    # from the webgpu-bound yetty_yshadertoy lib.
    ${YETTY_ROOT}/src/yetty/yshadertoy/demo-shaders.c
    ${DEMO_YGUI_PLATFORM_SOURCES}
)
target_include_directories(demo_ygui_runner PUBLIC
    ${YETTY_ROOT}/src
    ${YETTY_ROOT}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DEMO_FONTCONFIG_INCLUDE_DIRS}
)
target_compile_definitions(demo_ygui_runner PUBLIC
    YETTY_WEB=0
    YETTY_ANDROID=0
    YETTY_USE_FORKPTY=1
    YETTY_YGUI_HAS_UV
)
# Inherit usage requirements (include dirs, defs) from every dep that
# the inlined platform sources transitively reach: webgpu headers from
# Dawn, glfw, ygui's own headers, yetty_yplatform_*, etc. OBJECT libs
# propagate these to consumers but also use them to compile their own
# sources — exactly what we need for runner.c + the platform .c files.
target_link_libraries(demo_ygui_runner PUBLIC
    ygui
    yetty_ywire
    yetty_yfigure
    yetty_ygrid
    yetty_yshadertoy
    yetty_yfont_core
    yetty_ydraw_factory
    yetty_yimage
    yetty_yplot
    ${YETTY_LIBS}
    yetty_yplatform_core
    yetty_yplatform_move_resize
    yetty_yco
    yetty_telnet
)

# Helper macro: define one demo executable. `name` is the suffix
# (e.g. `00-simple` → `demo-ygui-00-simple`). Extra link libs (for
# producer-widget demos) come after via `EXTRA_LIBS`.
function(demo_ygui_add name)
    set(target demo-ygui-${name})
    set(srcdir ${CMAKE_CURRENT_SOURCE_DIR}/${ARGV1})
    if(NOT ARGV1)
        message(FATAL_ERROR "demo_ygui_add: missing source dir")
    endif()
    add_executable(${target}
        ${srcdir}/main.c
        $<TARGET_OBJECTS:demo_ygui_runner>
    )
    target_include_directories(${target} PRIVATE
        ${YETTY_ROOT}/src
        ${YETTY_ROOT}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(${target} PRIVATE
        YETTY_WEB=0
        YETTY_ANDROID=0
        YETTY_USE_FORKPTY=1
    )
    target_link_libraries(${target} PRIVATE
        ygui
        yetty_ywire
        yetty_yfigure
        yetty_ygrid
        yetty_yfont_core
        yetty_ydraw_factory
        yetty_yimage
        yetty_yplot
        ${YETTY_LIBS}
        yetty_yplatform_core
        yetty_yplatform_move_resize
        yetty_yco
        yetty_telnet
        ${BROTLIDEC_LIBRARIES}
        Threads::Threads
        ${DEMO_FONTCONFIG_LIBRARIES}
        rt
        util
    )
    # libwebgpu_dawn is C++, so the final link needs the C++ runtime.
    set_target_properties(${target} PROPERTIES LINKER_LANGUAGE CXX)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

demo_ygui_add(00-simple 00_simple)
demo_ygui_add(01-button 01_button_test)
demo_ygui_add(02-coord-debug 02_coord_debug)
demo_ygui_add(03-widgets 03_all_widgets)
demo_ygui_add(04-edge 04_edge_test)
demo_ygui_add(05-debug-events 05_debug_events)
demo_ygui_add(06-hello-button 06_hello_button)
demo_ygui_add(07-label-and-button 07_label_and_button)
demo_ygui_add(08-slider 08_slider)
demo_ygui_add(09-checkbox 09_checkbox)
demo_ygui_add(10-panel-layout 10_panel_layout)
demo_ygui_add(11-progress 11_progress_bar)
demo_ygui_add(12-calculator 12_calculator)
demo_ygui_add(13-color-mixer 13_color_mixer)
demo_ygui_add(14-todo-list 14_todo_list)
demo_ygui_add(15-dashboard 15_dashboard)
demo_ygui_add(16-new-widgets 16_new_widgets)
demo_ygui_add(17-flex-row 17_flex_row)
demo_ygui_add(18-flex-column 18_flex_column)
demo_ygui_add(19-flex-dashboard 19_flex_dashboard)
demo_ygui_add(20-tree-view 20_tree_view)
demo_ygui_add(21-tree-with-panes 21_tree_with_panes)
demo_ygui_add(22-tree-complex 22_tree_complex)
demo_ygui_add(23-rich-tabbar 23_rich_tabbar)

if(YETTY_ENABLE_FEATURE_YMARKDOWN)
    demo_ygui_add(24-ymarkdown 24_ymarkdown)
endif()
if(YETTY_ENABLE_FEATURE_YPDF)
    demo_ygui_add(25-ypdf 25_ypdf)
endif()
if(YETTY_ENABLE_FEATURE_YBROWSER)
    demo_ygui_add(26-ybrowser 26_ybrowser)
endif()
if(YETTY_ENABLE_FEATURE_YJUNGLE)
    demo_ygui_add(27-yjungle 27_yjungle)
endif()
demo_ygui_add(40-ymaze 40_ymaze)
demo_ygui_add(41-yshadertoy 41_yshadertoy)
if(YETTY_ENABLE_FEATURE_YZOO)
    demo_ygui_add(28-yzoo 28_yzoo)
endif()
if(YETTY_ENABLE_FEATURE_YIMAGE)
    demo_ygui_add(29-yimage 29_yimage)
endif()

if(YETTY_ENABLE_FEATURE_YDIAGRAM)
    demo_ygui_add(36-ydiagram 36_ydiagram)
endif()

demo_ygui_add(30-radio 30_radio)
demo_ygui_add(31-spinner 31_spinner)
demo_ygui_add(32-splitter 32_splitter)
demo_ygui_add(33-dialog 33_dialog)
demo_ygui_add(34-textarea 34_textarea)
demo_ygui_add(35-collapsing-header 35_collapsing_header_open)
demo_ygui_add(37-scrollarea 37_scrollarea)
demo_ygui_add(38-ynodes 38_ynodes)
