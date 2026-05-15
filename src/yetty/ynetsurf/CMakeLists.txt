# yetty_ynetsurf — NetSurf 3.11 frontend that emits ypaint primitives.
#
# Depends on the `netsurf_core` IMPORTED target produced by
# build-tools/yetty/libs/netsurf.cmake. Disables itself if NetSurf core
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
        yetty_ydraw_core
        yetty_ysdf
)

if(NOT MSVC)
    target_compile_options(yetty_ynetsurf PRIVATE -Wall -Wno-unused-parameter)
endif()

# NetSurf core asks the frontend for its built-in stylesheets via URLs
# like resource:default.css. Bake the unpacked source's resources/ dir
# into the binary so the resource: fetcher resolves out of the box.
# YETTY_NETSURF_MESSAGES_FILE is the per-language Messages file split
# from FatMessages by libs/netsurf-messages.cmake at build time —
# loading it makes NetSurf's `messages_get("FetchErrorTitle")` etc.
# return real strings instead of the raw key.
target_compile_definitions(yetty_ynetsurf PRIVATE
    YETTY_NETSURF_RESOURCES_DIR="${YETTY_NETSURF_ROOT}/netsurf/resources"
    YETTY_NETSURF_MESSAGES_FILE="${CMAKE_BINARY_DIR}/netsurf-Messages-en"
)
