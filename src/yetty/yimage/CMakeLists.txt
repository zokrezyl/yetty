# yetty_yimage_core — wire-format / image decode (stb_image) / OSC emit.
# GPU-less. Used by client tools (ycat, ylexbor, demos) and cross-targets.
add_library(yetty_yimage_core STATIC
    yimage.c
    yimage-gen-wire.c
)

target_include_directories(yetty_yimage_core PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_yimage_core PUBLIC
    stb
    yetty_ydraw_core
    yetty_yface
)

# yetty_yimage — GPU pipeline (concrete factory). Pulls Dawn / WebGPU.
if(YETTY_ENABLE_LIB_WEBGPU)
    add_library(yetty_yimage STATIC
        yimage-gen.c
    )

    target_include_directories(yetty_yimage PUBLIC
        ${YETTY_ROOT}/include
        ${YETTY_ROOT}/src
    )

    target_link_libraries(yetty_yimage PUBLIC
        yetty_yimage_core
        yetty_ydraw_factory
        yetty_yrender
    )

    # Embed shaders via incbin
    incbin_add_resources(yetty_yimage
        yimage_shader "${CMAKE_CURRENT_SOURCE_DIR}/yimage.wgsl"
        yimage_lib_shader "${CMAKE_CURRENT_SOURCE_DIR}/yimage-gen.wgsl"
    )
endif()
