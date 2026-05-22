add_library(yetty_ygrid STATIC
    grid.c
)

target_include_directories(yetty_ygrid PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_ygrid PUBLIC
    yetty_ycore
    yetty_yfigure
    yetty_ydraw_core
    yetty_yrender
    webgpu
)
