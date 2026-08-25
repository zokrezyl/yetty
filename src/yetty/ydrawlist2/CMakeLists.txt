# ydrawlist2 — yclass client interface over the ydraw drawable list (v2).
# Role-split codegen layout (same as yview):
#
#   yetty_api_ydrawlist2  compiles the generated api glue and exports
#                         <yetty/api/ydrawlist2/*.h> — the object API.
#                         GPU-free (yclass + ycore only).
#   yetty_ydrawlist2      compiles the hand-written annotated sources (each
#                         #includes its generated impl glue at the foot).
#                         Strictly client-side; never linked into the yetty
#                         process's render path.
#
# The `2` suffix is transitional: these modules run alongside the plain-C
# producer surface until the tools migrate, then the suffix drops in the
# cleanup pass (epic #712).

add_library(yetty_api_ydrawlist2 STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/ydrawlist2/drawable.c
    ${YETTY_ROOT}/src/yetty/gen/api/ydrawlist2/shape.c
    ${YETTY_ROOT}/src/yetty/gen/api/ydrawlist2/list.c
)

target_include_directories(yetty_api_ydrawlist2 PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_api_ydrawlist2 PUBLIC
    yetty_ycore
    yetty_yclass
)

add_library(yetty_ydrawlist2 STATIC
    drawable.c
    shape.c
    list.c
)

target_include_directories(yetty_ydrawlist2 PUBLIC
    ${YETTY_ROOT}/include
)
# The hand-written sources #include their generated impl glue via
# "yetty/gen/impl/...".
target_include_directories(yetty_ydrawlist2 PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_ydrawlist2 PUBLIC
    yetty_api_ydrawlist2
    yetty_ycore
    yetty_yclass
    yetty_ydraw_list
    yetty_yface # dcs_emit: the YDRAW_BIN envelope framing
)

# The annotated sources carry `[[clang::annotate(...)]]` (C23 attribute
# syntax).
if(NOT MSVC)
    set_target_properties(yetty_ydrawlist2 PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()

# Register with the yclass codegen graph — the module list IS these
# call sites (see build-tools/yetty/yclass-codegen.cmake).
yetty_yclass_module(ydrawlist2)
