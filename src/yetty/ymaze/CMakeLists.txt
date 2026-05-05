add_library(yetty_ymaze STATIC
    ymaze.c
)

target_include_directories(yetty_ymaze PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ymaze PUBLIC
    yetty_ypaint_core
    yetty_ysdf
)
