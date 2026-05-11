# yetty_ypaint_factory — abstract-factory registry for complex primitives.
# Server-only (links Dawn via yrender). Wrapping in YETTY_ENABLE_LIB_WEBGPU
# means riscv64 / no-GPU builds skip it entirely; client code uses only
# yetty_ypaint_core (wire format) and never touches the factory pattern.
if(YETTY_ENABLE_LIB_WEBGPU)
    add_library(yetty_ypaint_factory STATIC
        complex-prim-factory.c
    )

    target_include_directories(yetty_ypaint_factory PUBLIC
        ${YETTY_ROOT}/include
        ${YETTY_ROOT}/src
    )

    target_link_libraries(yetty_ypaint_factory PUBLIC
        yetty_ycore
        yetty_ypaint_core
        yetty_yrender
    )
endif()
