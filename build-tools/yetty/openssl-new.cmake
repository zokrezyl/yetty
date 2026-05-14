# OpenSSL 4.x — parallel to openssl.cmake which is pinned to 1.1.1w for
# the libssh2/libcurl/cpr stack. This file uses distinct target names so
# both versions can coexist in one build.
#
# Fetch tag: lib-openssl-new-<ver>
# Version source of truth: build-tools/3rdparty/openssl-new/version
# Tarball layout identical to the 1.1.1w build:
#   lib/libssl.a + lib/libcrypto.a + include/openssl/*.h
#
# Exposed targets:
#   OpenSSL4::SSL     interface lib → ssl_new + OpenSSL4::Crypto
#   OpenSSL4::Crypto  interface lib → crypto_new
# Imported archives: crypto_new, ssl_new (distinct from 1.1.1w's crypto/ssl).
#
# Cache variable: OPENSSL_NEW_VERSION — the semantic version string (e.g. 4.0.0).
# Consumers that branch on the API version read this.
#
# Opt-out: -DYETTY_OPENSSL_NEW_USE_SYSTEM=ON falls back to find_package(OpenSSL)
# and aliases its targets as OpenSSL4::*.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET OpenSSL4::SSL AND TARGET OpenSSL4::Crypto)
    return()
endif()

option(YETTY_OPENSSL_NEW_USE_SYSTEM
    "Use system OpenSSL via find_package for OpenSSL4:: targets (skip prebuilt fetch)" OFF)

if(YETTY_OPENSSL_NEW_USE_SYSTEM)
    find_package(OpenSSL REQUIRED)
    add_library(OpenSSL4::Crypto INTERFACE IMPORTED GLOBAL)
    set_target_properties(OpenSSL4::Crypto PROPERTIES
        INTERFACE_LINK_LIBRARIES      "OpenSSL::Crypto"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
    )
    add_library(OpenSSL4::SSL INTERFACE IMPORTED GLOBAL)
    set_target_properties(OpenSSL4::SSL PROPERTIES
        INTERFACE_LINK_LIBRARIES      "OpenSSL::SSL;OpenSSL4::Crypto"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
    )
    set(OPENSSL_NEW_VERSION "${OPENSSL_VERSION}" CACHE STRING "" FORCE)
    message(STATUS "openssl-new: using system OpenSSL ${OPENSSL_VERSION} (YETTY_OPENSSL_NEW_USE_SYSTEM=ON)")
    return()
endif()

#-----------------------------------------------------------------------------
# Fetch + import static targets
#-----------------------------------------------------------------------------
yetty_3rdparty_fetch(openssl-new _OPENSSL_NEW_DIR)

if(WIN32)
    set(_SSL_NEW_LIB_NAME    "libssl.lib")
    set(_CRYPTO_NEW_LIB_NAME "libcrypto.lib")
else()
    set(_SSL_NEW_LIB_NAME    "libssl.a")
    set(_CRYPTO_NEW_LIB_NAME "libcrypto.a")
endif()

set(_SSL_NEW_LIB_PATH    "${_OPENSSL_NEW_DIR}/lib/${_SSL_NEW_LIB_NAME}")
set(_CRYPTO_NEW_LIB_PATH "${_OPENSSL_NEW_DIR}/lib/${_CRYPTO_NEW_LIB_NAME}")
set(_OPENSSL_NEW_INC_DIR "${_OPENSSL_NEW_DIR}/include")

foreach(_F "${_SSL_NEW_LIB_PATH}" "${_CRYPTO_NEW_LIB_PATH}")
    if(NOT EXISTS "${_F}")
        message(FATAL_ERROR
            "openssl-new: prebuilt library not found: ${_F} — \
tarball layout changed? (check build-tools/3rdparty/openssl-new/_build.sh)")
    endif()
endforeach()
if(NOT EXISTS "${_OPENSSL_NEW_INC_DIR}/openssl/ssl.h")
    message(FATAL_ERROR
        "openssl-new: ssl.h not found in ${_OPENSSL_NEW_INC_DIR}/openssl/ — \
tarball layout changed?")
endif()

# Imported archive targets — distinct names from 1.1.1w's `crypto`/`ssl`.
add_library(crypto_new STATIC IMPORTED GLOBAL)
set_target_properties(crypto_new PROPERTIES
    IMPORTED_LOCATION             "${_CRYPTO_NEW_LIB_PATH}"
    INTERFACE_INCLUDE_DIRECTORIES "${_OPENSSL_NEW_INC_DIR}"
)

add_library(ssl_new STATIC IMPORTED GLOBAL)
set_target_properties(ssl_new PROPERTIES
    IMPORTED_LOCATION             "${_SSL_NEW_LIB_PATH}"
    INTERFACE_INCLUDE_DIRECTORIES "${_OPENSSL_NEW_INC_DIR}"
    INTERFACE_LINK_LIBRARIES      "crypto_new"
)

add_library(OpenSSL4::Crypto INTERFACE IMPORTED GLOBAL)
set_target_properties(OpenSSL4::Crypto PROPERTIES
    INTERFACE_LINK_LIBRARIES      "crypto_new"
    INTERFACE_INCLUDE_DIRECTORIES "${_OPENSSL_NEW_INC_DIR}"
)

add_library(OpenSSL4::SSL INTERFACE IMPORTED GLOBAL)
set_target_properties(OpenSSL4::SSL PROPERTIES
    INTERFACE_LINK_LIBRARIES      "ssl_new;OpenSSL4::Crypto"
    INTERFACE_INCLUDE_DIRECTORIES "${_OPENSSL_NEW_INC_DIR}"
)

file(READ "${YETTY_ROOT}/build-tools/3rdparty/openssl-new/version" _OSSL_NEW_VER)
string(STRIP "${_OSSL_NEW_VER}" _OSSL_NEW_VER)
set(OPENSSL_NEW_VERSION "${_OSSL_NEW_VER}"
    CACHE STRING "Version of the bundled OpenSSL 4.x build" FORCE)

message(STATUS "openssl-new: prebuilt v${_OSSL_NEW_VER} "
               "(${_SSL_NEW_LIB_PATH}, ${_CRYPTO_NEW_LIB_PATH})")
