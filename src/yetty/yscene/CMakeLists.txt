# yscene — retained scene graph (#691). One yclass class + one internal
# tree in one module:
#
#   dom    (dom.c)    GPU-free retained content tree — a PLAIN INTERNAL
#                     struct + functions (internal.h), created and
#                     exclusively owned by its scene; not a yclass class,
#                     never RPC/FFI-exposed. Depends on ydraw-core + ysdf
#                     for wire-record stride/AABB dispatch only.
#   scene  (scene.c)  the renderer figure kind and the single public
#                     yclass object. Owns one dom, surfaces the content-
#                     mutation API, derives world state, builds staging,
#                     and draws.
#
# Build ownership follows the role-split codegen layout (see ydummy):
#   yetty_api_yscene  the generated typed object-API stubs — what
#                     consumers link.
#   yetty_yscene      the hand-written class TUs (each foot-#includes its
#                     generated impl glue from src/yetty/gen/impl/yscene/).

add_library(yetty_api_yscene STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/yscene/scene.c
    # vtermgrid is an `internal` yclass: no public object-API TU is generated.
)

target_include_directories(yetty_api_yscene PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_api_yscene PUBLIC
    yetty_ycore
    yetty_yclass
)

add_library(yetty_yscene STATIC
    scene.c
    dom.c
    vtermgrid.c
    vtermgrid-store.c
)

target_include_directories(yetty_yscene PUBLIC
    ${YETTY_ROOT}/include
)
# scene.c #includes its generated impl glue via "yetty/gen/impl/...".
target_include_directories(yetty_yscene PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yscene PUBLIC
    yetty_api_yscene
    yetty_ycore
    yetty_yclass
    yetty_yfigure
    yetty_ydraw_core
    yetty_ydraw_factory   # complex (composite) instances at paint cut points
    yetty_ysdf
    yetty_yrender
    yetty_yfont_core      # font cache: materialise wire-shipped fonts
    yetty_yplatform_core  # cache dir + mkdir/file_exists for the font cache
    vterm                 # class@yscene:vtermgrid: independent client libvterm
    webgpu
)

# The class TUs carry `[[clang::annotate(...)]]` (C23 attribute syntax);
# see ygrid/CMakeLists.txt for the rationale.
if(NOT MSVC)
    set_target_properties(yetty_yscene PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
    # Same warning GATE as yetty_ymux (#699 review cycle 19).
    target_compile_options(yetty_yscene PRIVATE -Werror=return-type -Werror=comment)
endif()

# Register with the yclass codegen graph — the module list IS these
# call sites (see build-tools/yetty/yclass-codegen.cmake).
yetty_yclass_module(yscene)
