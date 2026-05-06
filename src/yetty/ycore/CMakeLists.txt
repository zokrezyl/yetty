# yetty core (C)
add_library(yetty_ycore STATIC
    result.c
    types.c
    util.c
    tcp-transport.c
)

target_include_directories(yetty_ycore PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
    ${DAWN_INCLUDE_DIR}
)

target_link_libraries(yetty_ycore PUBLIC
    webgpu
)
