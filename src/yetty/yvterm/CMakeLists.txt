add_library(yetty_yvterm STATIC
    vterm.c
)

target_include_directories(yetty_yvterm PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) sit under src/yetty/yvterm/.
# PRIVATE so the path does not leak to consumers — the public API is the per-class
# headers under include/yetty/yvterm/.
target_include_directories(yetty_yvterm PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yvterm PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_yfigure
    yetty_ydraw_core
    yetty_ydraw_factory  # yetty_ydraw_composite_destroy (anchored composite teardown)
    yetty_ywire          # wire statemachine: default raw-bytes sink → libvterm
    yetty_yfont_core     # MSDF font: glyph resolution + atlas for the text render
    yetty_yrender        # gpu_resource_set types used by the font atlas upload
    webgpu               # wgpu* pipeline/buffers/draw for the text render
    vterm
)

# vterm.c carries `[[clang::annotate(...)]]` on its class struct (codegen reads
# it off the AST). The C2x attribute syntax needs C23 to parse — host clang/gcc
# accept it by default, NDK clang 17 does not. (Mirrors ygrid/CMakeLists.txt.)
if(NOT MSVC)
    set_target_properties(yetty_yvterm PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
