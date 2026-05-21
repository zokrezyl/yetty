add_library(yetty_yterm
    terminal.c
    text-layer.c
    ydraw-layer.c
    ymgui-layer.c
    yrdawn-layer.c
    ${YETTY_ROOT}/src/yetty/yrdawn/server-dispatch.gen.c
    shader-glyph-layer.c
    background-layer.c
    osc-args.c
)

target_include_directories(yetty_yterm PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yterm
    vterm
    webgpu
    yetty_ydraw_core
    yetty_ydraw_factory
    yetty_yfont_core
    yetty_yui_core
    yetty_yface
    yetty_ywire
    yetty_yrdawn
    lz4_static
)

# Embed shaders via incbin (or stubs on Emscripten)
incbin_add_resources(yetty_yterm
    text_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/text-layer.wgsl"
    ydraw_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/ydraw-layer.wgsl"
    ymgui_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/ymgui-layer.wgsl"
    shader_glyph_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/shader-glyph-layer.wgsl"
    background_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/background-layer.wgsl"
)
