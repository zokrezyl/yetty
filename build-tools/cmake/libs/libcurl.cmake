# libcurl — HTTP(S) client. Used by ycat for http(s):// fetching.
#
# Consumes a prebuilt static lib + headers from the 3rdparty release
# tarball published by build-3rdparty-libcurl.yml. The tarball was built
# statically against three other yetty 3rdparty prebuilts (see
# build-tools/3rdparty/libcurl/_build.sh):
#   - openssl-new  (TLS backend)
#   - zlib         (gzip Content-Encoding)
#   - brotli       (br   Content-Encoding)
#
# libcurl.a's unresolved zlib/brotli/openssl symbols are satisfied here
# via the matching libs/{zlib,brotli}.cmake includes — the same tarballs
# yetty's main build already consumes elsewhere. So linking the same
# yetty binary against both libcurl AND (say) freetype pulls in only one
# copy of zlib.
#
# Exposes `CURL::libcurl` (the find_package(CURL) target name yetty + cpr
# expect).
#
# WARNING / SCOPE:
# At time of writing, libssh2 still links the OLD janbar 1.1.1w openssl
# (see libs/libssh2.cmake → openssl.cmake). Linking yetty with BOTH this
# libcurl AND libssh2 in the same binary will cause OpenSSL symbol
# conflicts. Either:
#   - disable libssh2 (YETTY_ENABLE_LIB_LIBSSH2=OFF) until it migrates, OR
#   - migrate libssh2 to also use openssl-new (not done yet).

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

if(TARGET CURL::libcurl)
    return()
endif()

# zlib + brotli must be resolved before us — libcurl.a has unresolved
# symbols from both. We pull in the same prebuilts curl was linked
# against at 3rdparty-build time. Order matters only for find_package
# style consumers; the actual link order is chosen by the linker from
# INTERFACE_LINK_LIBRARIES below.
include(${YETTY_ROOT}/build-tools/cmake/libs/zlib.cmake)
include(${YETTY_ROOT}/build-tools/cmake/libs/brotli.cmake)

#-----------------------------------------------------------------------------
# Fetch libcurl prebuilt + the openssl-new prebuilt it was built against.
#-----------------------------------------------------------------------------
yetty_3rdparty_fetch(libcurl     _LIBCURL_DIR)
yetty_3rdparty_fetch(openssl-new _OSSL_DIR)

# Tarball layout: lib/libcurl.a + include/curl/*.h
if(WIN32 AND EXISTS "${_LIBCURL_DIR}/lib/curl.lib")
    set(_LIBCURL_LIB "${_LIBCURL_DIR}/lib/curl.lib")
elseif(EXISTS "${_LIBCURL_DIR}/lib/libcurl.a")
    set(_LIBCURL_LIB "${_LIBCURL_DIR}/lib/libcurl.a")
else()
    message(FATAL_ERROR
        "libcurl: no static lib found in ${_LIBCURL_DIR}/lib/ — \
tarball layout changed? (check build-tools/3rdparty/libcurl/_build.sh)")
endif()
if(NOT EXISTS "${_LIBCURL_DIR}/include/curl/curl.h")
    message(FATAL_ERROR
        "libcurl: curl.h not found in ${_LIBCURL_DIR}/include/curl/ — tarball layout changed?")
endif()

# openssl-new prebuilt: lib/libssl.a + lib/libcrypto.a (from build-3rdparty-openssl-new.yml)
set(_OSSL_SSL    "${_OSSL_DIR}/lib/libssl.a")
set(_OSSL_CRYPTO "${_OSSL_DIR}/lib/libcrypto.a")
if(WIN32)
    if(NOT EXISTS "${_OSSL_SSL}")
        set(_OSSL_SSL "${_OSSL_DIR}/lib/libssl.lib")
    endif()
    if(NOT EXISTS "${_OSSL_CRYPTO}")
        set(_OSSL_CRYPTO "${_OSSL_DIR}/lib/libcrypto.lib")
    endif()
endif()
foreach(_F "${_OSSL_SSL}" "${_OSSL_CRYPTO}")
    if(NOT EXISTS "${_F}")
        message(FATAL_ERROR
            "libcurl: openssl-new prebuilt lib missing: ${_F} — \
push the lib-openssl-new-${YETTY_3RDPARTY_openssl-new_VERSION} release first")
    endif()
endforeach()

#-----------------------------------------------------------------------------
# Imported targets
#-----------------------------------------------------------------------------
add_library(CURL::libcurl STATIC IMPORTED GLOBAL)
set_target_properties(CURL::libcurl PROPERTIES
    IMPORTED_LOCATION "${_LIBCURL_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_LIBCURL_DIR}/include"
)

# Per-platform link deps that libcurl requires.
set(_LIBCURL_DEPS "${_OSSL_SSL};${_OSSL_CRYPTO}")
if(WIN32)
    list(APPEND _LIBCURL_DEPS ws2_32 crypt32 bcrypt advapi32 user32)
elseif(APPLE)
    # Curl on Apple uses CoreFoundation/Security for SecureTransport-adjacent
    # APIs even with OpenSSL backend (timer/cert helpers).
    list(APPEND _LIBCURL_DEPS "-framework CoreFoundation" "-framework Security")
elseif(ANDROID)
    list(APPEND _LIBCURL_DEPS dl)
elseif(EMSCRIPTEN)
    # nothing extra — emscripten libc covers it.
elseif(UNIX)
    list(APPEND _LIBCURL_DEPS pthread dl)
endif()
set_target_properties(CURL::libcurl PROPERTIES
    INTERFACE_LINK_LIBRARIES "${_LIBCURL_DEPS}"
)

# find_package(CURL) compat — cpr / any other consumer reading these vars.
set(CURL_FOUND          TRUE                                  CACHE BOOL    "" FORCE)
set(CURL_INCLUDE_DIRS   "${_LIBCURL_DIR}/include"             CACHE PATH    "" FORCE)
set(CURL_LIBRARIES      CURL::libcurl                         CACHE STRING  "" FORCE)
set(CURL_VERSION_STRING "${YETTY_3RDPARTY_libcurl_VERSION}"   CACHE STRING  "" FORCE)

message(STATUS "libcurl: prebuilt v${YETTY_3RDPARTY_libcurl_VERSION} "
               "(TLS: openssl-new ${YETTY_3RDPARTY_openssl-new_VERSION})")
