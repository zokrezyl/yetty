# fdk-aac - Fraunhofer FDK AAC Codec Library
# Fraunhofer FDK AAC Codec Library license (BSD-style)
# High-quality AAC encoder/decoder from Android

CPMAddPackage(
    NAME fdk-aac
    GITHUB_REPOSITORY mstorsjo/fdk-aac
    VERSION 2.0.3
    OPTIONS
        "BUILD_SHARED_LIBS OFF"
        "BUILD_PROGRAMS OFF"
)

if(fdk-aac_ADDED)
    message(STATUS "fdk-aac: AAC encoder/decoder v2.0.3")

    if(ANDROID)
        # fdk-aac's libSBRdec includes AOSP's log/log.h and calls
        # android_errorWriteLog(), neither of which the NDK ships.
        # Point the target at a stub that no-ops the call.
        target_include_directories(fdk-aac PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/fdk-aac-android-compat)
    endif()
endif()
