# yetty_yplot_core — wire-format / YAML / OSC emit. GPU-less.
# Used by client tools (yecho, yplot CLI, ycat, demos) and cross-targets
# without WebGPU (riscv64).
add_library(yetty_yplot_core STATIC
    yplot.c
    yplot-gen-wire.c
    yplot-yaml.c
)

target_include_directories(yetty_yplot_core PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_yplot_core PUBLIC
    yaml
    yetty_ydraw_core
    yetty_yfsvm_core
    yetty_yface
)

# yetty_yplot — GPU pipeline (concrete factory). Pulls Dawn / WebGPU.
# Built only when WebGPU is on; linked by the yetty server executable.
if(YETTY_ENABLE_LIB_WEBGPU)
    # yplot-time.c hooks the figure into the shared animation timer
    # when the compiled bytecode references `t`/`time`. Lives next to
    # yplot-gen.c because both need Dawn + the composite_factory headers.
    add_library(yetty_yplot STATIC
        yplot-gen.c
        yplot-time.c
        yplot-update.c
    )

    target_include_directories(yetty_yplot PUBLIC
        ${YETTY_ROOT}/include
        ${YETTY_ROOT}/src
    )

    target_link_libraries(yetty_yplot PUBLIC
        yetty_yplot_core
        yetty_yfsvm
        yetty_ydraw_factory
        yetty_yrender
    )

    # Embed shaders via incbin
    incbin_add_resources(yetty_yplot
        yplot_shader "${CMAKE_CURRENT_SOURCE_DIR}/yplot.wgsl"
        yplot_lib_shader "${CMAKE_CURRENT_SOURCE_DIR}/yplot-gen.wgsl"
    )
endif()
