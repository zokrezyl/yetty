add_library(yetty_ygrid STATIC
    grid.c
    methods.gen.c
    rpc.gen.c
)

target_include_directories(yetty_ygrid PUBLIC
    ${YETTY_ROOT}/include
)
# Internal codegen headers (methods.gen.h) sit under src/yetty/ygrid/.
# PRIVATE so the path does not leak to consumers — they go through
# the public include/yetty/ygrid/methods.h.
target_include_directories(yetty_ygrid PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_ygrid PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_yfigure
    yetty_ydraw_core
    yetty_yrender
    webgpu
)
