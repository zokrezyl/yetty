# libcss — NetSurf's MIT-licensed CSS cascade, plus its two MIT deps
# (libparserutils + libwapcaplet).
#
# Consumes the prebuilt static-lib + headers tarball published by
# build-3rdparty-libcss.yml. The tarball is built from the VENDORED
# (locally patched) tree at src/libcss — see src/libcss/README.md; when
# that source changes, bump the -pN suffix in
# build-tools/3rdparty/libcss/version and cut a new lib-libcss-* tag.
#
# Exposed targets (names unchanged from the old in-tree build, so
# consumers — src/yetty/ybrowser — need no edits):
#   libwapcaplet_static     — interned-string pool
#   libparserutils_static   — tokenizer / input stream used by libcss
#   libcss_static           — the CSS cascade (links the other two)
#
# Real builds exist for linux-* and macos-* only (same coverage the old
# in-tree build had); the other platforms' tarballs carry an UNSUPPORTED
# marker, detected below. Consumers guard with `if(TARGET libcss_static)`
# and fall back to the built-in lexbor-CSS path when it is absent.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET libcss_static)
    return()
endif()

yetty_3rdparty_fetch(libcss _LIBCSS_DIR)

# Placeholder tarball for platforms libcss isn't built for (android,
# ios/tvos, webasm, windows — libparserutils needs iconv, and ybrowser
# uses its lexbor-CSS fallback there anyway). Skip silently.
if(EXISTS "${_LIBCSS_DIR}/UNSUPPORTED")
    yetty_3rdparty_target_platform(_LIBCSS_PLATFORM)
    message(STATUS
        "libcss: ${_LIBCSS_PLATFORM} not supported — skipping \
(lexbor-CSS fallback; see ${_LIBCSS_DIR}/UNSUPPORTED)")
    return()
endif()

# Sanity-check tarball layout (real builds only).
foreach(_LIBCSS_FILE
    "${_LIBCSS_DIR}/lib/libcss.a"
    "${_LIBCSS_DIR}/lib/libparserutils.a"
    "${_LIBCSS_DIR}/lib/libwapcaplet.a"
    "${_LIBCSS_DIR}/include/libcss/libcss.h"
    "${_LIBCSS_DIR}/include/parserutils/parserutils.h"
    "${_LIBCSS_DIR}/include/libwapcaplet/libwapcaplet.h")
    if(NOT EXISTS "${_LIBCSS_FILE}")
        message(FATAL_ERROR
            "libcss: missing ${_LIBCSS_FILE} — tarball layout changed? \
(check build-tools/3rdparty/libcss/_build.sh)")
    endif()
endforeach()

add_library(libwapcaplet_static STATIC IMPORTED GLOBAL)
set_target_properties(libwapcaplet_static PROPERTIES
    IMPORTED_LOCATION "${_LIBCSS_DIR}/lib/libwapcaplet.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_LIBCSS_DIR}/include")

add_library(libparserutils_static STATIC IMPORTED GLOBAL)
set_target_properties(libparserutils_static PROPERTIES
    IMPORTED_LOCATION "${_LIBCSS_DIR}/lib/libparserutils.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_LIBCSS_DIR}/include")
# libparserutils calls iconv_open/close (src/input/filter.c). On Linux
# iconv is in glibc; macOS ships it as a separate libiconv that must be
# linked.
if(APPLE)
    set_target_properties(libparserutils_static PROPERTIES
        INTERFACE_LINK_LIBRARIES iconv)
endif()

add_library(libcss_static STATIC IMPORTED GLOBAL)
set_target_properties(libcss_static PROPERTIES
    IMPORTED_LOCATION "${_LIBCSS_DIR}/lib/libcss.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_LIBCSS_DIR}/include"
    INTERFACE_LINK_LIBRARIES "libparserutils_static;libwapcaplet_static")

message(STATUS "libcss: prebuilt v${YETTY_3RDPARTY_libcss_VERSION}")
