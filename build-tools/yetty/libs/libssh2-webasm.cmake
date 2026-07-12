# libssh2 for webasm — prebuilt static archives, mbedTLS crypto backend.
#
# Consumes the webasm flavor of the 3rdparty libssh2 release tarball
# published by build-3rdparty-libssh2.yml. Unlike the desktop/mobile
# flavors (openssl-backed, consumed by libssh2.cmake), the webasm
# tarball is built with the mbedTLS backend and bundles
# lib/libmbedcrypto.a — an openssl-backed archive would drag
# libssl+libcrypto into yetty.wasm (a multi-MB browser-download hit for
# symbols libssh2 barely uses), while the mbedTLS backend needs only the
# crypto archive. The from-source build lives in
# build-tools/3rdparty/libssh2/_build.sh (webasm branch).
#
# Exposed target: `libssh2_webasm` — INTERFACE target carrying the
# include dir and the two static archives (libssh2 + mbedcrypto).

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET libssh2_webasm)
    return()
endif()

yetty_3rdparty_fetch(libssh2 _LIBSSH2_DIR)

foreach(_LIBSSH2_ARCHIVE lib/libssh2.a lib/libmbedcrypto.a)
    if(NOT EXISTS "${_LIBSSH2_DIR}/${_LIBSSH2_ARCHIVE}")
        message(FATAL_ERROR
            "libssh2 (webasm): ${_LIBSSH2_ARCHIVE} not found in ${_LIBSSH2_DIR} — \
tarball layout changed? (check build-tools/3rdparty/libssh2/_build.sh)")
    endif()
endforeach()
if(NOT EXISTS "${_LIBSSH2_DIR}/include/libssh2.h")
    message(FATAL_ERROR
        "libssh2 (webasm): libssh2.h not found in ${_LIBSSH2_DIR}/include/ — tarball layout changed?")
endif()

add_library(libssh2_webasm INTERFACE)
target_include_directories(libssh2_webasm INTERFACE "${_LIBSSH2_DIR}/include")
# Archive order matters for wasm-ld: libssh2 first, then the mbedcrypto
# archive that resolves its crypto symbols.
target_link_libraries(libssh2_webasm INTERFACE
    "${_LIBSSH2_DIR}/lib/libssh2.a"
    "${_LIBSSH2_DIR}/lib/libmbedcrypto.a"
)

message(STATUS "libssh2: prebuilt v${YETTY_3RDPARTY_libssh2_VERSION} (webasm, mbedTLS backend)")
