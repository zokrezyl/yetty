set(YETTY_YRENDER_SOURCES
    types.c
    gpu-allocator.c
    pipeline.c
    gpu-resource-binder.c
    render-target-texture.c
    render-target-vnc.c
)

# X11-tile render target is only useful on Linux/BSD with X11 + XShm.
# Compiles out on macOS / Windows / Android / iOS / Emscripten.
set(YETTY_YRENDER_X11_TILE OFF)
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN AND NOT YETTY_ANDROID AND NOT YETTY_IOS)
    find_package(X11 QUIET COMPONENTS Xext)
    if(X11_FOUND AND X11_Xext_FOUND)
        list(APPEND YETTY_YRENDER_SOURCES render-target-x11-tile.c)
        set(YETTY_YRENDER_X11_TILE ON)
    endif()
endif()

add_library(yetty_yrender ${YETTY_YRENDER_SOURCES})

target_include_directories(yetty_yrender PUBLIC
    ${YETTY_ROOT}/include
    ${INCBIN_INCLUDE_DIR}
)

target_compile_definitions(yetty_yrender PRIVATE
    BLEND_SHADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/blend.wgsl"
)

if(YETTY_YRENDER_X11_TILE)
    target_compile_definitions(yetty_yrender PUBLIC YETTY_HAS_X11_TILE=1)
    # PUBLIC so consumers (e.g. glfw-main.c's native-X11 bridge) pick up
    # X11 headers transitively via linkage.
    target_link_libraries(yetty_yrender PUBLIC
        webgpu
        yetty_webgpu
        yetty_yrender_utils
        X11::X11
        X11::Xext
    )
else()
    target_link_libraries(yetty_yrender
        webgpu
        yetty_webgpu
    )
endif()

# Embed shaders via incbin (or stubs on Emscripten)
incbin_add_resources(yetty_yrender
    blend_shader "${CMAKE_CURRENT_SOURCE_DIR}/blend.wgsl"
)
