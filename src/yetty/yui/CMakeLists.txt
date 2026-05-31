add_library(yetty_yui STATIC
    tile.c
    workspace.c
    tabbar.c
    config-dialog.c
    debug-window.c
    yui.c
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
        yetty_yfigure
        yetty_ygrid
        yetty_yshadertoy
        yetty_yui_core
        yetty_yterm
        ygui
)

if(YETTY_ENABLE_FEATURE_YMGUI)
    target_link_libraries(yetty_yui PRIVATE yetty_ymgui)
endif()
