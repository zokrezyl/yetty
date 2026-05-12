# yetty core (C)
add_library(yetty_ycore STATIC
    result.c
    types.c
    util.c
    tcp-transport.c
)

target_include_directories(yetty_ycore PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# ycore has no source-level webgpu dependency — no .c file or header
# under src/yetty/ycore or include/yetty/ycore references WGPU* / dawn /
# webgpu.h. BUT sibling-module headers (notably yetty/yterm/terminal.h,
# included from ygui_osc.c and others) do include <webgpu/webgpu.h>, so
# any consumer that pulls in ycore PLUS one of those headers needs the
# header search path. We propagate the include directory only; we do
# NOT propagate the libwebgpu_dawn.a link, which was a holdover that
# pulled Dawn into every transitive consumer (every tool, every demo)
# whether they used it or not. Targets that actually call WGPU symbols
# (yetty_yrender, yetty_ywebgpu) link Dawn themselves.
if(YETTY_ENABLE_LIB_WEBGPU)
    target_include_directories(yetty_ycore PUBLIC ${DAWN_INCLUDE_DIR})
endif()
