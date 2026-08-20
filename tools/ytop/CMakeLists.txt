# ytop — btop-style live system monitor on the ygui widget engine.
#
# OS-specific data gathering lives behind the platform abstraction layer:
# each abstraction is platform/<abstraction>/<os>.c with a platform-independent
# header. Only the active platform's backends are compiled.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The current backends read /proc + /sys (Linux only). macOS / Windows
    # backends drop in next to the linux.c files later; skip silently for now
    # so the rest of the tools/ tree still builds elsewhere.
    return()
endif()

# Per-abstraction backends for the active platform. On Linux this globs every
# platform/*/linux.c; a future macOS build would glob platform/*/macos.c.
file(GLOB YTOP_PLATFORM_SRC CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/*/linux.c)

# ytrace is compiled per-final-target (not a standalone library); the rest
# (thread/term/etc.) come from yetty_yplatform_core, pulled in transitively by
# the new ygui.
add_executable(ytop
    main.c
    ui.c
    ${YTOP_PLATFORM_SRC}
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ytop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)

# yetty_ygui pulls in ydraw-list, ysdf, ycore, yface, yfont, yfigure, yplot,
# yplatform_core, etc. as transitive PUBLIC deps. The new yetty_ygui is a pure
# widget framework (no libuv of its own) and ytop drives its own libuv client
# loop, so libuv (uv_a) is listed explicitly.
target_link_libraries(ytop PRIVATE yetty_ygui uv_a Threads::Threads)

if(NOT MSVC)
    target_compile_options(ytop PRIVATE -Wall -Wextra)
endif()
