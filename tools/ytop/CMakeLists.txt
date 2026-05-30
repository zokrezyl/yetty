# ytop — top-style live process monitor demoing ygui's TABLE widget
# alongside per-core CPU progress bars.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # /proc/* is Linux-only. Skip silently elsewhere — the rest of the
    # tools/ tree still builds on macOS / Windows.
    return()
endif()

# ytrace is compiled per-final-target (not a standalone library); the
# rest (thread/term/etc.) come from yetty_yplatform_core, pulled in
# transitively by the new ygui.
add_executable(ytop
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ytop PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)

# ygui pulls in ydraw-core, ysdf, ycore, yface, yfont, yfigure,
# yplatform_core, etc. as transitive PUBLIC deps. The new ygui is a pure
# widget framework (no libuv of its own) and ytop drives its own libuv
# client loop, so libuv (uv_a) is listed explicitly.
target_link_libraries(ytop PRIVATE ygui uv_a Threads::Threads)

if(NOT MSVC)
    target_compile_options(ytop PRIVATE -Wall -Wextra)
endif()
