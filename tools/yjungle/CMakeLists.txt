# tools/yjungle — standalone animated-jungle app.
#
# Drives a single full-window ygrid figure straight from C (no terminal,
# no yui, no ygui widget tree), rendering the yetty_yjungle each frame.
# Modeled on tools/ymaze.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

if(NOT YETTY_ENABLE_LIB_WEBGPU)
    return()
endif()

set(YJUNGLE_PLATFORM_SOURCES
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

add_executable(yjungle
    main.c
    ${YJUNGLE_PLATFORM_SOURCES}
)

target_include_directories(yjungle BEFORE PRIVATE
    ${YETTY_ROOT}/src
    ${YETTY_ROOT}/include
)

target_compile_definitions(yjungle PRIVATE
    YETTY_WEB=0
    YETTY_ANDROID=0
    YETTY_USE_FORKPTY=1
)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)

target_link_libraries(yjungle PRIVATE
    yetty_yjungle
    yetty_ysdf
    yetty_ygrid
    yetty_yfigure
    yetty_ydraw_core
    yetty_yframework
    ${YETTY_LIBS}
    yetty_ywire
    yetty_ydraw_core
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

target_link_options(yjungle PRIVATE
    "-Wl,--start-group"
)
target_link_libraries(yjungle PRIVATE
    "-Wl,--end-group"
)
target_include_directories(yjungle PRIVATE ${FONTCONFIG_INCLUDE_DIRS})

set_target_properties(yjungle PROPERTIES LINKER_LANGUAGE CXX)

if(NOT MSVC)
    target_compile_options(yjungle PRIVATE -Wall -Wextra)
endif()
