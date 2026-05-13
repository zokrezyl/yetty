add_library(yetty_yterm
    terminal.c
    text-layer.c
    ypaint-layer.c
    ymgui-layer.c
    shader-glyph-layer.c
    background-layer.c
    pty-reader.c
    osc-args.c
    osc-statemachine.c
)

target_include_directories(yetty_yterm PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yterm
    vterm
    webgpu
    yetty_ypaint_core
    yetty_ypaint_factory
    yetty_yfont_core
    yetty_yui_core
    yetty_yface
    lz4_static
)

# Embed shaders via incbin (or stubs on Emscripten)
incbin_add_resources(yetty_yterm
    text_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/text-layer.wgsl"
    ypaint_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/ypaint-layer.wgsl"
    ymgui_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/ymgui-layer.wgsl"
    shader_glyph_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/shader-glyph-layer.wgsl"
    background_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/background-layer.wgsl"
)
