# ylexbor — permissively-licensed HTML/CSS renderer on top of lexbor.
#
# Unlike yetty_ynetsurf (which pulls in GPL NetSurf), ylexbor uses only
# Apache-2.0 lexbor + yetty's own code, so it can be linked directly
# into the main yetty binary without license conflict.


add_library(yetty_ylexbor STATIC
    ylexbor.c
    ylexbor-box.c
    ylexbor-layout.c
    ylexbor-paint.c
    ylexbor-css-vars.c
    ylexbor-js.c
    ylexbor-js-dom.c
    ylexbor-js-web.c
)

target_include_directories(yetty_ylexbor PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_ylexbor
    PUBLIC
        lexbor_static
        yetty_ypaint_core
        yetty_ysdf
        yetty_ycore
        yetty_yimage
        stb
)

# libpng + libjpeg-turbo handle real-world PNG/JPEG payloads that
# stb_image's built-in decoders trip over (interlaced PNG, ICC color
# profiles, palette+tRNS, JPEG with EXIF / progressive). Both targets
# are optional — if either is missing the dispatch in ylexbor-paint.c
# falls back through `#ifdef`s, but in our standard build both are
# available via the 3rdparty tarballs.
include(${YETTY_ROOT}/build-tools/cmake/libs/libpng.cmake OPTIONAL)
include(${YETTY_ROOT}/build-tools/cmake/libs/libjpeg-turbo.cmake OPTIONAL)
if(TARGET png_static)
    target_link_libraries(yetty_ylexbor PUBLIC png_static)
    target_compile_definitions(yetty_ylexbor PRIVATE YETTY_HAVE_LIBPNG=1)
    message(STATUS "yetty_ylexbor: libpng enabled")
endif()
if(TARGET turbojpeg-static)
    target_link_libraries(yetty_ylexbor PUBLIC turbojpeg-static)
    target_compile_definitions(yetty_ylexbor PRIVATE YETTY_HAVE_TURBOJPEG=1)
    message(STATUS "yetty_ylexbor: libjpeg-turbo enabled")
endif()

# QuickJS is opt-in — when present, ylexbor's <script> walker calls
# JS_Eval per inline script block. When absent, ylexbor-js.c compiles
# to no-op stubs so the rest of the lib stays the same shape.
if(TARGET qjs)
    target_link_libraries(yetty_ylexbor PUBLIC qjs)
    target_compile_definitions(yetty_ylexbor PRIVATE YETTY_HAVE_QUICKJS=1)
    message(STATUS "yetty_ylexbor: QuickJS enabled")
else()
    target_compile_definitions(yetty_ylexbor PRIVATE YETTY_HAVE_QUICKJS=0)
    message(STATUS "yetty_ylexbor: QuickJS disabled (qjs target missing)")
endif()

# libcurl — used by the JS WebAPI layer for fetch() / XHR / external
# script src=. Only required when QuickJS is enabled; when missing, we
# fall back to NULL fetches (the stubs still compile).
find_package(CURL QUIET)
if(CURL_FOUND)
    target_link_libraries(yetty_ylexbor PUBLIC CURL::libcurl)
    target_compile_definitions(yetty_ylexbor PRIVATE YETTY_HAVE_CURL=1)
    message(STATUS "yetty_ylexbor: libcurl enabled")
else()
    target_compile_definitions(yetty_ylexbor PRIVATE YETTY_HAVE_CURL=0)
    message(STATUS "yetty_ylexbor: libcurl missing — fetch() will fail")
endif()

if(NOT MSVC)
    target_compile_options(yetty_ylexbor PRIVATE -Wall -Wno-unused-parameter)
endif()
