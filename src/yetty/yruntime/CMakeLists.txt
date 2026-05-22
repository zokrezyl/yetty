# yruntime — exposes <yetty/yruntime/yruntime.h>. INTERFACE target so
# consumers of the header get the include path and the WebGPU dep
# transitively. yruntime.c is compiled into the final executable via
# YETTY_SOURCES (same pattern as yetty.c), not as a static archive, to
# avoid archive-order pain on the link line.

add_library(yetty_yruntime INTERFACE)

target_include_directories(yetty_yruntime INTERFACE
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yruntime INTERFACE
    webgpu
)
