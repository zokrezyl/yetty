# libco — minimal stackful coroutines (ISC license).
#
# Consumes a prebuilt static lib + header from the 3rdparty release
# tarball published by build-3rdparty-libco.yml. The from-source build
# (single .c file, platform-appropriate stack-switch backend selected by
# libco's own #ifdefs) lives in build-tools/3rdparty/libco/_build.sh.
#
# Exposed target: `co` (the IMPORTED static lib that
# src/yetty/yco/CMakeLists.txt links against).
#
# Skipped on emscripten — yetty's webasm coroutine backend uses
# emscripten_fiber_t (Asyncify) directly, see ycoroutine.c on that
# platform. Same exclusion the from-source build had.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET co)
    return()
endif()

if(EMSCRIPTEN)
    return()
endif()

yetty_3rdparty_fetch(libco _LIBCO_DIR)

if(WIN32)
    set(_LIBCO_LIB "${_LIBCO_DIR}/lib/libco.lib")
else()
    set(_LIBCO_LIB "${_LIBCO_DIR}/lib/libco.a")
endif()

if(NOT EXISTS "${_LIBCO_LIB}")
    message(FATAL_ERROR
        "libco: archive not found at ${_LIBCO_LIB} — \
tarball layout changed? (check build-tools/3rdparty/libco/_build.sh)")
endif()
if(NOT EXISTS "${_LIBCO_DIR}/include/libco.h")
    message(FATAL_ERROR
        "libco: libco.h not found in ${_LIBCO_DIR}/include/ — tarball layout changed?")
endif()

add_library(co STATIC IMPORTED GLOBAL)
set_target_properties(co PROPERTIES
    IMPORTED_LOCATION "${_LIBCO_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_LIBCO_DIR}/include"
)

message(STATUS "libco: prebuilt @${YETTY_3RDPARTY_libco_VERSION} (${_LIBCO_LIB})")
