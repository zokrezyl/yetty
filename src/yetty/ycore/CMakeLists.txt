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

# Dawn/WebGPU is optional — only headers/lib needed by ycore's GPU paths.
# Cross-builds that don't ship a Dawn prebuilt for the target arch (e.g.
# linux-riscv64) flip YETTY_ENABLE_LIB_WEBGPU=OFF; without the gate, cmake
# emits a literal `-lwebgpu` link flag (the imported target doesn't exist)
# and the link fails. ycore's own .c files don't need webgpu.h.
if(YETTY_ENABLE_LIB_WEBGPU)
    target_include_directories(yetty_ycore PUBLIC ${DAWN_INCLUDE_DIR})
    target_link_libraries(yetty_ycore PUBLIC webgpu)
endif()
