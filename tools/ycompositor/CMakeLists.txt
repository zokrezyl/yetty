# tools/ycompositor — standalone ycompositor test harness.
#
# Drives a ycompositor + one ygrid figure straight from C (no terminal,
# no yui, no ygui), so the new compositor render path can be exercised
# in isolation. Builds on Linux and Windows (standalone GPU window).

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT WIN32)
    return()
endif()

if(NOT YETTY_ENABLE_LIB_WEBGPU)
    return()
endif()

set(YCOMP_PLATFORM_SOURCES
    ${YETTY_ROOT}/src/yetty/yinit/glfw.c
    ${YETTY_ROOT}/src/yetty/yframework/yframework.c
    ${YETTY_ROOT}/src/yetty/yplatform/os-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/clipboard/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/window/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/coroutine/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/yworkpool/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/extract-assets/default.c
    ${YETTY_ROOT}/src/yetty/yncbin/incbin-assets.c
    ${YETTY_ROOT}/src/yetty/ypty/memory-pty.c
    ${YETTY_ROOT}/src/yetty/yconfig/config.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/ynotify/ynotify.c
)
# Platform pipe + pty backend. See tools/ygreeter for why Windows omits
# conpty.c (it drags in qemu/tinyemu via the pty factory).
if(WIN32)
    list(APPEND YCOMP_PLATFORM_SOURCES
        ${YETTY_ROOT}/src/yetty/yplatform/pipe/windows.c)
else()
    list(APPEND YCOMP_PLATFORM_SOURCES
        ${YETTY_ROOT}/src/yetty/yplatform/pipe/default.c
        ${YETTY_ROOT}/src/yetty/ypty/forkpty.c)
endif()

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
)
if(WIN32)
    target_compile_definitions(ycompositor PRIVATE
        YETTY_USE_CONPTY=1 YETTY_USE_DIRECTWRITE=1 NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_definitions(ycompositor PRIVATE YETTY_USE_FORKPTY=1)
endif()

find_package(Threads REQUIRED)
# fontconfig: Linux standalone font discovery; Windows uses DirectWrite.
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
endif()

target_link_libraries(ycompositor PRIVATE
    yetty_ygrid
    yetty_yfigure
    yetty_ydraw_core
    yetty_yframework
    ${YETTY_LIBS}
    yetty_ywire
    yetty_ydraw_core
    yetty_yplatform_core
    yetty_yplatform_move_resize
    yetty_yplatform_window_manager
    yetty_yco
    yetty_telnet
    ${BROTLIDEC_LIBRARIES}
    Threads::Threads
    ${FONTCONFIG_LIBRARIES}
)
if(WIN32)
    target_link_libraries(ycompositor PRIVATE ws2_32 dwrite)
else()
    target_link_libraries(ycompositor PRIVATE rt util)
endif()

# drawable-iterator (in libyetty_ydraw_core.a) calls into libyetty_ywire.a.
# GNU ld needs --start-group/--end-group to re-scan the static archives
# until all cross-references resolve. MSVC's link.exe re-scans by default,
# so the grouping is GNU-ld only.
if(NOT MSVC)
    target_link_options(ycompositor PRIVATE "-Wl,--start-group")
    target_link_libraries(ycompositor PRIVATE "-Wl,--end-group")
endif()
target_include_directories(ycompositor PRIVATE ${FONTCONFIG_INCLUDE_DIRS})

set_target_properties(ycompositor PROPERTIES LINKER_LANGUAGE CXX)

if(NOT MSVC)
    target_compile_options(ycompositor PRIVATE -Wall -Wextra)
endif()

# Standalone GPU window — provision the Dawn/D3D12 runtime DLLs on Windows.
if(WIN32)
    include(${YETTY_ROOT}/build-tools/yetty/dawn-runtime-dlls.cmake)
    yetty_copy_dawn_runtime_dlls(ycompositor)
endif()
