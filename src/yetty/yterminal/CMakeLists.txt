# yterminal object API (role-split codegen): the terminal is the yclass
# class `yterminal:terminal` — the session-root object a connecting tool
# receives, with the object-returning figure_root_container navigation slot.
add_library(yetty_api_yterminal STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/yterminal/terminal.c
)
target_include_directories(yetty_api_yterminal PUBLIC ${YETTY_ROOT}/include)
target_link_libraries(yetty_api_yterminal PUBLIC yetty_ycore yetty_yclass)

# yterminal — the coordination layer. Owns the PTY, the wire statemachine,
# mints the figures (the yvterm content layer, the root container of figures),
# and sets up wire-statemachine dispatch. The pane background fill layer lives
# here too. Symbol prefix: yetty_yterminal_.
add_library(yetty_yterminal
    terminal.c
    terminal-mime.c
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
    yetty_api_yterminal
    # The terminal IS-A ytermsink:sink (parent) — it overrides the five upcalls
    # (pty_write / request_render / mouse_sub / clipboard_write / sixel_write).
    yetty_ytermsink
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
    yetty_ymime
    yetty_ywire
    yetty_yrdawn
    yetty_yfigure
    yetty_ygrid
    yetty_yscene
    lz4_static
)

# Terminal-side file renderers for YETTY_DCS_MIME_FILE (terminal-mime.c).
# Each renderer is optional; the handler discards types whose renderer is
# not compiled in. Same gating style as the ymesh/yvideo factories below.
if(YETTY_ENABLE_FEATURE_YSVG)
    target_compile_definitions(yetty_yterminal PRIVATE YETTY_HAS_YSVG=1)
    target_link_libraries(yetty_yterminal yetty_ysvg)
endif()

if(YETTY_ENABLE_FEATURE_YMARKDOWN)
    target_compile_definitions(yetty_yterminal PRIVATE YETTY_HAS_YMARKDOWN=1)
    target_link_libraries(yetty_yterminal yetty_ymarkdown)
endif()

if(YETTY_ENABLE_FEATURE_YPDF)
    target_compile_definitions(yetty_yterminal PRIVATE YETTY_HAS_YPDF=1)
    target_link_libraries(yetty_yterminal yetty_ypdf)
endif()

if(YETTY_ENABLE_FEATURE_YIMAGE)
    target_compile_definitions(yetty_yterminal PRIVATE YETTY_HAS_YIMAGE=1)
    target_link_libraries(yetty_yterminal yetty_yimage_core)
endif()

if(YETTY_ENABLE_FEATURE_YSIXEL AND YETTY_ENABLE_FEATURE_YIMAGE)
    target_compile_definitions(yetty_yterminal PRIVATE YETTY_HAS_YSIXEL=1)
    target_link_libraries(yetty_yterminal yetty_ysixel_core)
endif()

# Same gates the ymusic / ycircuit libraries themselves use
# (src/yetty/CMakeLists.txt).
if(YETTY_ENABLE_FEATURE_YMUSIC AND YETTY_ENABLE_FEATURE_YDRAW AND YETTY_ENABLE_FEATURE_YFACE)
    target_compile_definitions(yetty_yterminal PRIVATE YETTY_HAS_YMUSIC=1)
    target_link_libraries(yetty_yterminal yetty_ymusic)
endif()

if(YETTY_ENABLE_FEATURE_YCIRCUIT AND YETTY_ENABLE_FEATURE_YDRAW AND YETTY_ENABLE_FEATURE_YFACE)
    target_compile_definitions(yetty_yterminal PRIVATE YETTY_HAS_YCIRCUIT=1)
    target_link_libraries(yetty_yterminal yetty_ycircuit)
endif()

if(YETTY_ENABLE_FEATURE_YMSOFFICE)
    target_compile_definitions(yetty_yterminal PRIVATE YETTY_HAS_YMSOFFICE=1)
    target_link_libraries(yetty_yterminal yetty_ymsoffice)
endif()

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
