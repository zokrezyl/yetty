# osc-analyzer — figure-tree OSC wire decoder.
#
# Drives yetty's own wire state machine in a coroutine, so envelopes
# stream through immediately (no splitter, no buffer-until-complete).
# Three entry points: forkpty (-e), stdin passthrough (--interpose),
# and replay from a file. See main.c for the protocol details.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

add_executable(osc-analyzer
    main.c
    # Coroutine support — the wire state machine spawns its scanner as
    # a coroutine and yields between PTY pulls, so the tool has to ship
    # the platform's coro implementation. Same approach ycompositor-ygui
    # uses.
    ${YETTY_ROOT}/src/yetty/yplatform/coroutine/default.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(osc-analyzer PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(osc-analyzer PRIVATE
    yetty_ywire
    yetty_yface
    # ydraw-core for the flyweight registry + drawable_command_parse +
    # text_span / font drawable parsers; ysdf for compute_aabb + type
    # tables used to decode SDF prims into YAML.
    yetty_ydraw_core
    yetty_ysdf
    yetty_ycore
    yetty_yplatform_core
    yetty_yco
    # <yetty/yterm/terminal.h> reaches webgpu/webgpu.h through
    # <yetty/yetty/yetty.h>; linking webgpu brings the include path.
    webgpu
    lz4_static
    util
)

if(NOT MSVC)
    target_compile_options(osc-analyzer PRIVATE -Wall -Wextra)
endif()
