add_library(yetty_yimage STATIC
    yimage.c
    yimage-gen.c
)

target_include_directories(yetty_yimage PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_yimage PUBLIC
    stb
    yetty_ypaint_core
    yetty_yrender
    yetty_yface
)

# Embed shaders via incbin
incbin_add_resources(yetty_yimage
    yimage_shader "${CMAKE_CURRENT_SOURCE_DIR}/yimage.wgsl"
    yimage_lib_shader "${CMAKE_CURRENT_SOURCE_DIR}/yimage-gen.wgsl"
)
