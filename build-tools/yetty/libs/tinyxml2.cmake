# tinyxml2 — small XML parser.
#
# Consumes a prebuilt static lib + header from the 3rdparty release
# tarball published by build-3rdparty-tinyxml2.yml. The from-source
# build (cmake) lives in build-tools/3rdparty/tinyxml2/_build.sh.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET tinyxml2::tinyxml2)
    return()
endif()

yetty_3rdparty_fetch(tinyxml2 _TINYXML2_DIR)

if(WIN32)
    set(_TINYXML2_LIB "${_TINYXML2_DIR}/lib/tinyxml2.lib")
else()
    set(_TINYXML2_LIB "${_TINYXML2_DIR}/lib/libtinyxml2.a")
endif()

if(NOT EXISTS "${_TINYXML2_LIB}")
    message(FATAL_ERROR
        "tinyxml2: archive not found at ${_TINYXML2_LIB} — tarball layout changed?")
endif()

add_library(tinyxml2 STATIC IMPORTED GLOBAL)
set_target_properties(tinyxml2 PROPERTIES
    IMPORTED_LOCATION "${_TINYXML2_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_TINYXML2_DIR}/include"
)
add_library(tinyxml2::tinyxml2 ALIAS tinyxml2)

message(STATUS "tinyxml2: prebuilt v${YETTY_3RDPARTY_tinyxml2_VERSION} (${_TINYXML2_LIB})")
