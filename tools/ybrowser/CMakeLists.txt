# ybrowser — HTML tool with three modes:
#   --once / non-TTY : render a page → one ydraw OSC envelope → exit (legacy).
#   client (in yetty): Chrome-like ygui client over OSC (no wheel — the host
#                      yetty eats the wheel for scrollback).
#   standalone       : own GPU window; local mouse/keyboard, so scrolling,
#                      clicking and link navigation all work.
#
# Standalone needs the yinit/yframework GPU bring-up (Linux + WebGPU), the
# same sources the ygui demos inline. When unavailable we still build the
# one-shot + client modes.

set(YBROWSER_STANDALONE OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND YETTY_ENABLE_LIB_WEBGPU)
    set(YBROWSER_STANDALONE ON)
endif()

add_executable(ybrowser
    main.c
    browser-ui.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    # yetty_ybrowser (linked below, every mode) calls
    # yetty_yplatform_yworkpool_submit from ybrowser-paint.c to decode images
    # off-thread, so this binary must always provide the workpool impl — not
    # just in the Linux standalone path. It only uses the cross-platform thread
    # abstraction (resolved transitively through yetty_ygui → yetty_yplatform_core),
    # so it builds on macOS / Windows too.
    ${YETTY_ROOT}/src/yetty/yplatform/yworkpool/default.c
)

target_include_directories(ybrowser PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)

target_link_libraries(ybrowser PRIVATE
    yetty_ygui
    yetty_ybrowser
    yetty_ysvg
    yetty_yimage_core
    yetty_ydraw_core
    yetty_ysdf
    yetty_yface
    yetty_ycore
    CURL::libcurl
    Threads::Threads
)

target_compile_options(ybrowser PRIVATE -Wall -Wno-unused-parameter)

if(YBROWSER_STANDALONE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(YBROWSER_FONTCONFIG REQUIRED fontconfig)

    # Platform / yinit / yframework sources — no static lib exists for this
    # layer (window, surface, event pipes carry file-static state), so each
    # standalone-window binary inlines them, same as demo/ygui/runner.
    target_sources(ybrowser PRIVATE
        ${YETTY_ROOT}/src/yetty/yinit/glfw.c
        ${YETTY_ROOT}/src/yetty/yframework/yframework.c
        ${YETTY_ROOT}/src/yetty/yplatform/os-event-loop/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/clipboard/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/window/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/coroutine/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/webgpu/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/pipe/default.c
        ${YETTY_ROOT}/src/yetty/yncbin/incbin-assets.c
        ${YETTY_ROOT}/src/yetty/ypty/forkpty.c
        ${YETTY_ROOT}/src/yetty/ypty/memory-pty.c
        ${YETTY_ROOT}/src/yetty/yconfig/config.c
        ${YETTY_ROOT}/src/yetty/ynotify/ynotify.c
    )

    target_include_directories(ybrowser PRIVATE ${YBROWSER_FONTCONFIG_INCLUDE_DIRS})

    target_compile_definitions(ybrowser PRIVATE
        YETTY_WEB=0
        YETTY_ANDROID=0
        YETTY_USE_FORKPTY=1
        YETTY_YGUI_HAS_UV
        YETTY_YBROWSER_HAS_STANDALONE
    )

    target_link_libraries(ybrowser PRIVATE
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
        yetty_yplatform_window_chrome
        yetty_ychrome
        yetty_yco
        yetty_telnet
        ${BROTLIDEC_LIBRARIES}
        ${YBROWSER_FONTCONFIG_LIBRARIES}
        rt
        util
    )

    # libwebgpu_dawn is C++, so the final link needs the C++ runtime.
    set_target_properties(ybrowser PROPERTIES LINKER_LANGUAGE CXX)
endif()
