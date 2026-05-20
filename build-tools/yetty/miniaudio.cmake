# miniaudio — single-header cross-platform audio I/O (mackron, MIT-0
# / public-domain dual). Covers WASAPI / DirectSound / WinMM on Windows,
# CoreAudio / AudioUnit on Apple, ALSA / PulseAudio / PipeWire-via-PA /
# JACK / sndio on Linux, AAudio / OpenSL ES on Android, WebAudio on
# Emscripten — backend picked at compile time inside the header.
#
# Consumes a prebuilt per-platform tarball (just miniaudio.h + LICENSE)
# from the 3rdparty release published by build-3rdparty-miniaudio.yml.
# The header is identical across platforms but the file is still named
# `miniaudio-<platform>-<version>.tar.gz` so the cmake fetcher resolves
# it uniformly with every other 3rdparty lib.
#
# Header-only: exactly one TU in the consumer must `#define
# MINIAUDIO_IMPLEMENTATION` before `#include <miniaudio.h>`. Yetty does
# this in src/platform/audio/miniaudio-device.c — that file is the
# single owner of the implementation symbols.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET miniaudio)
    return()
endif()

yetty_3rdparty_fetch(miniaudio _MINIAUDIO_DIR)

if(NOT EXISTS "${_MINIAUDIO_DIR}/include/miniaudio.h")
    message(FATAL_ERROR
        "miniaudio: miniaudio.h not found in ${_MINIAUDIO_DIR}/include/ — tarball layout changed?")
endif()

add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE "${_MINIAUDIO_DIR}/include")

# Per-platform link deps the header needs at the system-call level:
#   - Linux: pthread + math + dl (dl: dynamic-load of pulseaudio/jack at runtime)
#   - macOS/iOS/tvOS: CoreAudio / AudioUnit / AudioToolbox / CoreFoundation
#   - Android: OpenSLES + log (AAudio resolved via dlsym; libdl already
#     in Bionic's default link set)
#   - Windows: nothing extra (COM/WASAPI imports come from the toolchain)
#   - Emscripten: nothing extra (WebAudio uses emscripten_* intrinsics)
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio INTERFACE Threads::Threads m ${CMAKE_DL_LIBS})
elseif(APPLE)
    # macOS ships AudioUnit as a separate top-level framework; on
    # iOS/tvOS/watchOS the AudioUnit headers live inside AudioToolbox
    # and the standalone framework doesn't exist (linker errors with
    # "framework 'AudioUnit' not found"). Skip it on the embedded
    # Apple OSes; AudioToolbox alone is enough for miniaudio's
    # CoreAudio backend there.
    find_library(_MA_COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(_MA_AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    find_library(_MA_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(miniaudio INTERFACE
        ${_MA_COREAUDIO_FRAMEWORK}
        ${_MA_AUDIOTOOLBOX_FRAMEWORK}
        ${_MA_COREFOUNDATION_FRAMEWORK}
    )
    if(NOT (YETTY_IOS OR YETTY_TVOS
            OR CMAKE_SYSTEM_NAME STREQUAL "iOS"
            OR CMAKE_SYSTEM_NAME STREQUAL "tvOS"
            OR CMAKE_SYSTEM_NAME STREQUAL "watchOS"))
        find_library(_MA_AUDIOUNIT_FRAMEWORK AudioUnit REQUIRED)
        target_link_libraries(miniaudio INTERFACE ${_MA_AUDIOUNIT_FRAMEWORK})
    endif()
elseif(ANDROID)
    target_link_libraries(miniaudio INTERFACE OpenSLES log)
endif()

message(STATUS "miniaudio: prebuilt @${YETTY_3RDPARTY_miniaudio_VERSION} (${_MINIAUDIO_DIR}/include/miniaudio.h)")
