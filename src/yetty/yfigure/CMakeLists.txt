include(${YETTY_ROOT}/build-tools/yetty/yclass-codegen.cmake)

yetty_yclass_codegen(
    MODULE   yfigure
    SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/figure.c
             ${CMAKE_CURRENT_SOURCE_DIR}/container.c
    CLASSES  figure container
    GEN_SRC  YFIGURE_GEN_SRC
)

add_library(yetty_yfigure STATIC
    container.c
    figure.c
    registry.c
    ${YFIGURE_GEN_SRC}
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
