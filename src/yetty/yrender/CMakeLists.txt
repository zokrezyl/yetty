set(YETTY_YRENDER_SOURCES
    types.c
    gpu-allocator.c
    pipeline.c
    gpu-resource-binder.c
    render-target-texture.c
)

if(YETTY_ENABLE_FEATURE_YVNC)
    list(APPEND YETTY_YRENDER_SOURCES render-target-vnc.c)
endif()

# X11-tile render target is only useful on Linux/BSD with X11 + XShm.
# Compiles out on macOS / Windows / Android / iOS / Emscripten.
set(YETTY_YRENDER_X11_TILE OFF)
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN AND NOT YETTY_ANDROID AND NOT YETTY_IOS)
    include(${YETTY_ROOT}/build-tools/cmake/x11-static.cmake)
    if(YETTY_X11_STATIC_LIBS)
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
    target_include_directories(yetty_yrender PUBLIC ${YETTY_X11_STATIC_INCLUDE_DIR})
    target_link_libraries(yetty_yrender PUBLIC
        webgpu
        yetty_yrender_utils
        ${YETTY_X11_STATIC_LIBS}
    )
else()
    target_link_libraries(yetty_yrender PUBLIC
        webgpu
    )
endif()

# pipeline.c (added on yrender-binder-refactor) and gpu-resource-binder.c
# call into yetty_ywebgpu_error_*. Express the dep on the yrender target so
# every transitive consumer (tools, demos, tests) picks up yetty_ywebgpu in
# the right link order — main's link graph wired this implicitly via
# higher-level libs that already pulled in yetty_ywebgpu.
target_link_libraries(yetty_yrender PUBLIC yetty_ywebgpu)

# Embed shaders via incbin (or stubs on Emscripten)
incbin_add_resources(yetty_yrender
    blend_shader "${CMAKE_CURRENT_SOURCE_DIR}/blend.wgsl"
)
