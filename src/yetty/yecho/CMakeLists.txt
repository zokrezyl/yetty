# yecho - text/glyph/styled-block parser, renders to a ypaint-core buffer.
# Strictly client-side; never linked into the yetty process.

add_library(yetty_yecho STATIC
    yecho.c
)

target_include_directories(yetty_yecho PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yecho PUBLIC
    yetty_ycore
    yetty_ypaint_core
    yetty_ysdf
    yetty_yfont_shader_glyph
    yetty_yface
    # yplot brings yfsvm + yexpr + yrender transitively; needed for
    # {plot: ...} blocks. yrender drags GPU-side refs (yetty_ywebgpu_error_*)
    # that we don't actually call but the linker still wants resolved.
    yetty_yplot
    yetty_ywebgpu
)
