# yai — AI-CLI loop rendering into yetty: scrolling conversation in the
# terminal, non-scrolling status in a floating ygui HUD panel.
#
# The engine is a yclass class hierarchy (engine.c base; claude.c,
# codex.c, gemini.c subclasses; turn-engine.c per-turn intermediate) —
# methods.gen.c / rpc.gen.c / *.gen.c come from `make codegen`. Drives
# the headless CLIs over JSONL (parsed with the prebuilt yyjson
# 3rdparty lib) on a libuv event loop.

include(${YETTY_ROOT}/build-tools/yetty/libs/yyjson.cmake)

# ytrace is compiled per-final-target (not a standalone library); the
# rest comes transitively from yetty_ygui (ydraw-core, yface, yfont,
# yplatform_core, ...).
add_executable(yai
    main.c
    render.c
    hud.c
    commands.c
    event.c
    control.c
    engine.c
    turn-engine.c
    claude.c
    codex.c
    gemini.c
    editor.c
    editor-emacs.c
    editor-vi.c
    methods.gen.c
    rpc.gen.c
    # External msgpack-RPC control server reuses yetty's framing layer.
    ${YETTY_ROOT}/src/yetty/yctl/rpc-message.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yai PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)

# yetty_ygui is a pure widget framework (no libuv of its own); yai drives
# its own libuv client loop, so uv_a is listed explicitly.
# yetty_yfont_shader_glyph is the name->codepoint table for the activity
# status line's animated glyph (PUA-B, rendered by the host text layer).
target_link_libraries(yai PRIVATE yetty_ygui yetty_yclass yetty_yfont_shader_glyph
                                  yyjson::yyjson uv_a Threads::Threads msgpack-c)

# The engine classes carry `[[clang::annotate(...)]]` (C23 attribute
# syntax); see ygrid/CMakeLists.txt for the rationale.
if(NOT MSVC)
    set_target_properties(yai PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
    target_compile_options(yai PRIVATE -Wall -Wextra)
endif()
