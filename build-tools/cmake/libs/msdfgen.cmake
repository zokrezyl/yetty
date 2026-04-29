# msdfgen — multi-channel signed distance field generator.
#
# Consumes a prebuilt static lib + headers from the 3rdparty release
# tarball published by build-3rdparty-msdfgen.yml. The from-source
# build (cmake; depends on prebuilt freetype + tinyxml2 at compile time)
# lives in build-tools/3rdparty/msdfgen/_build.sh.
#
# Exposed targets:
#   msdfgen-core / msdfgen::msdfgen-core
#   msdfgen-ext  / msdfgen::msdfgen-ext (links freetype + tinyxml2)

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

if(TARGET msdfgen::msdfgen-core)
    return()
endif()

# Pull in deps — msdfgen-ext links freetype + tinyxml2.
include(${CMAKE_CURRENT_LIST_DIR}/tinyxml2.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/freetype.cmake)

yetty_3rdparty_fetch(msdfgen _MSDFGEN_DIR)

if(WIN32)
    set(_MSDFGEN_CORE_LIB "${_MSDFGEN_DIR}/lib/msdfgen-core.lib")
    set(_MSDFGEN_EXT_LIB  "${_MSDFGEN_DIR}/lib/msdfgen-ext.lib")
else()
    set(_MSDFGEN_CORE_LIB "${_MSDFGEN_DIR}/lib/libmsdfgen-core.a")
    set(_MSDFGEN_EXT_LIB  "${_MSDFGEN_DIR}/lib/libmsdfgen-ext.a")
endif()

if(NOT EXISTS "${_MSDFGEN_CORE_LIB}")
    message(FATAL_ERROR "msdfgen: archive not found at ${_MSDFGEN_CORE_LIB} — tarball layout changed?")
endif()

add_library(msdfgen-core STATIC IMPORTED GLOBAL)
set_target_properties(msdfgen-core PROPERTIES
    IMPORTED_LOCATION "${_MSDFGEN_CORE_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_MSDFGEN_DIR}/include"
    INTERFACE_COMPILE_DEFINITIONS "MSDFGEN_PUBLIC=;MSDFGEN_USE_CPP11"
)
add_library(msdfgen::msdfgen-core ALIAS msdfgen-core)

if(EXISTS "${_MSDFGEN_EXT_LIB}")
    add_library(msdfgen-ext STATIC IMPORTED GLOBAL)
    set_target_properties(msdfgen-ext PROPERTIES
        IMPORTED_LOCATION "${_MSDFGEN_EXT_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${_MSDFGEN_DIR}/include"
        INTERFACE_COMPILE_DEFINITIONS "MSDFGEN_EXT_PUBLIC=;MSDFGEN_EXTENSIONS;MSDFGEN_USE_TINYXML2;MSDFGEN_DISABLE_PNG"
        INTERFACE_LINK_LIBRARIES "msdfgen-core;Freetype::Freetype;tinyxml2::tinyxml2"
    )
    add_library(msdfgen::msdfgen-ext ALIAS msdfgen-ext)
endif()

message(STATUS "msdfgen: prebuilt v${YETTY_3RDPARTY_msdfgen_VERSION} (${_MSDFGEN_DIR}/lib/)")
