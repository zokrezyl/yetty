# ymap — the slippy-map module: yclass model + tile engine in one lib.
#
#   map.c           the `ymap:map` yclass class (hand-written annotated
#                   source; map.gen.c included at the foot). Provider
#                   registry, view state, pan/zoom verbs, render dispatch.
#                   map.gen.c + map.h are codegen output;
#                   model.yaml drives the FFI / host-language bindings.
#   engine.c        raster path: parallel tile fetch → RGBA complex →
#                   one yimage prim. Plus the slippy projection helpers.
#   vector-render.c vector path: shortbread MVT → SDF/MSDF drawables
#                   (cased roads, fills, decluttered labels).
#   vector-tile.c   minimal MVT (protobuf subset) decoder.
#   tile-fetch.c    disk cache + curl-multi parallel download (HTTP/3
#                   when the vendored curl has QUIC).
#   geoip.c         public-IP geolocation for the default map center.
#
# GPU-less: everything renders to generic ydraw drawables; the receiving
# terminal needs no ymap-specific code. Gated on libcurl + yimage (see
# src/yetty/CMakeLists.txt).

# GeoJSON overlay parsing (map.c overlay_geojson) uses the prebuilt yyjson.
include(${YETTY_ROOT}/build-tools/yetty/libs/yyjson.cmake)

add_library(yetty_api_ymap STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/ymap/map.c
)
target_include_directories(yetty_api_ymap PUBLIC ${YETTY_ROOT}/include)
target_link_libraries(yetty_api_ymap PUBLIC yetty_ycore yetty_yclass)

add_library(yetty_ymap STATIC
    map.c
    engine.c
    tile-fetch.c
    vector-tile.c
    vector-render.c
    geoip.c
)

target_include_directories(yetty_ymap PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ymap PUBLIC
    yetty_api_ymap
    yetty_ycore
    yetty_yclass
    yetty_yface
    yetty_yimage_core
    yetty_yplatform_core
    yetty_ysdf
    CURL::libcurl
    yyjson::yyjson
)

# map.c carries `[[clang::annotate(...)]]` (C23 attribute syntax) — same
# constraint as every yclass module (mirrors src/yetty/yflame).
if(NOT MSVC)
    set_target_properties(yetty_ymap PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()

# Register with the yclass codegen graph — the module list IS these
# call sites (see build-tools/yetty/yclass-codegen.cmake).
yetty_yclass_module(ymap)
