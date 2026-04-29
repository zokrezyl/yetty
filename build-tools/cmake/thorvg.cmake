# thorvg — vector graphics renderer (SVG/Lottie + SW raster).
#
# Consumes a prebuilt static lib + headers from the 3rdparty release
# tarball published by build-3rdparty-thorvg.yml. The from-source build
# (compiles common/, renderer/, sw_engine/, svg/, lottie/, raw/ globs +
# patches tvgInitializer.cpp for sized-delete + nothrow new/delete)
# lives in build-tools/3rdparty/thorvg/_build.sh.
#
# Exposed target: `thorvg_lib` — IMPORTED static archive. TVG_STATIC is
# kept on INTERFACE_COMPILE_DEFINITIONS so consumers don't get
# __declspec(dllimport) on Windows when they include thorvg.h.
#
# Internal headers (RenderMethod, PAINT() macro) used by
# src/yetty/ythorvg are exposed via the cache var
# YETTY_3RDPARTY_thorvg_INTERNAL_INCLUDE_DIR — that target adds it
# explicitly because they're not part of thorvg's public API.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

if(TARGET thorvg_lib)
    return()
endif()

yetty_3rdparty_fetch(thorvg _THORVG_DIR)

if(WIN32)
    set(_THORVG_STATIC_LIB "${_THORVG_DIR}/lib/libthorvg.lib")
else()
    set(_THORVG_STATIC_LIB "${_THORVG_DIR}/lib/libthorvg.a")
endif()

if(NOT EXISTS "${_THORVG_STATIC_LIB}")
    message(FATAL_ERROR
        "thorvg: archive not found at ${_THORVG_STATIC_LIB} — \
tarball layout changed? (check build-tools/3rdparty/thorvg/_build.sh)")
endif()
if(NOT EXISTS "${_THORVG_DIR}/include/thorvg.h")
    message(FATAL_ERROR
        "thorvg: thorvg.h not found in ${_THORVG_DIR}/include/ — tarball layout changed?")
endif()
if(NOT EXISTS "${_THORVG_DIR}/include-internal/renderer")
    message(FATAL_ERROR
        "thorvg: include-internal/renderer not found in ${_THORVG_DIR} — \
tarball layout changed? (yetty_ythorvg needs the internal headers)")
endif()

add_library(thorvg_lib STATIC IMPORTED GLOBAL)
set_target_properties(thorvg_lib PROPERTIES
    IMPORTED_LOCATION             "${_THORVG_STATIC_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_THORVG_DIR}/include"
    INTERFACE_COMPILE_DEFINITIONS "TVG_STATIC"
)

# Internal-headers path. Targets that need RenderMethod / PAINT() (just
# yetty_ythorvg today) include this directly; we keep it OFF the public
# interface so no other consumer accidentally pulls them in.
set(YETTY_3RDPARTY_thorvg_INTERNAL_INCLUDE_DIR
    "${_THORVG_DIR}/include-internal"
    CACHE INTERNAL "thorvg internal headers (renderer/, common/, ...)")

message(STATUS "thorvg: prebuilt v${YETTY_3RDPARTY_thorvg_VERSION} (${_THORVG_STATIC_LIB})")
