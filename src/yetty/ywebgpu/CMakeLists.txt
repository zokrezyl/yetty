add_library(yetty_ywebgpu
    error.c
    utils.c
    limits.c
    request.c
)

target_include_directories(yetty_ywebgpu PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_ywebgpu
    webgpu
)
