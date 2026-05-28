add_library(yetty_yfigure STATIC
    container.c
    figure.c
    registry.c
    methods.gen.c
    rpc.gen.c
)

target_include_directories(yetty_yfigure PUBLIC
    ${YETTY_ROOT}/include
)
# Internal codegen headers (methods.gen.h) sit under src/yetty/yfigure/.
# PRIVATE so the path does not leak to consumers — they go through
# the public include/yetty/yfigure/methods.h.
target_include_directories(yetty_yfigure PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yfigure PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_ywire
)
