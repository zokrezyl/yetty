# libcss — NetSurf's MIT-licensed CSS cascade (plus its two MIT deps:
# libparserutils + libwapcaplet). Consumes a prebuilt static-lib +
# headers tarball published by build-3rdparty-libcss.yml.
#
# Why this lives alongside netsurf.cmake: the netsurf prebuilt also
# exposes ns_css / ns_parserutils / ns_wapcaplet IMPORTED targets — but
# its tarball drags in the GPL'd NetSurf browser core. This file
# isolates the MIT CSS bits so they can stay once the netsurf prebuilt
# is dropped. Until then both coexist; consumers (currently
# src/yetty/ybrowser) keep linking ns_* until they migrate to the
# libcss_* targets here.
#
# Exposed targets:
#   libcss_static          (STATIC IMPORTED) — the CSS cascade
#   libparserutils_static  (STATIC IMPORTED) — tokenizer used by libcss
#   libwapcaplet_static    (STATIC IMPORTED) — interned-string pool
#
# Cross-platform: the 3rdparty tarball is published for every yetty
# target via build-3rdparty-libcss.yml. NetSurf's buildsystem is
# GNU-make-only — for windows-MSVC / webasm / android / iOS / tvOS the
# upstream tarball is a placeholder containing only an UNSUPPORTED
# marker. We detect that here and skip target creation; consumers
# guard via `if(TARGET libcss_static)`.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET libcss_static)
    return()
endif()

# libcss is VENDORED in-repo (build-tools/3rdparty/libcss/vendor/) and built
# from that source, not downloaded — so local edits to the CSS engine take
# effect on a rebuild without publishing a release. If the per-version
# tarball isn't already in the 3rdparty cache, build it now from the vendored
# source; yetty_3rdparty_fetch below then consumes it from the cache exactly
# like a downloaded prebuilt. Editing the vendored source requires bumping
# `build-tools/3rdparty/libcss/version` (or deleting the cached tarball) to
# force a rebuild.
yetty_3rdparty_target_platform(_LIBCSS_BUILD_PLAT)
file(READ "${YETTY_ROOT}/build-tools/3rdparty/libcss/version" _LIBCSS_BUILD_VER)
string(STRIP "${_LIBCSS_BUILD_VER}" _LIBCSS_BUILD_VER)
set(_LIBCSS_CACHED
    "${YETTY_3RDPARTY_CACHE_DIR}/libcss-${_LIBCSS_BUILD_PLAT}-${_LIBCSS_BUILD_VER}.tar.gz")
if(NOT EXISTS "${_LIBCSS_CACHED}")
    message(STATUS
        "libcss: building vendored source (${_LIBCSS_BUILD_PLAT} ${_LIBCSS_BUILD_VER})")
    file(MAKE_DIRECTORY "${YETTY_3RDPARTY_CACHE_DIR}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
                "TARGET_PLATFORM=${_LIBCSS_BUILD_PLAT}"
                "OUTPUT_DIR=${YETTY_3RDPARTY_CACHE_DIR}"
                bash "${YETTY_ROOT}/build-tools/3rdparty/libcss/build.sh"
        RESULT_VARIABLE _LIBCSS_BUILD_RC)
    if(NOT _LIBCSS_BUILD_RC EQUAL 0)
        message(FATAL_ERROR
            "libcss: building vendored source failed (rc=${_LIBCSS_BUILD_RC}) — \
needs make/perl/gcc + flex/bison/gperf on PATH")
    endif()
endif()

yetty_3rdparty_fetch(libcss _LIBCSS_DIR)

# Placeholder tarball for platforms where the upstream make build
# can't run — skip silently so the rest of yetty configures unaffected.
if(EXISTS "${_LIBCSS_DIR}/UNSUPPORTED")
    yetty_3rdparty_target_platform(_LIBCSS_PLATFORM)
    message(STATUS
        "libcss: ${_LIBCSS_PLATFORM} not supported by NetSurf buildsystem \
— skipping (see ${_LIBCSS_DIR}/UNSUPPORTED)")
    return()
endif()

# Tarball layout: lib/lib{css,parserutils,wapcaplet}.a + include/...
foreach(_F
    "${_LIBCSS_DIR}/lib/libcss.a"
    "${_LIBCSS_DIR}/lib/libparserutils.a"
    "${_LIBCSS_DIR}/lib/libwapcaplet.a"
    "${_LIBCSS_DIR}/include/libcss/libcss.h"
    "${_LIBCSS_DIR}/include/libwapcaplet/libwapcaplet.h")
    if(NOT EXISTS "${_F}")
        message(FATAL_ERROR
            "libcss: missing ${_F} — \
tarball layout changed? (check build-tools/3rdparty/libcss/_build.sh)")
    endif()
endforeach()

add_library(libwapcaplet_static STATIC IMPORTED GLOBAL)
set_target_properties(libwapcaplet_static PROPERTIES
    IMPORTED_LOCATION "${_LIBCSS_DIR}/lib/libwapcaplet.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_LIBCSS_DIR}/include")

add_library(libparserutils_static STATIC IMPORTED GLOBAL)
set_target_properties(libparserutils_static PROPERTIES
    IMPORTED_LOCATION "${_LIBCSS_DIR}/lib/libparserutils.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_LIBCSS_DIR}/include")

# libparserutils calls iconv_open/close in src/input/filter.c. On Linux
# iconv lives in glibc, but macOS ships it as a separate libiconv that
# must be linked explicitly. (Mirror of the same fix in netsurf.cmake
# for the ns_parserutils target — kept in both so a consumer using
# either path links correctly.)
if(APPLE)
    set_target_properties(libparserutils_static PROPERTIES
        INTERFACE_LINK_LIBRARIES iconv)
endif()

add_library(libcss_static STATIC IMPORTED GLOBAL)
set_target_properties(libcss_static PROPERTIES
    IMPORTED_LOCATION "${_LIBCSS_DIR}/lib/libcss.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_LIBCSS_DIR}/include"
    INTERFACE_LINK_LIBRARIES "libparserutils_static;libwapcaplet_static")

message(STATUS "libcss: prebuilt v${YETTY_3RDPARTY_libcss_VERSION}")
