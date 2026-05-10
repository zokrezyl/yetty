# netsurf — NetSurf 3.11 core + helper libraries.
#
# Consumes a prebuilt static-lib + headers + resources tarball published
# by build-3rdparty-netsurf.yml. The tarball preserves the in-source
# layout the consumer code knows (`${YETTY_NETSURF_ROOT}/inst-monkey/...`
# and `${YETTY_NETSURF_ROOT}/netsurf/...`), so the only thing this file
# does is point YETTY_NETSURF_ROOT at the extracted tarball directory
# and declare the IMPORTED targets ynetsurf links against. Consumer
# CMakeLists (src/yetty/ynetsurf, tools/ynetsurf) need no changes.
#
# Exposed targets:
#   ns_css ns_dom ns_hubbub ns_parserutils ns_wapcaplet
#   ns_nsutils ns_nsbmp ns_nsgif ns_nslog ns_nspsl
#   ns_svgtiny ns_utf8proc
#   netsurf_core   — ar'd from netsurf core .o files (frontends/* excluded)
#
# NetSurf is currently built only for linux-x86_64
# (see build-tools/3rdparty/netsurf/build.sh — monkey TARGET doesn't
# cross-compile cleanly). On any other platform the fetch is skipped
# silently and consumers fall through their `if(NOT TARGET netsurf_core)`
# guards.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

if(TARGET netsurf_core)
    return()
endif()

#-----------------------------------------------------------------------------
# Platform gate — netsurf prebuilt is linux-x86_64 only. On any other
# target, leave netsurf_core undefined; callers handle it.
#-----------------------------------------------------------------------------
yetty_3rdparty_target_platform(_NS_PLATFORM)
if(NOT _NS_PLATFORM STREQUAL "linux-x86_64")
    message(STATUS "netsurf: ${_NS_PLATFORM} not supported — skipping (linux-x86_64 only)")
    return()
endif()

#-----------------------------------------------------------------------------
# Fetch + extract. yetty_3rdparty_fetch is no-op on cache hit.
#-----------------------------------------------------------------------------
yetty_3rdparty_fetch(netsurf _NS_DIR)

# Re-export YETTY_NETSURF_ROOT so the consumer CMakeLists (which still
# reference ${YETTY_NETSURF_ROOT}/netsurf/resources) resolve into the
# extracted tarball without further changes.
set(YETTY_NETSURF_ROOT "${_NS_DIR}" CACHE PATH "" FORCE)

set(_NS_INST "${_NS_DIR}/inst-monkey")
set(_NS_CORE "${_NS_DIR}/netsurf")

# Sanity-check tarball layout.
foreach(_F
    "${_NS_INST}/lib/libnetsurf_core.a"
    "${_NS_INST}/lib/libcss.a"
    "${_NS_INST}/include/dom"
    "${_NS_CORE}/include/netsurf/netsurf.h"
    "${_NS_CORE}/desktop/gui_table.h"
    "${_NS_CORE}/utils/log.h"
    "${_NS_CORE}/content/fetch.h"
    "${_NS_CORE}/resources/default.css"
    "${_NS_DIR}/Messages-en")
    if(NOT EXISTS "${_F}")
        message(FATAL_ERROR
            "netsurf: missing ${_F} — tarball layout changed? \
(check build-tools/3rdparty/netsurf/_build.sh)")
    endif()
endforeach()

# Mirror the in-tree consumer's expectation: a Messages-en file at
# ${CMAKE_BINARY_DIR}/netsurf-Messages-en. The tarball ships it
# pre-split from FatMessages; just copy into place at configure time
# so src/yetty/ynetsurf/CMakeLists.txt's compile definition stays the
# untouched ${CMAKE_BINARY_DIR}/netsurf-Messages-en literal.
configure_file(
    "${_NS_DIR}/Messages-en"
    "${CMAKE_BINARY_DIR}/netsurf-Messages-en"
    COPYONLY)

#-----------------------------------------------------------------------------
# Helper-lib IMPORTED targets. Same names + properties as the old
# in-source build so consumers don't notice the swap.
#-----------------------------------------------------------------------------
set(_NS_LIB_NAMES
    css dom hubbub parserutils wapcaplet
    nsutils nsbmp nsgif nslog nspsl
    svgtiny utf8proc)

foreach(l IN LISTS _NS_LIB_NAMES)
    add_library(ns_${l} STATIC IMPORTED GLOBAL)
    set_target_properties(ns_${l} PROPERTIES
        IMPORTED_LOCATION "${_NS_INST}/lib/lib${l}.a"
        INTERFACE_INCLUDE_DIRECTORIES "${_NS_INST}/include")
endforeach()

#-----------------------------------------------------------------------------
# netsurf_core — the synthesized archive of core .o files (frontends/*
# excluded). Public include dirs match the original three-root setup so
# `#include "netsurf/...","desktop/...","utils/...","content/..."` all
# resolve.
#-----------------------------------------------------------------------------
add_library(netsurf_core STATIC IMPORTED GLOBAL)
set_target_properties(netsurf_core PROPERTIES
    IMPORTED_LOCATION "${_NS_INST}/lib/libnetsurf_core.a"
    INTERFACE_INCLUDE_DIRECTORIES
        "${_NS_INST}/include;${_NS_CORE};${_NS_CORE}/include")
target_link_libraries(netsurf_core INTERFACE
    ns_css ns_dom ns_hubbub ns_parserutils ns_wapcaplet
    ns_nsutils ns_nsbmp ns_nsgif ns_nslog ns_nspsl
    ns_svgtiny ns_utf8proc)

#-----------------------------------------------------------------------------
# Run-time link deps. Same set the in-source build pulled in:
#   - libcurl (yetty's prebuilt, with openssl-new bundled)
#   - libxml-2.0, libjpeg, libpng, libwebp from the host (pkg-config)
#   - z, m
#-----------------------------------------------------------------------------
include(${YETTY_ROOT}/build-tools/cmake/libs/libcurl.cmake)

find_package(PkgConfig REQUIRED)
pkg_check_modules(_NS_LIBXML  REQUIRED libxml-2.0)
pkg_check_modules(_NS_LIBJPEG REQUIRED libjpeg)
pkg_check_modules(_NS_LIBPNG  REQUIRED libpng)
pkg_check_modules(_NS_LIBWEBP REQUIRED libwebp)

target_link_libraries(netsurf_core INTERFACE
    CURL::libcurl
    ${_NS_LIBXML_LIBRARIES}
    ${_NS_LIBJPEG_LIBRARIES}
    ${_NS_LIBPNG_LIBRARIES}
    ${_NS_LIBWEBP_LIBRARIES}
    z
    m)
target_include_directories(netsurf_core INTERFACE
    ${_NS_LIBXML_INCLUDE_DIRS}
    ${_NS_LIBJPEG_INCLUDE_DIRS}
    ${_NS_LIBPNG_INCLUDE_DIRS}
    ${_NS_LIBWEBP_INCLUDE_DIRS})

message(STATUS "netsurf: prebuilt v${YETTY_3RDPARTY_netsurf_VERSION} (${_NS_PLATFORM})")
