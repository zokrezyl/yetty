# yetty core (C)
add_library(yetty_ycore STATIC
    memstats.c
    result.c
    types.c
    util.c
    tcp-transport.c
)

target_include_directories(yetty_ycore PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# memstats.c samples mimalloc (mi_stats_get / mi_process_info) when the
# allocator is enabled; without it the sampler compiles to a stub that
# returns an error and consumers degrade gracefully. PUBLIC so any
# target linking ycore also gets the archive on its link line.
if(YETTY_ENABLE_LIB_MIMALLOC)
    target_compile_definitions(yetty_ycore PRIVATE YETTY_HAS_MIMALLOC=1)
    target_link_libraries(yetty_ycore PUBLIC mimalloc::mimalloc)
endif()

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
