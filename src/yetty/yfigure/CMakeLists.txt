add_library(yetty_yfigure STATIC
    group.c
)

target_include_directories(yetty_yfigure PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yfigure PUBLIC
    yetty_ycore
)
