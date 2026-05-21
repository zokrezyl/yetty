# ywasm demos — programs that link libyetty_ywasm and talk to the local
# yetty's ywasm-layer over the PTY (stdin / stdout). Each demo runs as
# `./yetty -e demo-ywasm-<NN>-<name>`.
#
# Shared support: ytrace + the few platform glue symbols ytrace needs.
# An OBJECT library compiled once and linked into every demo exec, same
# pattern demo/ygui uses.

add_library(demo_ywasm_support OBJECT
    common.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)

target_include_directories(demo_ywasm_support PUBLIC
    ${YETTY_ROOT}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Inherit the codegen include dir (methods.gen.h) via yetty_ywasm's
# PUBLIC include dirs — every demo links yetty_ywasm anyway, but the
# support lib's translation units don't, so wire it in here too.
target_include_directories(demo_ywasm_support PUBLIC
    $<TARGET_PROPERTY:yetty_ywasm,INTERFACE_INCLUDE_DIRECTORIES>
)

find_package(Threads REQUIRED)

add_subdirectory(01_hello)
add_subdirectory(02_gradient)
add_subdirectory(03_async_init)
add_subdirectory(04_animation)
add_subdirectory(05_adapter_info)
add_subdirectory(06_buffer_write)
add_subdirectory(07_submit_chain)
add_subdirectory(08_glyph_shaders)
add_subdirectory(09_glyph_render)
add_subdirectory(10_glyph_animate)
