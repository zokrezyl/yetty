# yetty_ynetsurf — NetSurf 3.11 frontend that emits ypaint primitives.
#
# Depends on the `netsurf_core` IMPORTED target produced by
# build-tools/cmake/libs/netsurf.cmake. Disables itself if NetSurf core
# is not available so the rest of yetty still configures.

if(NOT TARGET netsurf_core)
    message(STATUS "yetty_ynetsurf: netsurf_core target missing — skipping")
    return()
endif()

add_library(yetty_ynetsurf STATIC
    ynetsurf.c
    ynetsurf-plotters.c
)

target_include_directories(yetty_ynetsurf PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_ynetsurf
    PUBLIC
        netsurf_core
        yetty_ypaint_core
        yetty_ysdf
)

target_compile_options(yetty_ynetsurf PRIVATE -Wall -Wno-unused-parameter)

# NetSurf core asks the frontend for its built-in stylesheets via URLs
# like resource:default.css. Bake the unpacked source's resources/ dir
# into the binary so the resource: fetcher resolves out of the box.
target_compile_definitions(yetty_ynetsurf PRIVATE
    YETTY_NETSURF_RESOURCES_DIR="${YETTY_NETSURF_ROOT}/netsurf/resources"
)
