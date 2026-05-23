# osc-analyzer — diagnostic CLI for the NEW figure-tree OSC wire format.
# Decodes envelopes, walks {length, id} records, and dumps admin /
# routed payloads (ymgui FRAME/TEX, ygrid SDF/glyph streams, nested
# container record streams). Use the capture mode on a saved log, or
# interpose mode to tee a live stream into a decode log while the wire
# passes through unchanged.

add_executable(osc-analyzer
    main.c
    # ytrace stubs used by libraries we pull in.
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(osc-analyzer PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(osc-analyzer PRIVATE
    yetty_yface
    yetty_ycore
    yetty_ydraw_core
    yetty_yplatform_core
    lz4_static
)

if(NOT MSVC)
    target_compile_options(osc-analyzer PRIVATE -Wall -Wextra)
endif()
