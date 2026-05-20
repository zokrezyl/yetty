# libopus — Opus audio codec reference encoder/decoder (xiph, BSD 3-Clause).
#
# Royalty-free, IETF RFC 6716. Wire codec for the v2 yvideo audio path —
# packets framed inside the `audio_stream` buffer. Decoder is used inside
# src/yetty/yacodec/opus-decoder.c. Encoder isn't compiled out at build
# time (opus's cmake bundles both) but we link only the decode surface
# from C — the encoder symbols remain dead-code-eliminated by the linker.
#
# Consumes a prebuilt static lib + headers from the 3rdparty release
# tarball published by build-3rdparty-libopus.yml. The from-source build
# for every yetty target lives in build-tools/3rdparty/libopus/_build.sh
# — see that script and build-tools/3rdparty/README.md for how to add
# platforms or bump versions.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET libopus)
    return()
endif()

yetty_3rdparty_fetch(libopus _LIBOPUS_DIR)

# Tarball layout: lib/libopus.a + include/opus/{opus.h,opus_defines.h,...}
if(WIN32)
    set(_LIBOPUS_LIB_NAME "opus.lib")
else()
    set(_LIBOPUS_LIB_NAME "libopus.a")
endif()

set(_LIBOPUS_LIB_PATH "${_LIBOPUS_DIR}/lib/${_LIBOPUS_LIB_NAME}")
set(_LIBOPUS_INCLUDE_DIR "${_LIBOPUS_DIR}/include")

if(NOT EXISTS "${_LIBOPUS_LIB_PATH}")
    message(FATAL_ERROR
        "libopus: library not found at ${_LIBOPUS_LIB_PATH} — \
tarball layout changed? (check build-tools/3rdparty/libopus/_build.sh)")
endif()
if(NOT EXISTS "${_LIBOPUS_INCLUDE_DIR}/opus/opus.h")
    message(FATAL_ERROR
        "libopus: opus.h not found in ${_LIBOPUS_INCLUDE_DIR}/opus/ — \
tarball layout changed?")
endif()

add_library(libopus STATIC IMPORTED GLOBAL)
set_target_properties(libopus PROPERTIES
    IMPORTED_LOCATION "${_LIBOPUS_LIB_PATH}"
    # Both `<opus/opus.h>` and `<opus.h>` show up in the wild — keep both
    # search roots so consumers can use either form.
    INTERFACE_INCLUDE_DIRECTORIES "${_LIBOPUS_INCLUDE_DIR};${_LIBOPUS_INCLUDE_DIR}/opus"
)

# libm only — libopus is pure C, no threads needed for the decode path.
# Linux/Android pull libm explicitly; Apple/Windows/wasm get it from
# the toolchain's libc.
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    set_target_properties(libopus PROPERTIES
        INTERFACE_LINK_LIBRARIES "m"
    )
endif()

message(STATUS "libopus: prebuilt v${YETTY_3RDPARTY_libopus_VERSION} (${_LIBOPUS_LIB_PATH})")
