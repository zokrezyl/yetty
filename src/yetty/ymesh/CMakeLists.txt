# yetty_ymesh_core — wire-format / mesh decode (GLB via cgltf, PLY/STL/OBJ
# via ymesh-load.c) / OSC emit. GPU-less.
add_library(yetty_ymesh_core STATIC
    ymesh.c
    ymesh-glb.c
    ymesh-load.c
)

target_include_directories(yetty_ymesh_core PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ymesh_core PUBLIC
    cgltf
    yetty_ydraw_core
    yetty_yface
)

# libm for sqrtf in ymesh-load.c (no-op on Windows / macOS where math is
# in the C runtime; the stricter riscv64 cross-linker needs it explicit).
if(NOT WIN32)
    target_link_libraries(yetty_ymesh_core PUBLIC m)
endif()

# yetty_ymesh — GPU pipeline (concrete factory). Pulls Dawn / WebGPU.
if(YETTY_ENABLE_LIB_WEBGPU)
    add_library(yetty_ymesh STATIC
        ymesh-gen.c
    )

    target_include_directories(yetty_ymesh PUBLIC
        ${YETTY_ROOT}/include
        ${YETTY_ROOT}/src
    )

    target_link_libraries(yetty_ymesh PUBLIC
        yetty_ymesh_core
        yetty_ydraw_factory
        yetty_yrender
    )

    # Embed shaders via incbin
    incbin_add_resources(yetty_ymesh
        ymesh_3d_shader   "${CMAKE_CURRENT_SOURCE_DIR}/ymesh.wgsl"
        ymesh_blit_shader "${CMAKE_CURRENT_SOURCE_DIR}/ymesh-blit.wgsl"
    )
endif()
