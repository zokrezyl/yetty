# harfbuzz — complex-script text shaping (Arabic joining, Indic reordering,
# Thai mark positioning).
#
# Consumes a prebuilt static lib + headers from the 3rdparty release tarball
# published by build-3rdparty-harfbuzz.yml. The from-source build (cmake;
# minimal static — glib/ICU/Graphite2/FreeType glue all disabled, built-in
# hb-ucd Unicode) lives in build-tools/3rdparty/harfbuzz/_build.sh.
#
# HarfBuzz is standalone here: yetty feeds it font tables from the already-
# loaded FreeType faces at runtime, so this target does NOT depend on the
# freetype target (avoids a producer build-order cycle).
#
# Exposed target:
#   harfbuzz           — IMPORTED static archive
#   harfbuzz::harfbuzz — alias

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET harfbuzz)
    return()
endif()

yetty_3rdparty_fetch(harfbuzz _HARFBUZZ_DIR)

if(WIN32)
    set(_HARFBUZZ_LIB "${_HARFBUZZ_DIR}/lib/harfbuzz.lib")
else()
    set(_HARFBUZZ_LIB "${_HARFBUZZ_DIR}/lib/libharfbuzz.a")
endif()

if(NOT EXISTS "${_HARFBUZZ_LIB}")
    message(FATAL_ERROR "harfbuzz: archive not found at ${_HARFBUZZ_LIB} — tarball layout changed?")
endif()

# Upstream installs headers to include/harfbuzz/. Expose both the parent (so
# <harfbuzz/hb.h> resolves) and the harfbuzz/ dir itself (so a bare <hb.h>
# also works).
set(_HARFBUZZ_INC "${_HARFBUZZ_DIR}/include;${_HARFBUZZ_DIR}/include/harfbuzz")

add_library(harfbuzz STATIC IMPORTED GLOBAL)
set_target_properties(harfbuzz PROPERTIES
    IMPORTED_LOCATION "${_HARFBUZZ_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_HARFBUZZ_INC}"
)
# HarfBuzz is C++; a C-only consumer object set needs the C++ runtime pulled
# into the final link. yetty already links C++ (Dawn/WebGPU) so the runtime is
# present, but naming it on the interface keeps a freestanding link correct.
if(NOT WIN32 AND NOT APPLE AND NOT EMSCRIPTEN)
    set_property(TARGET harfbuzz APPEND PROPERTY INTERFACE_LINK_LIBRARIES stdc++ m)
elseif(APPLE)
    set_property(TARGET harfbuzz APPEND PROPERTY INTERFACE_LINK_LIBRARIES c++)
endif()

add_library(harfbuzz::harfbuzz ALIAS harfbuzz)

set(HARFBUZZ_INCLUDE_DIR "${_HARFBUZZ_INC}" CACHE INTERNAL "")
set(HARFBUZZ_LIBRARY     "${_HARFBUZZ_LIB}" CACHE INTERNAL "")
set(HARFBUZZ_FOUND       TRUE               CACHE BOOL "" FORCE)

message(STATUS "harfbuzz: prebuilt v${YETTY_3RDPARTY_harfbuzz_VERSION} (${_HARFBUZZ_LIB})")
