include(${YETTY_ROOT}/build-tools/yetty/incbin.cmake)

add_library(yetty_yui STATIC
    tile.c
    workspace.c
    tabbar.c
    tabbar-paint.c
)

# stdatomic.h requires C11
set_target_properties(yetty_yui PROPERTIES C_STANDARD 11)

target_include_directories(yetty_yui
    PUBLIC
        ${YETTY_ROOT}/src
    PRIVATE
        ${YETTY_ROOT}/include
        ${INCBIN_INCLUDE_DIR}
)

target_link_libraries(yetty_yui
    PRIVATE
        webgpu
        yetty_yui_core
        yetty_yterm
)

# Embed the tabbar shader so the GPU pipeline can pick it up at runtime
# without a separate file dependency.
incbin_add_resources(yetty_yui
    tabbar_shader "${CMAKE_CURRENT_SOURCE_DIR}/tabbar.wgsl"
)
