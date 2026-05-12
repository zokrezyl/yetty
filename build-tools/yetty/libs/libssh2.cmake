# libssh2 — SSH2 protocol library.
#
# Consumes a prebuilt static lib + headers from the 3rdparty release
# tarball published by build-3rdparty-libssh2.yml. The from-source build
# (cmake driver, openssl backend, per-platform handling) lives in
# build-tools/3rdparty/libssh2/_build.sh — it links statically against
# the prebuilt openssl tarball we already use.
#
# Exposed target: `libssh2_static` — IMPORTED static archive. Same name
# the from-source CPM build exported, so existing consumers don't need
# to change.
#
# The static archive carries unresolved openssl symbols — consumer must
# wire `OpenSSL::SSL` + `OpenSSL::Crypto` to satisfy them at link time.
# We add those to INTERFACE_LINK_LIBRARIES so anything that links
# libssh2_static pulls in openssl transitively. Same pattern libcurl
# uses.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

if(TARGET libssh2_static)
    return()
endif()

# openssl must be resolved before us — the prebuilt libssh2 archive
# carries unresolved openssl symbols that yetty links via OpenSSL::SSL /
# OpenSSL::Crypto.
include(${YETTY_ROOT}/build-tools/cmake/openssl.cmake)

yetty_3rdparty_fetch(libssh2 _LIBSSH2_DIR)

if(WIN32)
    set(_LIBSSH2_LIB "${_LIBSSH2_DIR}/lib/libssh2.lib")
else()
    set(_LIBSSH2_LIB "${_LIBSSH2_DIR}/lib/libssh2.a")
endif()

if(NOT EXISTS "${_LIBSSH2_LIB}")
    message(FATAL_ERROR
        "libssh2: archive not found at ${_LIBSSH2_LIB} — \
tarball layout changed? (check build-tools/3rdparty/libssh2/_build.sh)")
endif()
if(NOT EXISTS "${_LIBSSH2_DIR}/include/libssh2.h")
    message(FATAL_ERROR
        "libssh2: libssh2.h not found in ${_LIBSSH2_DIR}/include/ — tarball layout changed?")
endif()

add_library(libssh2_static STATIC IMPORTED GLOBAL)
set_target_properties(libssh2_static PROPERTIES
    IMPORTED_LOCATION "${_LIBSSH2_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_LIBSSH2_DIR}/include"
    INTERFACE_LINK_LIBRARIES "OpenSSL::SSL;OpenSSL::Crypto"
)

# find_package(libssh2)-compat cache vars some downstream consumers read.
set(LIBSSH2_FOUND        TRUE                            CACHE BOOL    "" FORCE)
set(LIBSSH2_INCLUDE_DIR  "${_LIBSSH2_DIR}/include"       CACHE PATH    "" FORCE)
set(LIBSSH2_LIBRARY      libssh2_static                  CACHE STRING  "" FORCE)
set(LIBSSH2_LIBRARIES    libssh2_static                  CACHE STRING  "" FORCE)

message(STATUS "libssh2: prebuilt v${YETTY_3RDPARTY_libssh2_VERSION} (${_LIBSSH2_LIB})")
