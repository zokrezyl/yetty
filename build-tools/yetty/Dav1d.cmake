# dav1d - AV1 video decoder (VideoLAN, BSD 2-Clause).
#
# Consumes a prebuilt static lib + headers from the 3rdparty release tarball
# published by build-3rdparty-dav1d.yml. The from-source build for every
# yetty target (per-platform meson cross files for android/ios/wasm/win)
# now lives in build-tools/3rdparty/dav1d/_build.sh — see that script and
# build-tools/3rdparty/README.md for how to add platforms or bump versions.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

if(TARGET dav1d)
    return()
endif()

yetty_3rdparty_fetch(dav1d _DAV1D_DIR)

# Tarball layout: lib/libdav1d.a (or lib/dav1d.lib on windows) + include/dav1d/dav1d.h
if(WIN32)
    set(_DAV1D_LIB_NAME "dav1d.lib")
else()
    set(_DAV1D_LIB_NAME "libdav1d.a")
endif()

set(_DAV1D_LIB_PATH "${_DAV1D_DIR}/lib/${_DAV1D_LIB_NAME}")
# meson on windows can also install as libdav1d.a — accept either.
if(NOT EXISTS "${_DAV1D_LIB_PATH}" AND EXISTS "${_DAV1D_DIR}/lib/libdav1d.a")
    set(_DAV1D_LIB_PATH "${_DAV1D_DIR}/lib/libdav1d.a")
endif()
set(_DAV1D_INCLUDE_DIR "${_DAV1D_DIR}/include")

if(NOT EXISTS "${_DAV1D_LIB_PATH}")
    message(FATAL_ERROR
        "dav1d: library not found at ${_DAV1D_LIB_PATH} — \
tarball layout changed? (check build-tools/3rdparty/dav1d/_build.sh)")
endif()
if(NOT EXISTS "${_DAV1D_INCLUDE_DIR}/dav1d/dav1d.h")
    message(FATAL_ERROR
        "dav1d: dav1d.h not found in ${_DAV1D_INCLUDE_DIR}/dav1d/ — \
tarball layout changed?")
endif()

add_library(dav1d STATIC IMPORTED GLOBAL)
set_target_properties(dav1d PROPERTIES
    IMPORTED_LOCATION "${_DAV1D_LIB_PATH}"
    INTERFACE_INCLUDE_DIRECTORIES "${_DAV1D_INCLUDE_DIR}"
)

# Match the link-deps the from-source build set up:
#   - Linux (glibc): pthreads + libdl
#   - everywhere else: nothing extra (Apple/Android/Windows/wasm toolchain handles it)
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    set_target_properties(dav1d PROPERTIES
        INTERFACE_LINK_LIBRARIES "Threads::Threads;${CMAKE_DL_LIBS}"
    )
endif()

message(STATUS "dav1d: prebuilt v${YETTY_3RDPARTY_dav1d_VERSION} (${_DAV1D_LIB_PATH})")
