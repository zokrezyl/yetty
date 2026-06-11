# yvterm — the text-grid figure: libvterm text layer, scrollback, the ydraw
# rich-content canvas, the content layer that composites them, and the
# shader-glyph figure. Owned/driven by yterminal (the coordinator). Symbol
# prefix: yetty_yvterm_.
add_library(yetty_yvterm
    content-layer.c
    grid.c
    text-layer.c
    text-scrollback.c
    ydraw-content.c
    shader-glyph-figure.c
    osc-args.c
    methods.gen.c
    rpc.gen.c
)

target_include_directories(yetty_yvterm BEFORE PUBLIC
    # Pinned webgpu.h shared with yetty_yrdawn — server-dispatch.gen.c
    # and the surrounding layer code must see the SAME struct layouts
    # the client encodes from. See src/yetty/yrdawn/CMakeLists.txt for
    # the full rationale.
    ${YETTY_ROOT}/include/yetty/yrdawn
)
target_include_directories(yetty_yvterm PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) sit under src/.
target_include_directories(yetty_yvterm PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yvterm
    vterm
    webgpu
    yetty_yclass
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

# shader-glyph-figure.c carries `[[clang::annotate(...)]]` on its data
# struct (codegen reads it off the AST). The C2x attribute syntax needs
# C23 to parse — host clang/gcc accept it by default, NDK clang 17 does
# not. Off-MSVC only: MSVC can't express C23 via CMake's C_STANDARD and
# parses the attributes via the global /std:clatest. (Mirrors ygui.)
if(NOT MSVC)
    set_target_properties(yetty_yvterm PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()

# The yrdawn server (figure + session + codegen dispatch) is linked
# wherever a host wants to mint yrdawn figures from a registry. On
# webasm we don't host canvases, so the server lib doesn't exist.
if(TARGET yetty_yrdawn_server)
    target_link_libraries(yetty_yvterm yetty_yrdawn_server)
endif()

# yvterm's sources reference only header-only constants from
# <yetty/ymgui/wire.h> (YMGUI_WIRE_VERSION) — no compiled symbols. Pull
# yetty_ymgui in transitively only when the feature is built, so webasm
# (FEATURE_YMGUI=OFF) doesn't request -lyetty_ymgui at link time.
if(YETTY_ENABLE_FEATURE_YMGUI)
    target_link_libraries(yetty_yvterm yetty_ymgui)
endif()

# Embed shaders via incbin (or stubs on Emscripten)
incbin_add_resources(yetty_yvterm
    text_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/text-layer.wgsl"
    ydraw_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/ydraw-layer.wgsl"
    shader_glyph_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/shader-glyph-layer.wgsl"
)
