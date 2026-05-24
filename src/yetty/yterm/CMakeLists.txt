add_library(yetty_yterm
    terminal.c
    text-layer.c
    ydraw-layer.c
    shader-glyph-layer.c
    background-layer.c
    osc-args.c
)

target_include_directories(yetty_yterm BEFORE PUBLIC
    # Pinned webgpu.h shared with yetty_yrdawn — server-dispatch.gen.c
    # and the surrounding layer code must see the SAME struct layouts
    # the client encodes from. See src/yetty/yrdawn/CMakeLists.txt for
    # the full rationale.
    ${YETTY_ROOT}/include/yetty/yrdawn
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
    yetty_yfigure
    yetty_ygrid
    lz4_static
)

# The yrdawn server (figure + session + codegen dispatch) is linked
# wherever a host wants to mint yrdawn figures from a registry. On
# webasm we don't host canvases, so the server lib doesn't exist.
if(TARGET yetty_yrdawn_server)
    target_link_libraries(yetty_yterm yetty_yrdawn_server)
endif()

# yterm's sources reference only header-only constants from
# <yetty/ymgui/wire.h> (YMGUI_WIRE_VERSION) — no compiled symbols. Pull
# yetty_ymgui in transitively only when the feature is built, so webasm
# (FEATURE_YMGUI=OFF) doesn't request -lyetty_ymgui at link time.
if(YETTY_ENABLE_FEATURE_YMGUI)
    target_link_libraries(yetty_yterm yetty_ymgui)
endif()

# Embed shaders via incbin (or stubs on Emscripten)
incbin_add_resources(yetty_yterm
    text_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/text-layer.wgsl"
    ydraw_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/ydraw-layer.wgsl"
    shader_glyph_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/shader-glyph-layer.wgsl"
    background_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/background-layer.wgsl"
)
