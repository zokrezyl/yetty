# OpenSSL — pinned to 1.1.1w via the janbar/openssl-cmake wrapper.
#
# Consumes a prebuilt static lib + headers from the 3rdparty release
# tarball published by build-3rdparty-openssl.yml. The from-source build
# (CMake driver, per-platform handling) lives in
# build-tools/3rdparty/openssl/_build.sh.
#
# A parallel build of upstream OpenSSL 4.x lives at
# build-tools/3rdparty/openssl-new/ + build-3rdparty-openssl-new.yml,
# attached to `lib-openssl-new-*` releases. Yetty currently consumes the
# 1.1.1w build because cpr / libcurl / libssh2 pinned versions still
# expect the 1.1.1 API. When those are bumped, this file flips to
# `yetty_3rdparty_fetch(openssl-new ...)` and the openssl-new dir
# becomes the canonical openssl.
#
# Exposed targets (matched to what cpr / libcurl / libssh2 expect):
#   OpenSSL::SSL        interface lib that links libssl.a + libcrypto.a
#   OpenSSL::Crypto     interface lib that links libcrypto.a
# Plus the standard find_package(OpenSSL) compat variables.
#
# Opt-out: -DYETTY_OPENSSL_USE_SYSTEM=ON falls back to find_package(OpenSSL).
# This is escape-hatch territory; the prebuilt is the default to keep
# every cross target on the same OpenSSL version (see version file).

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

if(TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
    return()
endif()

option(YETTY_OPENSSL_USE_SYSTEM "Use system OpenSSL via find_package (skip prebuilt fetch)" OFF)

if(YETTY_OPENSSL_USE_SYSTEM)
    find_package(OpenSSL REQUIRED)
    message(STATUS "openssl: using system OpenSSL ${OPENSSL_VERSION} (YETTY_OPENSSL_USE_SYSTEM=ON)")
    return()
endif()

#-----------------------------------------------------------------------------
# Fetch + import static targets
#-----------------------------------------------------------------------------
yetty_3rdparty_fetch(openssl _OPENSSL_DIR)

# Tarball layout: lib/libssl.a + lib/libcrypto.a + include/openssl/*.h
# (Windows: lib/libssl.lib + lib/libcrypto.lib)
if(WIN32)
    set(_SSL_LIB_NAME    "libssl.lib")
    set(_CRYPTO_LIB_NAME "libcrypto.lib")
else()
    set(_SSL_LIB_NAME    "libssl.a")
    set(_CRYPTO_LIB_NAME "libcrypto.a")
endif()

set(_SSL_LIB_PATH     "${_OPENSSL_DIR}/lib/${_SSL_LIB_NAME}")
set(_CRYPTO_LIB_PATH  "${_OPENSSL_DIR}/lib/${_CRYPTO_LIB_NAME}")
set(_OPENSSL_INC_DIR  "${_OPENSSL_DIR}/include")

foreach(_F "${_SSL_LIB_PATH}" "${_CRYPTO_LIB_PATH}")
    if(NOT EXISTS "${_F}")
        message(FATAL_ERROR
            "openssl: prebuilt library not found: ${_F} — \
tarball layout changed? (check build-tools/3rdparty/openssl/_build.sh)")
    endif()
endforeach()
if(NOT EXISTS "${_OPENSSL_INC_DIR}/openssl/ssl.h")
    message(FATAL_ERROR
        "openssl: ssl.h not found in ${_OPENSSL_INC_DIR}/openssl/ — \
tarball layout changed?")
endif()

# Imported static targets for the actual archives. Use the bare names
# `crypto` / `ssl` to mirror the from-source build (those names are
# referenced by some downstream linker invocations).
add_library(crypto STATIC IMPORTED GLOBAL)
set_target_properties(crypto PROPERTIES
    IMPORTED_LOCATION "${_CRYPTO_LIB_PATH}"
    INTERFACE_INCLUDE_DIRECTORIES "${_OPENSSL_INC_DIR}"
)

add_library(ssl STATIC IMPORTED GLOBAL)
set_target_properties(ssl PROPERTIES
    IMPORTED_LOCATION "${_SSL_LIB_PATH}"
    INTERFACE_INCLUDE_DIRECTORIES "${_OPENSSL_INC_DIR}"
    INTERFACE_LINK_LIBRARIES "crypto"
)

# Standard OpenSSL::Crypto / OpenSSL::SSL aliases that cpr / libcurl /
# libssh2 expect from find_package(OpenSSL).
add_library(OpenSSL::Crypto INTERFACE IMPORTED GLOBAL)
set_target_properties(OpenSSL::Crypto PROPERTIES
    INTERFACE_LINK_LIBRARIES      "crypto"
    INTERFACE_INCLUDE_DIRECTORIES "${_OPENSSL_INC_DIR}"
)

add_library(OpenSSL::SSL INTERFACE IMPORTED GLOBAL)
set_target_properties(OpenSSL::SSL PROPERTIES
    INTERFACE_LINK_LIBRARIES      "ssl;OpenSSL::Crypto"
    INTERFACE_INCLUDE_DIRECTORIES "${_OPENSSL_INC_DIR}"
)

# find_package(OpenSSL) compat — some downstream cmake reads these
# variables instead of using the imported targets. Set them all.
set(OPENSSL_FOUND           TRUE                              CACHE BOOL    "" FORCE)
set(OPENSSL_INCLUDE_DIR     "${_OPENSSL_INC_DIR}"             CACHE PATH    "" FORCE)
set(OPENSSL_CRYPTO_LIBRARY  "${_CRYPTO_LIB_PATH}"             CACHE FILEPATH "" FORCE)
set(OPENSSL_SSL_LIBRARY     "${_SSL_LIB_PATH}"                CACHE FILEPATH "" FORCE)
set(OPENSSL_LIBRARIES       "OpenSSL::SSL;OpenSSL::Crypto"    CACHE STRING  "" FORCE)
# Hardcode the semantic openssl version. cpr / libcurl / libssh2 read
# this and decide which API to use; they expect a clean "1.1.1" prefix,
# not the janbar release tag (which carries a date suffix).
set(OPENSSL_VERSION         "1.1.1w"                          CACHE STRING  "" FORCE)

message(STATUS "openssl: prebuilt v${YETTY_3RDPARTY_openssl_VERSION} "
               "(${_SSL_LIB_PATH}, ${_CRYPTO_LIB_PATH})")
