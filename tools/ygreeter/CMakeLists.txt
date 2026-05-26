# ygreeter — first-contact tool. Dual-mode:
#   - TERM_PROGRAM=yetty  → ships OSC envelopes over stdout to the host.
#   - otherwise           → opens its own GPU window via yinit_run +
#                           yframework_create, renders the figure tree
#                           locally through an in-process wire SM →
#                           yfigure_container → render_target chain.
#
# ygui itself has no knowledge of either mode; this file is the wiring.
#
# Linux + webgpu-only for v1, matching yaudio (the only other
# standalone-window app using the same yinit/yframework bring-up).

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

if(NOT YETTY_ENABLE_LIB_WEBGPU)
    return()
endif()

# Platform / yinit / yframework / yconfig sources — same set yaudio
# inlines, because there isn't a single static lib for the yinit
# layer (window, surface, event-pipes), so each standalone-window
# binary pulls the sources directly.
set(YGREETER_PLATFORM_SOURCES
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

add_executable(ygreeter
    main.c
    ${YGREETER_PLATFORM_SOURCES}
)

target_include_directories(ygreeter BEFORE PRIVATE
    ${YETTY_ROOT}/src
    ${YETTY_ROOT}/include
)

target_compile_definitions(ygreeter PRIVATE
    YETTY_WEB=0
    YETTY_ANDROID=0
    YETTY_USE_FORKPTY=1
    YETTY_YGUI_HAS_UV
)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)

target_link_libraries(ygreeter PRIVATE
    ygui
    yetty_ywire
    yetty_yfigure
    yetty_ygrid
    yetty_yfont_core
    ${YETTY_LIBS}
    yetty_yplatform_core
    yetty_yplatform_move_resize
    yetty_yco
    yetty_telnet
    ${BROTLIDEC_LIBRARIES}
    Threads::Threads
    ${FONTCONFIG_LIBRARIES}
    rt
    util
)
target_include_directories(ygreeter PRIVATE ${FONTCONFIG_INCLUDE_DIRS})

# Dawn (libwebgpu_dawn) is C++ — needs C++ runtime even though our
# sources are C. Same trick yaudio uses.
set_target_properties(ygreeter PROPERTIES LINKER_LANGUAGE CXX)

if(NOT MSVC)
    target_compile_options(ygreeter PRIVATE -Wall -Wextra)
endif()
