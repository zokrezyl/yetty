# yframework — exposes <yetty/yframework/yframework.h>. INTERFACE target so
# consumers of the header get the include path and the WebGPU dep
# transitively. yframework.c is compiled into the final executable via
# YETTY_SOURCES (same pattern as yetty.c), not as a static archive, to
# avoid archive-order pain on the link line.

add_library(yetty_yframework INTERFACE)

target_include_directories(yetty_yframework INTERFACE
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yframework INTERFACE
    webgpu
)
