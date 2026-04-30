# yrender-utils — rendering helper components reusable across render targets
# and output sinks (VNC wire protocol, X11 tile blitter, headless capture, …).

add_library(yetty_yrender_utils STATIC
    tile-diff.c
)

target_include_directories(yetty_yrender_utils PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yrender_utils PUBLIC
    webgpu
)
