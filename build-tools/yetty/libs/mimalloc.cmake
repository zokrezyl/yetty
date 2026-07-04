# mimalloc — general-purpose allocator (microsoft/mimalloc).
#
# Consumes a prebuilt static lib + headers from the 3rdparty release
# tarball published by build-3rdparty-mimalloc.yml. The from-source
# build (cmake) lives in build-tools/3rdparty/mimalloc/_build.sh.
#
# The archive is compiled with MI_OVERRIDE=ON: it defines malloc/free/
# realloc/... entry points, so linking it into an executable ahead of
# libc routes every allocation through mimalloc (Linux/macOS/Android;
# on Windows static override is not possible — the archive there only
# provides the explicit mi_* API). Statistics come from mimalloc-stats.h
# (mi_stats_get) and mi_process_info, surfaced through
# <yetty/ycore/memstats.h>.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET mimalloc::mimalloc)
    return()
endif()

yetty_3rdparty_fetch(mimalloc _MIMALLOC_DIR)

if(WIN32)
    set(_MIMALLOC_LIB "${_MIMALLOC_DIR}/lib/mimalloc.lib")
else()
    set(_MIMALLOC_LIB "${_MIMALLOC_DIR}/lib/libmimalloc.a")
endif()

if(NOT EXISTS "${_MIMALLOC_LIB}")
    message(FATAL_ERROR
        "mimalloc: archive not found at ${_MIMALLOC_LIB} — tarball layout changed?")
endif()

add_library(mimalloc STATIC IMPORTED GLOBAL)
set_target_properties(mimalloc PROPERTIES
    IMPORTED_LOCATION "${_MIMALLOC_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_MIMALLOC_DIR}/include"
)
if(WIN32)
    # mimalloc's OS primitive layer on Windows.
    set_target_properties(mimalloc PROPERTIES
        INTERFACE_LINK_LIBRARIES "psapi;shell32;user32;advapi32;bcrypt")
elseif(NOT APPLE AND NOT EMSCRIPTEN)
    # pthread TLS destructors on Linux/Android (no-op on modern glibc
    # where pthread lives in libc, still required for older/bionic).
    set_target_properties(mimalloc PROPERTIES
        INTERFACE_LINK_LIBRARIES "pthread")
endif()
add_library(mimalloc::mimalloc ALIAS mimalloc)

message(STATUS "mimalloc: prebuilt v${YETTY_3RDPARTY_mimalloc_VERSION} (${_MIMALLOC_LIB})")
