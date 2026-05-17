add_library(yetty_ywire STATIC
    wire-statemachine.c
)

target_include_directories(yetty_ywire PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_ywire
    yetty_ycore
    lz4_static
)
