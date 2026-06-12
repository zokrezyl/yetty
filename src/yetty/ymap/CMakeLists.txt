# ymap — yclass class `ymap:map`: a slippy-map client/model.
#
# map.c is the hand-written annotated source (#includes map.gen.c at the
# foot); methods.gen.c + rpc.gen.c are codegen output. It holds the view
# (center / zoom / viewport / tile provider) and renders through the
# yopenstreet engine (raster composite or vector SDF/MSDF drawables). The
# yclass model.yaml drives the FFI / host-language bindings. GPU-less.

add_library(yetty_ymap STATIC
    map.c
    methods.gen.c
    rpc.gen.c
)

target_include_directories(yetty_ymap PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) live under src/yetty/ymap/.
target_include_directories(yetty_ymap PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_ymap PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_yopenstreet
    yetty_yplatform_core
)

# map.c carries `[[clang::annotate(...)]]` (C23 attribute syntax) — same
# constraint as every yclass module (mirrors src/yetty/yflame).
if(NOT MSVC)
    set_target_properties(yetty_ymap PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
