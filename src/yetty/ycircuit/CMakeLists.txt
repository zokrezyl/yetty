# ycircuit — yclass class `ycircuit:circuit`: an electronic schematic renderer.
#
# circuit.c is the hand-written annotated source (#includes circuit.gen.c at
# the foot); the per-class <stem>.gen.c files are codegen output (#included into each .c). It parses a small
# schematic DSL (or is filled through its add_* methods) into an editable
# circuit model and renders a ydraw drawable list: wires / component bodies as
# SDF segments, circles and triangles, and designators / values / labels as
# MSDF text spans. The yclass model.yaml drives the FFI / host-language
# bindings. GPU-less — produces generic ydraw primitives, shipped via the
# ycat / scrolling-layer path (like ymusic).

add_library(yetty_api_ycircuit STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/ycircuit/circuit.c
)
target_include_directories(yetty_api_ycircuit PUBLIC ${YETTY_ROOT}/include)
target_link_libraries(yetty_api_ycircuit PUBLIC yetty_ycore yetty_yclass)

add_library(yetty_ycircuit STATIC
    circuit.c
)

target_include_directories(yetty_ycircuit PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) live under src/yetty/ycircuit/.
target_include_directories(yetty_ycircuit PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_ycircuit PUBLIC
    yetty_api_ycircuit
    yetty_ycore
    yetty_yclass
    yetty_yface
    yetty_ydraw_core
    yetty_ysdf
)

# circuit.c carries `[[clang::annotate(...)]]` (C23 attribute syntax).
if(NOT MSVC)
    set_target_properties(yetty_ycircuit PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()

# Register with the yclass codegen graph — the module list IS these
# call sites (see build-tools/yetty/yclass-codegen.cmake).
yetty_yclass_module(ycircuit)
