# yview — yclass client emitter for server-side scrollable content surfaces.
#
# view.c is the hand-written annotated source (#includes view.gen.c at the
# foot); the per-class <stem>.gen.c files are codegen output (#included into each .c). Builds the figure-tree
# wire records (DCS YCOMPOSITOR_BIN) from a ydraw-core drawable list and ships
# them via yface. The yclass model.yaml drives the FFI / host-language bindings.

add_library(yetty_yview STATIC
    view.c
    rpc.gen.c
)

target_include_directories(yetty_yview PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) live under src/yetty/yview/.
target_include_directories(yetty_yview PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yview PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_yfigure       # typed container stubs (yclass-RPC emit path)
    yetty_api_yterminal # figure_root_container navigation from the session root
    yetty_ydraw_core
    yetty_yplot_core   # set_plot: render a yplot expression to a drawable list (GPU-less)
)

# view.c carries `[[clang::annotate(...)]]` (C23 attribute syntax); see
# ygrid/CMakeLists.txt for the rationale.
if(NOT MSVC)
    set_target_properties(yetty_yview PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
