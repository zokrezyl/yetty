# brotli — Google's brotli compression.
#
# Consumes prebuilt static libs + headers from the 3rdparty release
# tarball published by build-3rdparty-brotli.yml. The from-source build
# (cmake) lives in build-tools/3rdparty/brotli/_build.sh.
#
# Exposed targets: brotlicommon, brotlidec, brotlienc — IMPORTED static
# archives, same names the upstream cmake exports.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET brotlidec)
    return()
endif()

yetty_3rdparty_fetch(brotli _BROTLI_DIR)

if(WIN32)
    set(_BROTLI_COMMON "${_BROTLI_DIR}/lib/brotlicommon.lib")
    set(_BROTLI_DEC    "${_BROTLI_DIR}/lib/brotlidec.lib")
    set(_BROTLI_ENC    "${_BROTLI_DIR}/lib/brotlienc.lib")
else()
    set(_BROTLI_COMMON "${_BROTLI_DIR}/lib/libbrotlicommon.a")
    set(_BROTLI_DEC    "${_BROTLI_DIR}/lib/libbrotlidec.a")
    set(_BROTLI_ENC    "${_BROTLI_DIR}/lib/libbrotlienc.a")
endif()

foreach(_LIB "${_BROTLI_COMMON}" "${_BROTLI_DEC}" "${_BROTLI_ENC}")
    if(NOT EXISTS "${_LIB}")
        message(FATAL_ERROR "brotli: archive not found at ${_LIB} — tarball layout changed?")
    endif()
endforeach()

add_library(brotlicommon STATIC IMPORTED GLOBAL)
set_target_properties(brotlicommon PROPERTIES
    IMPORTED_LOCATION "${_BROTLI_COMMON}"
    INTERFACE_INCLUDE_DIRECTORIES "${_BROTLI_DIR}/include"
)
add_library(brotlidec STATIC IMPORTED GLOBAL)
set_target_properties(brotlidec PROPERTIES
    IMPORTED_LOCATION "${_BROTLI_DEC}"
    INTERFACE_INCLUDE_DIRECTORIES "${_BROTLI_DIR}/include"
    INTERFACE_LINK_LIBRARIES "brotlicommon"
)
add_library(brotlienc STATIC IMPORTED GLOBAL)
set_target_properties(brotlienc PROPERTIES
    IMPORTED_LOCATION "${_BROTLI_ENC}"
    INTERFACE_INCLUDE_DIRECTORIES "${_BROTLI_DIR}/include"
    INTERFACE_LINK_LIBRARIES "brotlicommon"
)

# Compat for FreeType.cmake's old bundle-mode names.
set(BROTLIDEC_LIBRARIES "brotlidec;brotlicommon" CACHE INTERNAL "")
set(BROTLI_INCLUDE_DIR  "${_BROTLI_DIR}/include" CACHE INTERNAL "")

message(STATUS "brotli: prebuilt v${YETTY_3RDPARTY_brotli_VERSION} (${_BROTLI_DIR}/lib/)")
