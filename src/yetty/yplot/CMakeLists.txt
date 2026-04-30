add_library(yetty_yplot STATIC
    yplot.c
    yplot-gen.c
    yplot-gen-yaml.c
)

target_include_directories(yetty_yplot PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_yplot PUBLIC
    yaml
    yetty_ypaint_core
    yetty_yrender
    yetty_yfsvm
    yetty_yface
)

# Embed shaders via incbin
incbin_add_resources(yetty_yplot
    yplot_shader "${CMAKE_CURRENT_SOURCE_DIR}/yplot.wgsl"
    yplot_lib_shader "${CMAKE_CURRENT_SOURCE_DIR}/yplot-gen.wgsl"
)
