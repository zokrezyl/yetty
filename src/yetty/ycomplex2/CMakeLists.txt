# ycomplex2 — v2 drawable classes for the complex kinds (image, mesh,
# shadertoy, video). Each class packs ONE complex record via the kind's
# existing GPU-less client emitter; nothing here re-encodes a wire layout.
# The `2` suffix is transitional (epic #712) — see ydrawlist2/README.md.

set(ycomplex2_gen_sources
    ${YETTY_ROOT}/src/yetty/gen/api/ycomplex2/image.c
    ${YETTY_ROOT}/src/yetty/gen/api/ycomplex2/mesh.c
    ${YETTY_ROOT}/src/yetty/gen/api/ycomplex2/shadertoy.c
)
set(ycomplex2_sources
    image.c
    mesh.c
    shadertoy.c
)
set(ycomplex2_defines "")
if(YETTY_ENABLE_FEATURE_YVIDEO)
    list(APPEND ycomplex2_gen_sources ${YETTY_ROOT}/src/yetty/gen/api/ycomplex2/video.c)
    list(APPEND ycomplex2_sources video.c)
    list(APPEND ycomplex2_defines YETTY_YCOMPLEX2_HAS_YVIDEO)
endif()

add_library(yetty_api_ycomplex2 STATIC ${ycomplex2_gen_sources})

target_include_directories(yetty_api_ycomplex2 PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_api_ycomplex2 PUBLIC
    yetty_ycore
    yetty_yclass
)

add_library(yetty_ycomplex2 STATIC ${ycomplex2_sources})

target_include_directories(yetty_ycomplex2 PUBLIC
    ${YETTY_ROOT}/include
)
# Hand-written sources #include their generated impl glue via "yetty/gen/...".
target_include_directories(yetty_ycomplex2 PRIVATE ${YETTY_ROOT}/src)

if(ycomplex2_defines)
    target_compile_definitions(yetty_ycomplex2 PUBLIC ${ycomplex2_defines})
    target_compile_definitions(yetty_api_ycomplex2 PUBLIC ${ycomplex2_defines})
endif()

target_link_libraries(yetty_ycomplex2 PUBLIC
    yetty_api_ycomplex2
    yetty_ydrawlist2     # drawable parent + pack slot
    yetty_api_ydrawlist2
    yetty_ycore
    yetty_yclass
    yetty_ydraw_list
    yetty_yimage_core     # image: decode + emit_into
    yetty_ymesh_core      # mesh: glb parse + render
    yetty_yshadertoy_core # shadertoy: prim serialize
)
if(YETTY_ENABLE_FEATURE_YVIDEO)
    target_link_libraries(yetty_ycomplex2 PUBLIC yetty_yvideo_core)
endif()

# The annotated sources carry `[[clang::annotate(...)]]` (C23 attribute
# syntax).
if(NOT MSVC)
    set_target_properties(yetty_ycomplex2 PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()

# Register with the yclass codegen graph.
yetty_yclass_module(ycomplex2 DEFINES ${ycomplex2_defines})
