# ymux — the tmux equivalent for yetty (#695). ONE binary, tmux model:
# the client forks the server from itself on first use; commands and
# flags follow tmux's grammar. The session library lives in
# src/yetty/ymux; this is the CLI + in-binary server frontend.

# forkpty-based server half: no Windows / webasm build.
if(WIN32 OR EMSCRIPTEN)
    return()
endif()

add_executable(ymux
    main.c
    ${YETTY_ROOT}/src/yetty/ypty/forkpty.c
    # No yetty_ytrace library target exists — the trace TU is compiled
    # into each executable (same as the test-harness trace shim).
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ymux PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
# The attach bridge is a PURE RPC CLIENT toward the hosting yetty: it
# links only the generated api stubs (figure/scene/terminal navigation)
# + the yclass runtime — no impls, no GPU (the ydummy-client model).
target_link_libraries(ymux PRIVATE
    yetty_ymux
    yetty_api_ymux
    yetty_api_yfigure
    yetty_api_yscene
    yetty_api_yterminal
    yetty_yclass
    yetty_yface
    yetty_ydraw_core
    yetty_ysdf
    Threads::Threads
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # forkpty() lives in libutil on glibc.
    target_link_libraries(ymux PRIVATE util)
endif()

if(NOT MSVC)
    target_compile_options(ymux PRIVATE -Wall -Wextra)
endif()
