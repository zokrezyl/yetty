add_library(yetty_ycompositor STATIC
    compositor.c
)

target_include_directories(yetty_ycompositor PUBLIC
    ${YETTY_ROOT}/include
)

# yfigure for the base figure type; ycore for result/types. The
# compositor declares itself a yterm terminal_layer (via
# <yetty/yterm/terminal.h>) but does NOT link against yterm —
# terminal.h is a pure-interface header (no impl symbols). The
# yterm <-> ycompositor link goes in yterm's CMakeLists, where
# terminal.c calls yetty_ycompositor_layer_create.
#
# webgpu is here only for the include path: terminal.h transitively
# pulls <yetty/yetty/yetty.h>, which #includes <webgpu/webgpu.h>.
# ycompositor itself does not use webgpu types.
target_link_libraries(yetty_ycompositor PUBLIC
    yetty_ycore
    yetty_yfigure
    yetty_ygrid
    yetty_ywire
    yetty_ydraw_core
)
