add_library(ypaint_core STATIC
    ypaint-buffer.c
    ypaint-canvas.c
)

target_include_directories(ypaint_core PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(ypaint_core PUBLIC ypaint_sdf)
