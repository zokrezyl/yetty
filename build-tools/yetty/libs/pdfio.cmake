# pdfio — lightweight C library for PDF parsing.
#
# Consumes a prebuilt static lib + headers from the 3rdparty release
# tarball published by build-3rdparty-pdfio.yml. The from-source build
# (compiles 16 .c files directly — pdfio isn't a CMake project upstream)
# lives in build-tools/3rdparty/pdfio/_build.sh.
#
# Exposed target: `pdfio_lib` — IMPORTED static archive.
#
# Note on zlib: the prebuilt archive holds UNRESOLVED zlib references
# (FlateDecode stream filter). Consumers must satisfy them. We add
# ZLIB::ZLIB to INTERFACE_LINK_LIBRARIES so anything linking pdfio_lib
# pulls in zlib transitively. Same model libcurl/libssh2 use for their
# TLS backend deps.
#
# Note on emscripten: the producer bakes in a getrandom() shim source
# (pdfio uses it for AES IV generation; emscripten ships <sys/random.h>
# but no getrandom). No extra consumer-side source needed.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET pdfio_lib)
    return()
endif()

# zlib must be resolved before us — pdfio's archive depends on its
# symbols at link time.
include(${YETTY_ROOT}/build-tools/yetty/libs/zlib.cmake)

yetty_3rdparty_fetch(pdfio _PDFIO_DIR)

if(WIN32)
    set(_PDFIO_LIB "${_PDFIO_DIR}/lib/libpdfio.lib")
else()
    set(_PDFIO_LIB "${_PDFIO_DIR}/lib/libpdfio.a")
endif()

if(NOT EXISTS "${_PDFIO_LIB}")
    message(FATAL_ERROR
        "pdfio: archive not found at ${_PDFIO_LIB} — \
tarball layout changed? (check build-tools/3rdparty/pdfio/_build.sh)")
endif()
if(NOT EXISTS "${_PDFIO_DIR}/include/pdfio.h")
    message(FATAL_ERROR
        "pdfio: pdfio.h not found in ${_PDFIO_DIR}/include/ — tarball layout changed?")
endif()

add_library(pdfio_lib STATIC IMPORTED GLOBAL)
set_target_properties(pdfio_lib PROPERTIES
    IMPORTED_LOCATION "${_PDFIO_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_PDFIO_DIR}/include"
    INTERFACE_LINK_LIBRARIES "ZLIB::ZLIB"
)

message(STATUS "pdfio: prebuilt v${YETTY_3RDPARTY_pdfio_VERSION} (${_PDFIO_LIB})")
