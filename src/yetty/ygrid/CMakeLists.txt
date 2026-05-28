include(${YETTY_ROOT}/build-tools/yetty/yclass-codegen.cmake)

yetty_yclass_codegen(
    MODULE   ygrid
    SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/grid.c
    CLASSES  grid
    GEN_SRC  YGRID_GEN_SRC
)

add_library(yetty_ygrid STATIC
    grid.c
    ${YGRID_GEN_SRC}
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
