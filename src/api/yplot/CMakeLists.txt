# api_yplot — the supported public yplot API facade (yetty_api_yplot_*).
#
# plot.c is the hand-written annotated source (it #includes its split impl glue
# src/yetty/gen/impl/api_yplot/plot.c at the foot); the public object-API stubs
# (yetty_api_yplot_*) are the split codegen's gen/api output. The yclass
# model.yaml drives the FFI / host-language bindings. The facade depends on the
# implementation (yetty_yplot_core), never the reverse.

add_library(yetty_api_yplot STATIC
    plot.c
    ${YETTY_ROOT}/src/yetty/gen/api/yplot/plot.c
)

target_include_directories(yetty_api_yplot PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) live under src/api/yplot/.
target_include_directories(yetty_api_yplot PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_api_yplot PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_yplot_core   # yetty_yplot_render + dcs_bin_emit (GPU-less)
    yetty_ydraw_list   # drawable_list_destroy
)

# plot.c carries `[[clang::annotate(...)]]` (C23 attribute syntax).
if(NOT MSVC)
    set_target_properties(yetty_api_yplot PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()

# Register with the yclass codegen graph — the module list IS these
# call sites (see build-tools/yetty/yclass-codegen.cmake).
yetty_yclass_module(api_yplot)
