# libuv — async I/O.
#
# Consumes a prebuilt static lib + headers from the 3rdparty release
# tarball published by build-3rdparty-libuv.yml. The from-source build
# (CMake driver, per-platform handling) lives in
# build-tools/3rdparty/libuv/_build.sh.
#
# Exposed target: `uv_a` (the static-archive name yetty's main code
# already links against in shared.cmake).

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

if(TARGET uv_a)
    return()
endif()

yetty_3rdparty_fetch(libuv _LIBUV_DIR)

# Tarball layout: lib/libuv.a (POSIX) or lib/libuv.lib (native MSVC).
# Older builds also produced uv_a.lib / libuv_a.a — accept those for
# back-compat with already-published tarballs.
if(WIN32 AND EXISTS "${_LIBUV_DIR}/lib/libuv.lib")
    set(_LIBUV_LIB "${_LIBUV_DIR}/lib/libuv.lib")
elseif(WIN32 AND EXISTS "${_LIBUV_DIR}/lib/uv_a.lib")
    set(_LIBUV_LIB "${_LIBUV_DIR}/lib/uv_a.lib")
elseif(EXISTS "${_LIBUV_DIR}/lib/libuv.a")
    set(_LIBUV_LIB "${_LIBUV_DIR}/lib/libuv.a")
elseif(EXISTS "${_LIBUV_DIR}/lib/libuv_a.a")
    set(_LIBUV_LIB "${_LIBUV_DIR}/lib/libuv_a.a")
else()
    message(FATAL_ERROR
        "libuv: no static lib found in ${_LIBUV_DIR}/lib/ — \
tarball layout changed? (check build-tools/3rdparty/libuv/_build.sh)")
endif()
if(NOT EXISTS "${_LIBUV_DIR}/include/uv.h")
    message(FATAL_ERROR
        "libuv: uv.h not found in ${_LIBUV_DIR}/include/ — tarball layout changed?")
endif()

add_library(uv_a STATIC IMPORTED GLOBAL)
set_target_properties(uv_a PROPERTIES
    IMPORTED_LOCATION "${_LIBUV_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_LIBUV_DIR}/include"
)

# Platform link deps that libuv expects from its consumers (mirrors what
# upstream libuv-config-cmake declares as INTERFACE_LINK_LIBRARIES).
if(WIN32)
    set_target_properties(uv_a PROPERTIES
        INTERFACE_LINK_LIBRARIES "iphlpapi;psapi;userenv;ws2_32;dbghelp;ole32;shell32"
    )
elseif(APPLE)
    # macOS/iOS: pthread + kernel kqueue/cf framework
    set_target_properties(uv_a PROPERTIES
        INTERFACE_LINK_LIBRARIES "pthread"
    )
elseif(ANDROID)
    set_target_properties(uv_a PROPERTIES
        INTERFACE_LINK_LIBRARIES "dl"
    )
elseif(EMSCRIPTEN)
    # nothing extra — emscripten libc covers it.
elseif(UNIX)
    set_target_properties(uv_a PROPERTIES
        INTERFACE_LINK_LIBRARIES "pthread;dl;rt"
    )
endif()

message(STATUS "libuv: prebuilt v${YETTY_3RDPARTY_libuv_VERSION} (${_LIBUV_LIB})")
