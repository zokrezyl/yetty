# ybrowser — fork of yetty_ylexbor that swaps lexbor's CSS cascade for
# NetSurf's libcss (MIT). HTML parsing and QuickJS bindings stay on
# lexbor; only the CSS-engine path differs. Symbols are deliberately left
# as `yetty_ylexbor_*` so the source tree diff against `src/yetty/ylexbor`
# stays minimal. The two libraries must not be linked into the same
# binary.

add_library(yetty_ybrowser STATIC
    ybrowser.c
    ybrowser-box.c
    ybrowser-layout.c
    ybrowser-paint.c
    ybrowser-css-vars.c
    ybrowser-libcss.c
    ybrowser-js.c
    ybrowser-js-dom.c
    ybrowser-js-web.c
)

target_include_directories(yetty_ybrowser PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_ybrowser
    PUBLIC
        lexbor_static
        yetty_ydraw_core
        yetty_ysdf
        yetty_ysvg
        yetty_ycore
        yetty_yimage_core
        stb
)

# libcss (MIT) — NetSurf's CSS cascade. Prefer the standalone libcss
# 3rdparty prebuilt (build-tools/yetty/libs/libcss.cmake, target
# libcss_static). Fall back to the legacy netsurf prebuilt (ns_css) on
# platforms where libcss isn't yet a standalone prebuilt. If neither is
# available, ybrowser falls back to the lexbor string-parsing path via
# YETTY_HAVE_LIBCSS=0.
include(${YETTY_ROOT}/build-tools/yetty/libs/libcss.cmake OPTIONAL)
include(${YETTY_ROOT}/build-tools/yetty/libs/netsurf.cmake OPTIONAL)
if(TARGET libcss_static)
    target_link_libraries(yetty_ybrowser PUBLIC libcss_static)
    target_compile_definitions(yetty_ybrowser PRIVATE YETTY_HAVE_LIBCSS=1)
    message(STATUS "yetty_ybrowser: libcss enabled (libcss_static)")
elseif(TARGET ns_css)
    target_link_libraries(yetty_ybrowser PUBLIC ns_css ns_wapcaplet ns_parserutils)
    target_compile_definitions(yetty_ybrowser PRIVATE YETTY_HAVE_LIBCSS=1)
    message(STATUS "yetty_ybrowser: libcss enabled (ns_css legacy)")
else()
    target_compile_definitions(yetty_ybrowser PRIVATE YETTY_HAVE_LIBCSS=0)
    message(STATUS "yetty_ybrowser: libcss missing — falling back to lexbor CSS path")
endif()

# libpng + libjpeg-turbo — same as ylexbor.
include(${YETTY_ROOT}/build-tools/yetty/libs/libpng.cmake OPTIONAL)
include(${YETTY_ROOT}/build-tools/yetty/libs/libjpeg-turbo.cmake OPTIONAL)
if(TARGET png_static)
    target_link_libraries(yetty_ybrowser PUBLIC png_static)
    target_compile_definitions(yetty_ybrowser PRIVATE YETTY_HAVE_LIBPNG=1)
    message(STATUS "yetty_ybrowser: libpng enabled")
endif()
if(TARGET turbojpeg-static)
    target_link_libraries(yetty_ybrowser PUBLIC turbojpeg-static)
    target_compile_definitions(yetty_ybrowser PRIVATE YETTY_HAVE_TURBOJPEG=1)
    message(STATUS "yetty_ybrowser: libjpeg-turbo enabled")
endif()

# libwebp — from the host via pkg-config (same source netsurf.cmake uses).
# Optional: when absent, WebP images fall back to a placeholder. Needed for
# modern sites (e.g. Google News serves every article thumbnail as WebP and
# ignores the Accept header, so content-negotiation can't dodge it).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(YBROWSER_LIBWEBP QUIET libwebp)
    if(YBROWSER_LIBWEBP_FOUND)
        target_link_libraries(yetty_ybrowser PUBLIC ${YBROWSER_LIBWEBP_LIBRARIES})
        target_include_directories(yetty_ybrowser PRIVATE ${YBROWSER_LIBWEBP_INCLUDE_DIRS})
        target_compile_definitions(yetty_ybrowser PRIVATE YETTY_HAVE_LIBWEBP=1)
        message(STATUS "yetty_ybrowser: libwebp enabled (${YBROWSER_LIBWEBP_VERSION})")
    else()
        message(STATUS "yetty_ybrowser: libwebp not found — WebP images render as placeholders")
    endif()
endif()

if(TARGET qjs)
    target_link_libraries(yetty_ybrowser PUBLIC qjs)
    target_compile_definitions(yetty_ybrowser PRIVATE YETTY_HAVE_QUICKJS=1)
    message(STATUS "yetty_ybrowser: QuickJS enabled")
else()
    target_compile_definitions(yetty_ybrowser PRIVATE YETTY_HAVE_QUICKJS=0)
    message(STATUS "yetty_ybrowser: QuickJS disabled (qjs target missing)")
endif()

# libcurl is wired in by the platform cmake (e.g. linux.cmake) via
# build-tools/yetty/libs/libcurl.cmake — it creates the IMPORTED target
# CURL::libcurl + sets CURL_FOUND in cache. find_package(CURL) re-runs
# the system search and ignores the imported target, so use the target
# check directly (same pattern as src/yetty/ycat/CMakeLists.txt).
if(YETTY_ENABLE_LIB_LIBCURL AND TARGET CURL::libcurl)
    target_link_libraries(yetty_ybrowser PUBLIC CURL::libcurl)
    target_compile_definitions(yetty_ybrowser PRIVATE YETTY_HAVE_CURL=1)
    message(STATUS "yetty_ybrowser: libcurl enabled")
else()
    target_compile_definitions(yetty_ybrowser PRIVATE YETTY_HAVE_CURL=0)
    message(STATUS "yetty_ybrowser: libcurl missing — fetch() will fail")
endif()

if(NOT MSVC)
    target_compile_options(yetty_ybrowser PRIVATE -Wall -Wno-unused-parameter)
endif()
