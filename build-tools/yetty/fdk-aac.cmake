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
endif()
