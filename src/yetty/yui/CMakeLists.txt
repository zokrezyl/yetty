add_library(yetty_yui STATIC
    tile.c
    workspace.c
    tabbar.c
)

# stdatomic.h requires C11
set_target_properties(yetty_yui PROPERTIES C_STANDARD 11)

target_include_directories(yetty_yui
    PUBLIC
        ${YETTY_ROOT}/src
    PRIVATE
        ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yui
    PRIVATE
        webgpu
        yetty_yui_core
        yetty_yterm
)
