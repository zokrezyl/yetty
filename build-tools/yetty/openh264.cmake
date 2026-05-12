# openh264 - H.264 video decoder (Cisco, BSD 2-Clause).
#
# Consumes a prebuilt static lib + headers from the 3rdparty release tarball
# published by build-3rdparty.yml. The from-source build for every yetty
# target (5 platform-specific Make invocations: linux/macos/android/ios/wasm)
# now lives in build-tools/3rdparty/openh264/_build.sh — see that script and
# build-tools/3rdparty/README.md for how to add platforms or bump versions.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

if(TARGET openh264)
    return()
endif()

yetty_3rdparty_fetch(openh264 _OPENH264_DIR)

# Tarball layout: lib/libopenh264.a + include/wels/codec_api.h
if(WIN32)
    set(_OPENH264_LIB_NAME "openh264.lib")
else()
    set(_OPENH264_LIB_NAME "libopenh264.a")
endif()

set(_OPENH264_LIB_PATH "${_OPENH264_DIR}/lib/${_OPENH264_LIB_NAME}")
set(_OPENH264_INCLUDE_DIR "${_OPENH264_DIR}/include")

if(NOT EXISTS "${_OPENH264_LIB_PATH}")
    message(FATAL_ERROR
        "openh264: library not found at ${_OPENH264_LIB_PATH} — \
tarball layout changed? (check build-tools/3rdparty/openh264/_build.sh)")
endif()
if(NOT EXISTS "${_OPENH264_INCLUDE_DIR}/wels/codec_api.h")
    message(FATAL_ERROR
        "openh264: codec_api.h not found in ${_OPENH264_INCLUDE_DIR}/wels/ — \
tarball layout changed?")
endif()

add_library(openh264 STATIC IMPORTED GLOBAL)
set_target_properties(openh264 PROPERTIES
    IMPORTED_LOCATION "${_OPENH264_LIB_PATH}"
    INTERFACE_INCLUDE_DIRECTORIES "${_OPENH264_INCLUDE_DIR}"
)

# Match the link-deps the from-source build set up:
#   - Unix non-Android: pthread + libstdc++
#   - Android: pthread (libc++ comes from ANDROID_STL=c++_static)
#   - Apple/Windows/wasm: nothing extra needed (toolchain handles libc++/pthread)
if(UNIX AND NOT ANDROID AND NOT APPLE AND NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    set_target_properties(openh264 PROPERTIES
        INTERFACE_LINK_LIBRARIES "Threads::Threads;stdc++"
    )
elseif(ANDROID)
    find_package(Threads REQUIRED)
    set_target_properties(openh264 PROPERTIES
        INTERFACE_LINK_LIBRARIES "Threads::Threads"
    )
endif()

message(STATUS "openh264: prebuilt v${YETTY_3RDPARTY_openh264_VERSION} (${_OPENH264_LIB_PATH})")
