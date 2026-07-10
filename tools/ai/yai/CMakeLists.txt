# yai — AI-CLI loop rendering into yetty: scrolling conversation in the
# terminal, non-scrolling status in a floating ygui HUD panel.
#
# The engine is a yclass class hierarchy (engine.c base; claude.c,
# codex.c, gemini.c subclasses; turn-engine.c per-turn intermediate) —
# the per-class *.gen.c come from `make codegen`. Drives
# the headless CLIs over JSONL (parsed with the prebuilt yyjson
# 3rdparty lib) on a libuv event loop.

include(${YETTY_ROOT}/build-tools/yetty/libs/yyjson.cmake)
# Persisted config (~/.config/yetty/yai.yaml) is parsed + emitted with libyaml.
include(${YETTY_ROOT}/build-tools/yetty/libs/libyaml.cmake)

# ytrace is compiled per-final-target (not a standalone library); the
# rest comes transitively from yetty_ygui (ydraw-core, yface, yfont,
# yplatform_core, ...).
add_executable(yai
    main.c
    render.c
    hud.c
    hud-format.c
    commands.c
    event.c
    control.c
    proxy.c
    shell.c
    picohttpparser/picohttpparser.c
    fzy/match.c
    engine.c
    turn-engine.c
    claude.c
    codex.c
    codex-quota.c
    gemini.c
    editor.c
    editor-emacs.c
    editor-vi.c
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
# yetty_ymarkdown renders assistant answers to a ydraw drawable list in-process;
# yai frames it as a YDRAW_BIN envelope itself (yface + ydraw-core come
# transitively via yetty_ygui), so markdown no longer shells out to ycat.
# yetty_yplot renders the /usage tokens-vs-time curve the same in-process way.
target_link_libraries(yai PRIVATE yetty_ygui yetty_yclass yetty_yfont_shader_glyph
                                  yetty_ymarkdown yetty_yplot yyjson::yyjson uv_a Threads::Threads
                                  msgpack-c yaml)

# proxy.c forwards the child's Anthropic traffic over HTTPS with libcurl to
# capture live rate-limit/quota headers.
target_link_libraries(yai PRIVATE CURL::libcurl)
# libcurl.a needs Brotli symbols for `Content-Encoding: br`, but Brotli is
# also a transitive dep of freetype (woff2); CMake collapses it to its first
# (early) occurrence, so re-list it after libcurl on the link line.
if(TARGET brotlidec)
    target_link_libraries(yai PRIVATE brotlidec brotlicommon)
endif()

# The engine classes carry `[[clang::annotate(...)]]` (C23 attribute
# syntax); see ygrid/CMakeLists.txt for the rationale.
if(NOT MSVC)
    set_target_properties(yai PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
    target_compile_options(yai PRIVATE -Wall -Wextra)
    # Vendored third-party code: don't hold it to our warning flags.
    set_source_files_properties(picohttpparser/picohttpparser.c PROPERTIES COMPILE_OPTIONS "-w")
    set_source_files_properties(fzy/match.c PROPERTIES COMPILE_OPTIONS "-w")
endif()
