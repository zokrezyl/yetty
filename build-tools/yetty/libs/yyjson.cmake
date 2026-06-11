# yyjson — fast JSON reader/writer (ibireme/yyjson).
#
# Consumes a prebuilt static lib + header from the 3rdparty release
# tarball published by build-3rdparty-yyjson.yml. The from-source
# build (cmake) lives in build-tools/3rdparty/yyjson/_build.sh.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET yyjson::yyjson)
    return()
endif()

yetty_3rdparty_fetch(yyjson _YYJSON_DIR)

if(WIN32)
    set(_YYJSON_LIB "${_YYJSON_DIR}/lib/yyjson.lib")
else()
    set(_YYJSON_LIB "${_YYJSON_DIR}/lib/libyyjson.a")
endif()

if(NOT EXISTS "${_YYJSON_LIB}")
    message(FATAL_ERROR
        "yyjson: archive not found at ${_YYJSON_LIB} — tarball layout changed?")
endif()

add_library(yyjson STATIC IMPORTED GLOBAL)
set_target_properties(yyjson PROPERTIES
    IMPORTED_LOCATION "${_YYJSON_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_YYJSON_DIR}/include"
)
add_library(yyjson::yyjson ALIAS yyjson)

message(STATUS "yyjson: prebuilt v${YETTY_3RDPARTY_yyjson_VERSION} (${_YYJSON_LIB})")
