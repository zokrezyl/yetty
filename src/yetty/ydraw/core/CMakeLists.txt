add_library(ydraw_core STATIC
    ydraw-buffer.c
    ydraw-canvas.c
)

target_include_directories(ydraw_core PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(ydraw_core PUBLIC ydraw_sdf)
