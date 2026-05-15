# yecho - text/glyph/styled-block parser, renders to a ydraw-core buffer.
# Strictly client-side; never linked into the yetty process.

add_library(yetty_yecho STATIC
    yecho.c
)

target_include_directories(yetty_yecho PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yecho PUBLIC
    yetty_ycore
    yetty_ydraw_core
    yetty_ysdf
    yetty_yfont_shader_glyph
    yetty_yface
    # yplot_core brings yfsvm + yexpr transitively for {plot: ...} blocks.
    # No GPU / WebGPU pull-in — yecho is strictly client-side.
    yetty_yplot_core
)
