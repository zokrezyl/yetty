# libjpeg-turbo — JPEG codec with SIMD optimisations.
#
# Consumes a prebuilt static lib + headers from the 3rdparty release
# tarball published by build-3rdparty-libjpeg-turbo.yml. The from-source
# build (cmake + NASM) lives in build-tools/3rdparty/libjpeg-turbo/_build.sh.
#
# Exposed target: turbojpeg-static — IMPORTED static archive (same name
# the from-source ExternalProject build exported).

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET turbojpeg-static)
    return()
endif()

yetty_3rdparty_fetch(libjpeg-turbo _LIBJPEG_DIR)

if(WIN32)
    set(_TURBOJPEG_LIB "${_LIBJPEG_DIR}/lib/turbojpeg-static.lib")
    set(_LIBJPEG_LIB   "${_LIBJPEG_DIR}/lib/jpeg-static.lib")
else()
    set(_TURBOJPEG_LIB "${_LIBJPEG_DIR}/lib/libturbojpeg.a")
    set(_LIBJPEG_LIB   "${_LIBJPEG_DIR}/lib/libjpeg.a")
endif()

if(NOT EXISTS "${_TURBOJPEG_LIB}")
    message(FATAL_ERROR "libjpeg-turbo: archive not found at ${_TURBOJPEG_LIB} — tarball layout changed?")
endif()

add_library(turbojpeg-static STATIC IMPORTED GLOBAL)
set_target_properties(turbojpeg-static PROPERTIES
    IMPORTED_LOCATION "${_TURBOJPEG_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_LIBJPEG_DIR}/include"
)

if(EXISTS "${_LIBJPEG_LIB}")
    add_library(jpeg-static STATIC IMPORTED GLOBAL)
    set_target_properties(jpeg-static PROPERTIES
        IMPORTED_LOCATION "${_LIBJPEG_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${_LIBJPEG_DIR}/include"
    )
endif()

set(JPEG_FOUND        TRUE                       CACHE BOOL   "" FORCE)
set(JPEG_INCLUDE_DIRS "${_LIBJPEG_DIR}/include"  CACHE PATH   "" FORCE)
set(JPEG_LIBRARIES    turbojpeg-static           CACHE STRING "" FORCE)

message(STATUS "libjpeg-turbo: prebuilt v${YETTY_3RDPARTY_libjpeg-turbo_VERSION} (${_TURBOJPEG_LIB})")
