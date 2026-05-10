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
# Cross-platform: the 3rdparty tarball is published for every yetty
# target via build-3rdparty-netsurf.yml. NetSurf's monkey TARGET only
# truly compiles on linux + macOS; for windows-MSVC / webasm /
# iOS / tvOS the upstream tarball is a placeholder containing only an
# UNSUPPORTED marker. We detect that here and skip target creation —
# the rest of yetty (including ylexbor) configures unaffected.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

if(TARGET netsurf_core)
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

# Detect the placeholder tarball that build-3rdparty-netsurf.yml
# publishes for platforms NetSurf can't build for. Skip silently —
# consumer CMakeLists guard via `if(NOT TARGET netsurf_core) return()`.
if(EXISTS "${_NS_DIR}/UNSUPPORTED")
    yetty_3rdparty_target_platform(_NS_PLATFORM)
    message(STATUS
        "netsurf: ${_NS_PLATFORM} not supported by NetSurf's monkey \
TARGET — skipping (see ${_NS_DIR}/UNSUPPORTED)")
    return()
endif()

# Sanity-check tarball layout (real builds only).
foreach(_F
    "${_NS_INST}/lib/libnetsurf_core.a"
    "${_NS_INST}/lib/libcss.a"
    "${_NS_INST}/include/dom"
    "${_NS_CORE}/include/netsurf/netsurf.h"
    "${_NS_CORE}/desktop/gui_table.h"
    "${_NS_CORE}/utils/log.h"
    "${_NS_CORE}/content/fetch.h"
    "${_NS_CORE}/resources/default.css")
    if(NOT EXISTS "${_F}")
        message(FATAL_ERROR
            "netsurf: missing ${_F} — tarball layout changed? \
(check build-tools/3rdparty/netsurf/_build.sh)")
    endif()
endforeach()

# Messages-en is pre-split on native builds (build-host == target). For
# cross builds the tarball ships only FatMessages; cmake splits it
# locally if a system `split-messages` binary is on PATH, otherwise
# the consumer falls back to FatMessages-as-text.
if(EXISTS "${_NS_DIR}/Messages-en")
    configure_file(
        "${_NS_DIR}/Messages-en"
        "${CMAKE_BINARY_DIR}/netsurf-Messages-en"
        COPYONLY)
elseif(EXISTS "${_NS_CORE}/resources/FatMessages")
    find_program(_NS_SPLIT_MESSAGES split-messages)
    if(_NS_SPLIT_MESSAGES)
        execute_process(
            COMMAND "${_NS_SPLIT_MESSAGES}" -l en -p any -f messages
                    -o "${CMAKE_BINARY_DIR}/netsurf-Messages-en"
                    "${_NS_CORE}/resources/FatMessages"
            RESULT_VARIABLE _NS_SPLIT_RC)
        if(NOT _NS_SPLIT_RC EQUAL 0)
            configure_file(
                "${_NS_CORE}/resources/FatMessages"
                "${CMAKE_BINARY_DIR}/netsurf-Messages-en"
                COPYONLY)
        endif()
    else()
        # No split-messages on PATH — ship FatMessages as-is. NetSurf's
        # `messages_load` will treat unrecognised lines as no-ops.
        configure_file(
            "${_NS_CORE}/resources/FatMessages"
            "${CMAKE_BINARY_DIR}/netsurf-Messages-en"
            COPYONLY)
    endif()
endif()

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
#   - libjpeg-turbo (yetty's prebuilt, jpeg-static target)
#   - libxml-2.0, libpng, libwebp from the host (pkg-config)
#   - z, m
#-----------------------------------------------------------------------------
include(${YETTY_ROOT}/build-tools/cmake/libs/libcurl.cmake)

find_package(PkgConfig REQUIRED)
pkg_check_modules(_NS_LIBXML  REQUIRED libxml-2.0)
pkg_check_modules(_NS_LIBPNG  REQUIRED libpng)
pkg_check_modules(_NS_LIBWEBP REQUIRED libwebp)

if(NOT TARGET jpeg-static)
    message(FATAL_ERROR "netsurf: jpeg-static target not found — include libjpeg-turbo.cmake before netsurf.cmake")
endif()

target_link_libraries(netsurf_core INTERFACE
    CURL::libcurl
    ${_NS_LIBXML_LIBRARIES}
    jpeg-static
    ${_NS_LIBPNG_LIBRARIES}
    ${_NS_LIBWEBP_LIBRARIES}
    z
    m)
target_include_directories(netsurf_core INTERFACE
    ${_NS_LIBXML_INCLUDE_DIRS}
    ${_NS_LIBPNG_INCLUDE_DIRS}
    ${_NS_LIBWEBP_INCLUDE_DIRS})

yetty_3rdparty_target_platform(_NS_PLATFORM)
message(STATUS "netsurf: prebuilt v${YETTY_3RDPARTY_netsurf_VERSION} (${_NS_PLATFORM})")
