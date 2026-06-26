# yterminal — the coordination layer. Owns the PTY, the wire statemachine,
# mints the figures (the yvterm content layer, the root container of figures),
# and sets up wire-statemachine dispatch. The pane background fill layer lives
# here too. Symbol prefix: yetty_yterminal_.
add_library(yetty_yterminal
    terminal.c
    background-layer.c
)

target_include_directories(yetty_yterminal BEFORE PUBLIC
    # Pinned webgpu.h shared with yetty_yrdawn (matches yetty_yvterm).
    ${YETTY_ROOT}/include/yetty/yrdawn
)
target_include_directories(yetty_yterminal PUBLIC
    ${YETTY_ROOT}/include
)
target_include_directories(yetty_yterminal PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yterminal
    # The text-grid figure the coordinator drives. PUBLIC so consumers (yui)
    # see yvterm's headers/symbols transitively.
    yetty_yvterm
    vterm
    webgpu
    yetty_yclass
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

if(TARGET yetty_yrdawn_server)
    target_link_libraries(yetty_yterminal yetty_yrdawn_server)
endif()

if(YETTY_ENABLE_FEATURE_YMGUI)
    target_link_libraries(yetty_yterminal yetty_ymgui)
endif()

# ymesh composite factory — terminal.c registers it with composite_factory so
# inbound YDRAW_BIN ymesh records (type 0x80000005) decode + render. The GPU
# factory symbol (yetty_ymesh_factory_create) only exists when both the ymesh
# feature and WebGPU are enabled; gate the macro + link on exactly that.
if(YETTY_ENABLE_FEATURE_YMESH AND YETTY_ENABLE_LIB_WEBGPU)
    target_compile_definitions(yetty_yterminal PRIVATE YETTY_HAS_YMESH=1)
    target_link_libraries(yetty_yterminal yetty_ymesh)
endif()

# yvideo composite factory — terminal.c registers it so inbound YDRAW_BIN
# yvideo records (type 0x80000006) decode + render. The factory symbol
# (yetty_yvideo_factory_create) exists only when the yvideo feature, OpenH264,
# and WebGPU are all enabled; gate the macro + link on exactly that. (The
# always-built yshadertoy prim factory needs no gate — like yplot/yimage it
# resolves transitively through the final executable's link.)
if(YETTY_ENABLE_FEATURE_YVIDEO AND YETTY_ENABLE_LIB_OPENH264 AND YETTY_ENABLE_LIB_WEBGPU)
    target_compile_definitions(yetty_yterminal PRIVATE YETTY_HAS_YVIDEO=1)
    target_link_libraries(yetty_yterminal yetty_yvideo)
endif()

# Pane background fill shader.
incbin_add_resources(yetty_yterminal
    background_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/background-layer.wgsl"
)
