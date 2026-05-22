# tools/yaudio - audio analyzer GUI.
#
# Reuses the yinit platform-bootstrap layer (window, WGPU surface,
# event pipes) compiled inline alongside this binary. Same platform
# source list as the main yetty target — minus ymain/glfw.c (we
# supply our own main here) and the yetty-specific YETTY_SOURCES.

# Linux-only for v1. The other platforms' yinit code still has the
# event-driven lifecycle that doesn't fit yinit_run yet.
if(NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
    return()
endif()

if(NOT YETTY_ENABLE_LIB_WEBGPU)
    return()
endif()

set(YAUDIO_PLATFORM_SOURCES
    ${YETTY_ROOT}/src/yetty/yinit/glfw.c
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

add_executable(yaudio
    main.c
    ${YAUDIO_PLATFORM_SOURCES}
)

target_include_directories(yaudio BEFORE PRIVATE
    ${YETTY_ROOT}/src
    ${YETTY_ROOT}/include
)

target_compile_definitions(yaudio PRIVATE
    YETTY_WEB=0
    YETTY_ANDROID=0
    YETTY_USE_FORKPTY=1
)

# Link against the platform/core libs that the inline yplatform/yinit
# sources reference. Same set yetty itself uses, minus the yetty-
# terminal libs (yterm/yui/yrich/...) we don't need. PUBLIC includes
# propagate webgpu headers (yetty_ywebgpu) and GLFW headers (glfw3).
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)

target_link_libraries(yaudio PRIVATE
    yetty_yaudio
    ${YETTY_LIBS}
    yetty_yplatform_core
    yetty_yplatform_wayland_move
    yetty_yco
    yetty_telnet
    ${BROTLIDEC_LIBRARIES}
    Threads::Threads
    ${FONTCONFIG_LIBRARIES}
    rt
    util
)
target_include_directories(yaudio PRIVATE ${FONTCONFIG_INCLUDE_DIRS})

# Dawn (libwebgpu_dawn) is C++ — needs the C++ runtime even though all
# our sources are C. Force C++ linker language so libstdc++ gets pulled
# in automatically (same de facto outcome as yetty itself, which links
# C++ libs like msdfgen transitively).
set_target_properties(yaudio PROPERTIES LINKER_LANGUAGE CXX)

if(NOT MSVC)
    target_compile_options(yaudio PRIVATE -Wall -Wextra)
endif()
