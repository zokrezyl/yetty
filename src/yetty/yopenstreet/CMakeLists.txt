# yopenstreet — OpenStreetMap snapshot rendering for the scrolling grid.
#
# CPU-only client-side library: slippy-tile math, libcurl tile download
# with disk cache, stb_image PNG decode, RGBA compositing, and packing as
# ONE yimage prim (the receiving terminal renders it through the existing
# yimage factory — no server-side yopenstreet code exists). Only added to
# the build when libcurl is enabled (see src/yetty/CMakeLists.txt).

add_library(yetty_yopenstreet STATIC
    openstreet.c
    tile-fetch.c
    vector-tile.c
    vector-render.c
    geoip.c
)

target_include_directories(yetty_yopenstreet PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_yopenstreet PUBLIC
    yetty_ycore
    yetty_yimage_core
    yetty_yplatform_core
    yetty_ysdf
    CURL::libcurl
)
