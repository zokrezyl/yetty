set(_yrdawn_server_src ${YETTY_ROOT}/src/yetty/yrdawn/server-dispatch.gen.c)
# The codegen-emitted dispatcher calls every Dawn entrypoint in our
# committed webgpu.h. Some of them (wgpu*SetResourceTable,
# wgpuSharedFenceSetLabel, …) aren't exported by the Android / iOS /
# tvOS Dawn prebuilts, so linking the full dispatcher on those targets
# fails. The bridge server only makes sense where a desktop yetty
# hosts remote demos — mobile yetty doesn't have that workflow. Swap
# in a stub TU that answers every CMD with UNKNOWN_METHOD; the layer
# itself (yrdawn-layer.c) still registers OSC handlers and routes
# HELLO/CMD/BYE, so the wire stays alive and the absence is visible
# to the client. Linux / macOS / Windows Dawn prebuilts export the
# full surface so they keep the real dispatcher.
#
# Emscripten is also routed to the stub: a webasm yetty is the *client*
# end of the bridge (it's the thing remoted INTO from a desktop yetty),
# so it never hosts the server. Compiling the real dispatcher under
# emcc additionally hits incompatible-pointer-types hard errors against
# emscripten's stricter clang — sidestepping that by stubbing is cleaner
# than disabling the diagnostic on dead code.
if(ANDROID OR YETTY_ANDROID OR YETTY_IOS OR YETTY_TVOS OR EMSCRIPTEN)
    set(_yrdawn_server_src ${YETTY_ROOT}/src/yetty/yrdawn/server-dispatch-stub.c)
endif()

add_library(yetty_yterm
    terminal.c
    text-layer.c
    ydraw-layer.c
    ymgui-layer.c
    yrdawn-layer.c
    ${_yrdawn_server_src}
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
