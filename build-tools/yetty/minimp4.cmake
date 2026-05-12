# minimp4 — header-only minimalistic MP4 demuxer (MIT, lieff).
#
# Consumes a prebuilt noarch tarball (just minimp4.h) from the 3rdparty
# release published by build-3rdparty-minimp4.yml.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

if(TARGET minimp4)
    return()
endif()

yetty_3rdparty_fetch(minimp4 _MINIMP4_DIR)

if(NOT EXISTS "${_MINIMP4_DIR}/include/minimp4.h")
    message(FATAL_ERROR
        "minimp4: minimp4.h not found in ${_MINIMP4_DIR}/include/ — tarball layout changed?")
endif()

add_library(minimp4 INTERFACE)
target_include_directories(minimp4 INTERFACE "${_MINIMP4_DIR}/include")

message(STATUS "minimp4: prebuilt @${YETTY_3RDPARTY_minimp4_VERSION} (${_MINIMP4_DIR}/include/minimp4.h)")
