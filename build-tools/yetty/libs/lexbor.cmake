# lexbor — Apache-2.0 HTML/CSS parsing + DOM + selector engine.
#
# Consumes a prebuilt static lib + headers from the 3rdparty release
# tarball published by build-3rdparty-lexbor.yml. lexbor is pure C /
# libc-only — no transitive prebuilt deps to thread through.
#
# Exposes `lexbor_static` (STATIC IMPORTED) — same target name the
# upstream CMakeLists used to produce, so consumers
# (src/yetty/ylexbor, tools/ylexbor, test/integration/ylexbor) link
# unchanged.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

if(TARGET lexbor_static)
    return()
endif()

yetty_3rdparty_fetch(lexbor _LEXBOR_DIR)

# Tarball layout: lib/liblexbor_static.a + include/lexbor/<module>/*.h
if(WIN32 AND EXISTS "${_LEXBOR_DIR}/lib/lexbor_static.lib")
    set(_LEXBOR_LIB "${_LEXBOR_DIR}/lib/lexbor_static.lib")
elseif(EXISTS "${_LEXBOR_DIR}/lib/liblexbor_static.a")
    set(_LEXBOR_LIB "${_LEXBOR_DIR}/lib/liblexbor_static.a")
else()
    message(FATAL_ERROR
        "lexbor: no static lib found in ${_LEXBOR_DIR}/lib/ — \
tarball layout changed? (check build-tools/3rdparty/lexbor/_build.sh)")
endif()
if(NOT EXISTS "${_LEXBOR_DIR}/include/lexbor/core/core.h")
    message(FATAL_ERROR
        "lexbor: core.h not found in ${_LEXBOR_DIR}/include/lexbor/core/ — \
tarball layout changed?")
endif()

add_library(lexbor_static STATIC IMPORTED GLOBAL)
# LEXBOR_STATIC mirrors the PUBLIC define upstream's CMakeLists set on
# the static target. Without it, Windows consumers would import symbols
# as DLL exports.
set_target_properties(lexbor_static PROPERTIES
    IMPORTED_LOCATION "${_LEXBOR_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_LEXBOR_DIR}/include"
    INTERFACE_COMPILE_DEFINITIONS "LEXBOR_STATIC"
)

message(STATUS "lexbor: prebuilt v${YETTY_3RDPARTY_lexbor_VERSION}")
