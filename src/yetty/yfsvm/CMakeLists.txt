# yetty_yfsvm_core — pure-CPU yexpr-AST → bytecode compiler. GPU-less.
add_library(yetty_yfsvm_core STATIC
    compiler.c
)

target_include_directories(yetty_yfsvm_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(yetty_yfsvm_core PUBLIC
    yetty_yexpr
)

# yetty_yfsvm — server-side glue: builds a static gpu_resource_set wrapping
# the embedded yfsvm WGSL. Pulls yrender (and via it, Dawn / WebGPU).
if(YETTY_ENABLE_LIB_WEBGPU)
    add_library(yetty_yfsvm STATIC
        shader-rs.c
    )

    target_include_directories(yetty_yfsvm PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    )

    target_link_libraries(yetty_yfsvm PUBLIC
        yetty_yfsvm_core
        yetty_yrender
    )

    # Embed shader via incbin
    incbin_add_resources(yetty_yfsvm
        yfsvm_shader "${CMAKE_CURRENT_SOURCE_DIR}/yfsvm.gen.wgsl"
    )
endif()
