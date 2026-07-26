# ydummy — pilot yclass module for the role-split codegen layout.
# One class, `canvas`; the module is pure contract + state (no GPU code,
# no GPU include anywhere — rendering the state is the embedding
# program's business, see demo/ydummy/server).
#
# Build ownership follows the generated-source ownership:
#
#   yetty_api_ydummy   compiles src/yetty/gen/api/ydummy/canvas.c and
#                     exports <yetty/api/ydummy/canvas.h> — the object
#                     API. What every consumer links; a PURE WIRE CLIENT
#                     links nothing else of this module. No strong
#                     references to the implementation, accessor, skels
#                     or registration.
#
#   yetty_ydummy      compiles the hand-written canvas.c (which
#                     #includes the generated impl glue
#                     src/yetty/gen/impl/ydummy/canvas.c: accessor,
#                     check shims, constructor stub, skels, factory,
#                     registration hook). Links yetty_api_ydummy — the
#                     skels re-enter the public API stubs.

add_library(yetty_api_ydummy STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/ydummy/canvas.c
)

target_include_directories(yetty_api_ydummy PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_api_ydummy PUBLIC
    yetty_ycore
    yetty_yclass
)

add_library(yetty_ydummy STATIC
    canvas.c
)

target_include_directories(yetty_ydummy PUBLIC
    ${YETTY_ROOT}/include
)
# canvas.c #includes the generated impl glue via "yetty/gen/impl/...".
target_include_directories(yetty_ydummy PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_ydummy PUBLIC
    yetty_api_ydummy
)

# canvas.c carries `[[clang::annotate(...)]]` (C23 attribute syntax); see
# ygrid/CMakeLists.txt for the rationale.
if(NOT MSVC)
    set_target_properties(yetty_ydummy PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
