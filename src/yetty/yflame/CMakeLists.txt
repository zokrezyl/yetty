# yflame — yclass class `yflame:flame`: a flame-graph client/model.
#
# flame.c is the hand-written annotated source (#includes flame.gen.c at the
# foot); methods.gen.c + rpc.gen.c are codegen output. It parses folded stacks
# into a call tree and renders a ydraw drawable list of boxes + MSDF labels,
# plus hit-test / focus for interactive zoom. The yclass model.yaml drives the
# FFI / host-language bindings. GPU-less — produces generic ydraw primitives.

add_library(yetty_yflame STATIC
    flame.c
    methods.gen.c
    rpc.gen.c
)

target_include_directories(yetty_yflame PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) live under src/yetty/yflame/.
target_include_directories(yetty_yflame PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yflame PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_yface
    yetty_ydraw_core
    yetty_ysdf
)

# flame.c carries `[[clang::annotate(...)]]` (C23 attribute syntax).
if(NOT MSVC)
    set_target_properties(yetty_yflame PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
