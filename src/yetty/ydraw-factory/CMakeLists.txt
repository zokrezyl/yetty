# yetty_ydraw_factory — abstract-factory registry for composites.
# Server-only (links Dawn via yrender). Wrapping in YETTY_ENABLE_LIB_WEBGPU
# means riscv64 / no-GPU builds skip it entirely; client code uses only
# yetty_ydraw_core (wire format) and never touches the factory pattern.
if(YETTY_ENABLE_LIB_WEBGPU)
    add_library(yetty_ydraw_factory STATIC
        composite-factory.c
    )

    target_include_directories(yetty_ydraw_factory PUBLIC
        ${YETTY_ROOT}/include
        ${YETTY_ROOT}/src
    )

    target_link_libraries(yetty_ydraw_factory PUBLIC
        yetty_ycore
        yetty_ydraw_core
        yetty_yrender
    )
endif()
