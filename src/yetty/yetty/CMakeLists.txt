# The yetty module — exposes <yetty/yetty/yetty.h>. That header includes
# <webgpu/webgpu.h>, so webgpu is a PUBLIC link dep on this INTERFACE
# target: anything that links yetty_yetty gets the webgpu include path
# through INTERFACE_INCLUDE_DIRECTORIES, even if the consumer never
# touches WGPU types directly.
#
# yetty.c stays in YETTY_SOURCES (compiled into the final executable,
# alongside the rest of the orchestrator surface) — wrapping yetty.c in
# a STATIC library here would force this module to declare deps on
# every yetty subsystem, which causes archive-order pain on the final
# link line.

add_library(yetty_yetty INTERFACE)

target_include_directories(yetty_yetty INTERFACE
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yetty INTERFACE
    webgpu
)
