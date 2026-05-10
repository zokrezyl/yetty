# 3rdparty-fetch.cmake
#
# Downloads a per-library prebuilt tarball published by
# .github/workflows/build-3rdparty-<lib>.yml, attached to the
# `lib-<lib>-<libver>` GitHub release. Extracts into the build tree as an
# imported-static-target consumer-side stub.
#
# Per-lib version is read from build-tools/3rdparty/<lib>/version — the
# single source of truth used for both upstream fetch (in _build.sh) and
# tarball naming. There is no global 3rdparty version pin.
#
# Output layout (one subdir per library):
#   ${CMAKE_BINARY_DIR}/3rdparty/<lib>/lib/...
#   ${CMAKE_BINARY_DIR}/3rdparty/<lib>/include/...
#
# URL override: YETTY_3RDPARTY_URL_BASE (default: GitHub releases for the
# upstream repo).

include_guard(GLOBAL)

#-----------------------------------------------------------------------------
# Resolve URL base + cache dir (lib-agnostic)
#-----------------------------------------------------------------------------
if(NOT DEFINED YETTY_3RDPARTY_URL_BASE)
    if(DEFINED ENV{YETTY_3RDPARTY_URL_BASE})
        set(YETTY_3RDPARTY_URL_BASE "$ENV{YETTY_3RDPARTY_URL_BASE}")
    else()
        set(YETTY_3RDPARTY_URL_BASE "https://github.com/zokrezyl/yetty/releases/download")
    endif()
endif()
set(YETTY_3RDPARTY_URL_BASE "${YETTY_3RDPARTY_URL_BASE}"
    CACHE STRING "Base URL for 3rdparty downloads")

if(NOT DEFINED YETTY_3RDPARTY_CACHE_DIR)
    if(DEFINED ENV{YETTY_3RDPARTY_CACHE_DIR})
        set(YETTY_3RDPARTY_CACHE_DIR "$ENV{YETTY_3RDPARTY_CACHE_DIR}")
    else()
        set(YETTY_3RDPARTY_CACHE_DIR "$ENV{HOME}/.cache/yetty/3rdparty")
    endif()
endif()

set(YETTY_3RDPARTY_OUTPUT_DIR "${CMAKE_BINARY_DIR}/3rdparty")
file(MAKE_DIRECTORY "${YETTY_3RDPARTY_CACHE_DIR}")
file(MAKE_DIRECTORY "${YETTY_3RDPARTY_OUTPUT_DIR}")

#-----------------------------------------------------------------------------
# yetty_3rdparty_target_platform(OUT_VAR)
#
# Maps the active build target to the <platform> slug used in the tarball
# filename. Must match the TARGET_PLATFORM values understood by every
# build-tools/3rdparty/<lib>/build.sh wrapper.
#-----------------------------------------------------------------------------
function(yetty_3rdparty_target_platform OUT_VAR)
    if(YETTY_ANDROID OR ANDROID)
        if(ANDROID_ABI STREQUAL "x86_64")
            set(_P "android-x86_64")
        else()
            set(_P "android-arm64-v8a")
        endif()
    elseif(YETTY_TVOS OR CMAKE_SYSTEM_NAME STREQUAL "tvOS")
        # tvOS check before iOS — tvOS targets set YETTY_IOS=1 internally to
        # share the iOS Objective-C platform sources, so YETTY_IOS alone is
        # not enough to disambiguate.
        if(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
            set(_P "tvos-x86_64")
        else()
            set(_P "tvos-arm64")
        endif()
    elseif(YETTY_IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
        if(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
            set(_P "ios-x86_64")
        else()
            set(_P "ios-arm64")
        endif()
    elseif(EMSCRIPTEN)
        # yetty.wasm is single-threaded — TinyEMU now runs in its own
        # iframe wasm (see tinyemu-iframe.cmake) so we no longer need
        # -pthread / --shared-memory in the parent. Pull the non-mt
        # webasm variant; the -mt prebuilts have atomics+bulk-memory
        # features that wasm-ld rejects when the parent lacks
        # shared-memory.
        set(_P "webasm")
    elseif(APPLE)
        if(CMAKE_OSX_ARCHITECTURES)
            set(_ARCHS "${CMAKE_OSX_ARCHITECTURES}")
        else()
            set(_ARCHS "${CMAKE_HOST_SYSTEM_PROCESSOR}")
        endif()
        if(_ARCHS MATCHES "x86_64")
            set(_P "macos-x86_64")
        else()
            set(_P "macos-arm64")
        endif()
    elseif(WIN32)
        set(_P "windows-x86_64")
    else()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            set(_P "linux-aarch64")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64|riscv")
            set(_P "linux-riscv64")
        else()
            set(_P "linux-x86_64")
        endif()
    endif()
    set(${OUT_VAR} "${_P}" PARENT_SCOPE)
endfunction()

#-----------------------------------------------------------------------------
# yetty_3rdparty_fetch(LIB_NAME [DEST_VAR])
#
# Reads build-tools/3rdparty/<LIB_NAME>/version, downloads
#   ${URL_BASE}/lib-<LIB_NAME>-<libver>/<LIB_NAME>-<platform>-<libver>.tar.gz
# into the cache, extracts into ${OUTPUT_DIR}/<LIB_NAME>/. Sets DEST_VAR
# (if provided) to that directory in the caller's scope. Stamp-guarded:
# re-extracts only if the version changed.
#-----------------------------------------------------------------------------
function(yetty_3rdparty_fetch LIB_NAME)
    set(_LIB_DIR  "${YETTY_ROOT}/build-tools/3rdparty/${LIB_NAME}")
    set(_VER_FILE "${_LIB_DIR}/version")
    if(NOT EXISTS "${_VER_FILE}")
        message(FATAL_ERROR
            "3rdparty-fetch(${LIB_NAME}): version file not found: ${_VER_FILE}")
    endif()
    file(READ "${_VER_FILE}" _LIBVER)
    string(STRIP "${_LIBVER}" _LIBVER)
    if(NOT _LIBVER)
        message(FATAL_ERROR "3rdparty-fetch(${LIB_NAME}): ${_VER_FILE} is empty")
    endif()

    # Platform-independent assets are marked with a `.noarch` file next to
    # the version. Their tarballs omit the target slug — same artifact for
    # every host (e.g. cdb, linux kernel, opensbi firmware, alpine ext4).
    if(EXISTS "${_LIB_DIR}/.noarch")
        set(_FILENAME "${LIB_NAME}-${_LIBVER}.tar.gz")
        set(_PLAT_DESC "noarch")
    else()
        yetty_3rdparty_target_platform(_PLATFORM)
        if(NOT _PLATFORM)
            message(FATAL_ERROR
                "3rdparty-fetch(${LIB_NAME}): no target platform mapped — \
unsupported build configuration?")
        endif()
        set(_FILENAME "${LIB_NAME}-${_PLATFORM}-${_LIBVER}.tar.gz")
        set(_PLAT_DESC "${_PLATFORM}")
    endif()
    set(_TARBALL "${YETTY_3RDPARTY_CACHE_DIR}/${_FILENAME}")
    set(_TAG "lib-${LIB_NAME}-${_LIBVER}")
    set(_URL "${YETTY_3RDPARTY_URL_BASE}/${_TAG}/${_FILENAME}")
    set(_DEST "${YETTY_3RDPARTY_OUTPUT_DIR}/${LIB_NAME}")
    set(_STAMP "${_DEST}/.fetched-${_LIBVER}")

    if(NOT EXISTS "${_STAMP}")
        if(NOT EXISTS "${_TARBALL}")
            message(STATUS "3rdparty-fetch: downloading ${_FILENAME}")
            file(DOWNLOAD "${_URL}" "${_TARBALL}"
                SHOW_PROGRESS
                STATUS _DL_STATUS
                TLS_VERIFY ON
            )
            list(GET _DL_STATUS 0 _DL_CODE)
            if(NOT _DL_CODE EQUAL 0)
                file(REMOVE "${_TARBALL}")
                message(FATAL_ERROR
                    "3rdparty-fetch: download failed for ${_URL}: ${_DL_STATUS}")
            endif()
        endif()

        # Wipe any stale extraction (different version) before re-extracting.
        if(EXISTS "${_DEST}")
            file(REMOVE_RECURSE "${_DEST}")
        endif()
        file(MAKE_DIRECTORY "${_DEST}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf "${_TARBALL}"
            WORKING_DIRECTORY "${_DEST}"
            RESULT_VARIABLE _TAR_RESULT
        )
        if(NOT _TAR_RESULT EQUAL 0)
            message(FATAL_ERROR
                "3rdparty-fetch: failed to extract ${_TARBALL} into ${_DEST}")
        endif()

        # Auto-decompress any *.br files side-by-side (keep the .br too —
        # the embed pipeline reads it pre-compressed; the runtime path
        # mode reads the decompressed raw file). Brotli-guarded; if brotli
        # isn't installed and a consumer needs the raw file, that consumer
        # will report a missing-file error with a clear path.
        file(GLOB_RECURSE _BR_FILES "${_DEST}/*.br")
        if(_BR_FILES)
            find_program(BROTLI_EXECUTABLE brotli)
            if(BROTLI_EXECUTABLE)
                foreach(_BR ${_BR_FILES})
                    string(REGEX REPLACE "\\.br$" "" _RAW "${_BR}")
                    if(NOT EXISTS "${_RAW}")
                        execute_process(
                            COMMAND "${BROTLI_EXECUTABLE}" -d -k -f -o "${_RAW}" "${_BR}"
                            RESULT_VARIABLE _BR_RESULT
                        )
                        if(NOT _BR_RESULT EQUAL 0)
                            message(WARNING
                                "3rdparty-fetch(${LIB_NAME}): failed to decompress ${_BR}")
                        endif()
                    endif()
                endforeach()
            endif()
        endif()

        file(WRITE "${_STAMP}" "${_LIBVER}\n")
        message(STATUS "3rdparty-fetch: ${LIB_NAME} ${_LIBVER} ready (${_PLAT_DESC})")
    endif()

    if(ARGC GREATER 1)
        set(${ARGV1} "${_DEST}" PARENT_SCOPE)
    endif()
    set(YETTY_3RDPARTY_${LIB_NAME}_VERSION "${_LIBVER}" CACHE INTERNAL "")
    set(YETTY_3RDPARTY_${LIB_NAME}_DIR     "${_DEST}"   CACHE INTERNAL "")
endfunction()
