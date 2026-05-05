add_library(yetty_ymesh STATIC
    ymesh.c
    ymesh-gen.c
    ymesh-glb.c
)

target_include_directories(yetty_ymesh PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ymesh PUBLIC
    cgltf
    yetty_ypaint_core
    yetty_yrender
    yetty_yface
)

# Embed shaders via incbin
incbin_add_resources(yetty_ymesh
    ymesh_3d_shader   "${CMAKE_CURRENT_SOURCE_DIR}/ymesh.wgsl"
    ymesh_blit_shader "${CMAKE_CURRENT_SOURCE_DIR}/ymesh-blit.wgsl"
)
