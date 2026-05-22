# tools/ycompositor — standalone ycompositor test harness.
#
# Drives a ycompositor + one ygrid figure straight from C (no terminal,
# no yui, no ygui), so the new compositor render path can be exercised
# in isolation.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

if(NOT YETTY_ENABLE_LIB_WEBGPU)
    return()
endif()

set(YCOMP_PLATFORM_SOURCES
    ${YETTY_ROOT}/src/yetty/yinit/glfw.c
    ${YETTY_ROOT}/src/yetty/yruntime/yruntime.c
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

add_executable(ycompositor
    main.c
    ${YCOMP_PLATFORM_SOURCES}
)

target_include_directories(ycompositor BEFORE PRIVATE
    ${YETTY_ROOT}/src
    ${YETTY_ROOT}/include
)

target_compile_definitions(ycompositor PRIVATE
    YETTY_WEB=0
    YETTY_ANDROID=0
    YETTY_USE_FORKPTY=1
)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)

target_link_libraries(ycompositor PRIVATE
    yetty_ycompositor
    yetty_ygrid
    yetty_yfigure
    yetty_ydraw_core
    yetty_yruntime
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

# drawable-iterator (in libyetty_ydraw_core.a) calls into libyetty_ywire.a.
# CMake's archive ordering pulls ywire in early via the transitive
# ycompositor → ywire link, before ydraw_core's references show up.
# Wrap the static block in a --start-group/--end-group so the linker
# re-scans archives until all references are resolved.
target_link_options(ycompositor PRIVATE
    "-Wl,--start-group"
)
target_link_libraries(ycompositor PRIVATE
    "-Wl,--end-group"
)
target_include_directories(ycompositor PRIVATE ${FONTCONFIG_INCLUDE_DIRS})

set_target_properties(ycompositor PROPERTIES LINKER_LANGUAGE CXX)

if(NOT MSVC)
    target_compile_options(ycompositor PRIVATE -Wall -Wextra)
endif()
