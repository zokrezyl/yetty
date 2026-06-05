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

# Pane background fill shader.
incbin_add_resources(yetty_yterminal
    background_layer_shader "${CMAKE_CURRENT_SOURCE_DIR}/background-layer.wgsl"
)
