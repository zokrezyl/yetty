# yflame — yclass class `yflame:flame`: a flame-graph client/model.
#
# flame.c is the hand-written annotated source (#includes flame.gen.c at the
# foot); the per-class <stem>.gen.c files are codegen output (#included into each .c). It parses folded stacks
# into a call tree and renders a ydraw drawable list of boxes + MSDF labels,
# plus hit-test / focus for interactive zoom. The yclass model.yaml drives the
# FFI / host-language bindings. GPU-less — produces generic ydraw primitives.

add_library(yetty_api_yflame STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/yflame/flame.c
)
target_include_directories(yetty_api_yflame PUBLIC ${YETTY_ROOT}/include)
target_link_libraries(yetty_api_yflame PUBLIC yetty_ycore yetty_yclass)

add_library(yetty_yflame STATIC
    flame.c
)

target_include_directories(yetty_yflame PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) live under src/yetty/yflame/.
target_include_directories(yetty_yflame PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yflame PUBLIC
    yetty_api_yflame
    yetty_ycore
    yetty_yclass
    yetty_yface
    yetty_ydraw_list
    yetty_ysdf
)

# flame.c carries `[[clang::annotate(...)]]` (C23 attribute syntax).
if(NOT MSVC)
    set_target_properties(yetty_yflame PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()

# Register with the yclass codegen graph — the module list IS these
# call sites (see build-tools/yetty/yclass-codegen.cmake).
yetty_yclass_module(yflame)
