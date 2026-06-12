# libssh2 for webasm — from-source emscripten build, mbedTLS crypto backend.
#
# The desktop/iOS/Android platforms consume a prebuilt openssl-backed
# libssh2 archive from the 3rdparty release pipeline (libssh2.cmake).
# No such prebuilt exists for webasm, and openssl is a heavy port to
# wasm — mbedTLS compiles cleanly under emscripten and is the crypto
# backend libssh2 upstream recommends for embedded targets, so the
# webasm build compiles both from source at build time via
# ExternalProject. (Candidate for the 3rdparty prebuilt pipeline later;
# the from-source build adds ~1 min to a cold webasm build and nothing
# to warm rebuilds.)
#
# Exposed target: `libssh2_webasm` — INTERFACE target carrying the
# include dir and the static archives (libssh2 + mbedcrypto; libssh2's
# mbedTLS backend uses only the crypto library, not TLS/X.509).
#
# NOTE: mbedTLS is built with MBEDTLS_FATAL_WARNINGS=OFF — recent
# clang's -Wunterminated-string-initialization trips on mbedtls 3.6.x
# TLS1.3 label tables under -Werror (in code libssh2 doesn't even use).

include_guard(GLOBAL)
include(ExternalProject)

set(_SSH_WASM_PREFIX ${CMAKE_BINARY_DIR}/libssh2-webasm)

ExternalProject_Add(mbedtls_webasm_build
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.2/mbedtls-3.6.2.tar.bz2
    URL_HASH SHA256=8b54fb9bcf4d5a7078028e0520acddefb7900b3e66fec7f7175bb5b7d85ccdca
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    PREFIX ${_SSH_WASM_PREFIX}/mbedtls
    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${_SSH_WASM_PREFIX}/prefix
        -DENABLE_TESTING=OFF
        -DENABLE_PROGRAMS=OFF
        -DMBEDTLS_FATAL_WARNINGS=OFF
    BUILD_BYPRODUCTS
        ${_SSH_WASM_PREFIX}/prefix/lib/libmbedcrypto.a
)

ExternalProject_Add(libssh2_webasm_build
    URL https://github.com/libssh2/libssh2/releases/download/libssh2-1.11.1/libssh2-1.11.1.tar.gz
    URL_HASH SHA256=d9ec76cbe34db98eec3539fe2c899d26b0c837cb3eb466a56b0f109cabf658f7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    PREFIX ${_SSH_WASM_PREFIX}/libssh2
    DEPENDS mbedtls_webasm_build
    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${_SSH_WASM_PREFIX}/prefix
        -DCRYPTO_BACKEND=mbedTLS
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_STATIC_LIBS=ON
        -DBUILD_EXAMPLES=OFF
        -DBUILD_TESTING=OFF
        -DENABLE_ZLIB_COMPRESSION=OFF
        -DCMAKE_PREFIX_PATH=${_SSH_WASM_PREFIX}/prefix
        -DMBEDTLS_INCLUDE_DIR=${_SSH_WASM_PREFIX}/prefix/include
        -DMBEDCRYPTO_LIBRARY=${_SSH_WASM_PREFIX}/prefix/lib/libmbedcrypto.a
        -DMBEDTLS_LIBRARY=${_SSH_WASM_PREFIX}/prefix/lib/libmbedtls.a
        -DMBEDX509_LIBRARY=${_SSH_WASM_PREFIX}/prefix/lib/libmbedx509.a
    BUILD_BYPRODUCTS
        ${_SSH_WASM_PREFIX}/prefix/lib/libssh2.a
)

# The include dir must exist at configure time for INTERFACE_INCLUDE_DIRECTORIES.
file(MAKE_DIRECTORY ${_SSH_WASM_PREFIX}/prefix/include)

add_library(libssh2_webasm INTERFACE)
add_dependencies(libssh2_webasm libssh2_webasm_build)
target_include_directories(libssh2_webasm INTERFACE ${_SSH_WASM_PREFIX}/prefix/include)
target_link_libraries(libssh2_webasm INTERFACE
    ${_SSH_WASM_PREFIX}/prefix/lib/libssh2.a
    ${_SSH_WASM_PREFIX}/prefix/lib/libmbedcrypto.a
)
