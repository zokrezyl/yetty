# msgpack — MessagePack serialization (C only).
#
# yetty's only consumer is src/yetty/yctl/rpc-message.c which uses the C
# API (`msgpack_unpacked`, `msgpack_unpack_next`, `msgpack_object`). The
# C++ branch (msgpack-cxx) was dropped to avoid a no-value C++ dep.
#
# Consumes a prebuilt static lib + headers from the 3rdparty release
# tarball published by build-3rdparty-msgpack-c.yml. The from-source
# build (CMake driver, per-platform handling) lives in
# build-tools/3rdparty/msgpack-c/_build.sh.
#
# Exposed target: `msgpack-c` (matches what shared.cmake links against).

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET msgpack-c)
    return()
endif()

yetty_3rdparty_fetch(msgpack-c _MSGPACK_DIR)

# Tarball layout: lib/libmsgpack-c.a (POSIX) or lib/msgpack-c.lib (native MSVC).
# msgpack 6.x's CMakeLists installs as msgpack-c.lib (with the dash).
if(WIN32 AND EXISTS "${_MSGPACK_DIR}/lib/msgpack-c.lib")
    set(_MSGPACK_LIB "${_MSGPACK_DIR}/lib/msgpack-c.lib")
elseif(WIN32 AND EXISTS "${_MSGPACK_DIR}/lib/msgpackc.lib")
    set(_MSGPACK_LIB "${_MSGPACK_DIR}/lib/msgpackc.lib")
elseif(EXISTS "${_MSGPACK_DIR}/lib/libmsgpack-c.a")
    set(_MSGPACK_LIB "${_MSGPACK_DIR}/lib/libmsgpack-c.a")
elseif(EXISTS "${_MSGPACK_DIR}/lib/libmsgpackc.a")
    set(_MSGPACK_LIB "${_MSGPACK_DIR}/lib/libmsgpackc.a")
else()
    message(FATAL_ERROR
        "msgpack-c: no static lib found in ${_MSGPACK_DIR}/lib/ — \
tarball layout changed? (check build-tools/3rdparty/msgpack-c/_build.sh)")
endif()
if(NOT EXISTS "${_MSGPACK_DIR}/include/msgpack.h")
    message(FATAL_ERROR
        "msgpack-c: msgpack.h not found in ${_MSGPACK_DIR}/include/ — tarball layout changed?")
endif()

add_library(msgpack-c STATIC IMPORTED GLOBAL)
set_target_properties(msgpack-c PROPERTIES
    IMPORTED_LOCATION "${_MSGPACK_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_MSGPACK_DIR}/include"
)

message(STATUS "msgpack-c: prebuilt v${YETTY_3RDPARTY_msgpack-c_VERSION} (${_MSGPACK_LIB})")
