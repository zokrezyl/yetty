# miniaudio - Single-header audio playback and capture library
# Public Domain / MIT No Attribution, David Reid (mackron)
# Cross-platform audio I/O with no external dependencies

CPMAddPackage(
    NAME miniaudio
    GITHUB_REPOSITORY mackron/miniaudio
    GIT_TAG master
    DOWNLOAD_ONLY YES
)

if(miniaudio_ADDED)
    # Header-only library (STB-style: define MINIAUDIO_IMPLEMENTATION in one .c/.cpp)
    add_library(miniaudio INTERFACE)
    target_include_directories(miniaudio INTERFACE ${miniaudio_SOURCE_DIR})

    # Platform-specific audio backend dependencies
    if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
        # Linux: needs pthread and math, plus dl for dynamic loading of backends
        target_link_libraries(miniaudio INTERFACE pthread m dl)
    elseif(APPLE)
        # macOS/iOS: Core Audio frameworks
        find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
        find_library(AUDIOUNIT_FRAMEWORK AudioUnit REQUIRED)
        find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
        target_link_libraries(miniaudio INTERFACE
            ${COREAUDIO_FRAMEWORK}
            ${AUDIOUNIT_FRAMEWORK}
            ${COREFOUNDATION_FRAMEWORK}
        )
    elseif(ANDROID)
        # Android: OpenSL ES or AAudio (linked via NDK)
        target_link_libraries(miniaudio INTERFACE OpenSLES log)
    elseif(WIN32)
        # Windows: no extra libs needed (uses COM/WASAPI)
    endif()

    message(STATUS "miniaudio: Header-only audio library added")
endif()
