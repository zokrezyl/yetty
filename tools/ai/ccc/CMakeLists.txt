# ccc — `claude` CLI loop rendering into yetty: scrolling conversation in
# the terminal, non-scrolling status in a floating ygui HUD panel.
#
# Drives the headless `claude` CLI over stream-json (parsed with the
# prebuilt yyjson 3rdparty lib) on a libuv event loop.

include(${YETTY_ROOT}/build-tools/yetty/libs/yyjson.cmake)

# ytrace is compiled per-final-target (not a standalone library); the
# rest comes transitively from yetty_ygui (ydraw-core, yface, yfont,
# yplatform_core, ...).
add_executable(ccc
    main.c
    render.c
    hud.c
    commands.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ccc PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)

# yetty_ygui is a pure widget framework (no libuv of its own); ccc drives
# its own libuv client loop, so uv_a is listed explicitly.
# yetty_yfont_shader_glyph is the name->codepoint table for the activity
# status line's animated glyph (PUA-B, rendered by the host text layer).
target_link_libraries(ccc PRIVATE yetty_ygui yetty_yfont_shader_glyph yyjson::yyjson uv_a
                                  Threads::Threads)

if(NOT MSVC)
    target_compile_options(ccc PRIVATE -Wall -Wextra)
endif()
