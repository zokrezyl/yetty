# yetty-asset-fetch.cmake
#
# Sister to 3rdparty-fetch.cmake, but for *first-party* yetty release
# assets that don't follow the `lib-<name>-<ver>` convention. Used today
# for `yetty-tools-riscv-<ver>` (cross-built riscv64 binaries + repo
# checkout, packed as a brotli ext4 disk image).
#
# Layout:
#   version file:  build-tools/yemu/<NAME>/version
#   tag:           <NAME>-<VER>          (no `lib-` prefix)
#   tarball:       <NAME>-<VER>.tar.gz   (noarch — same artifact for every host)
#   release URL:   ${URL_BASE}/<NAME>-<VER>/<NAME>-<VER>.tar.gz
#   extract dir:   ${CMAKE_BINARY_DIR}/yetty-assets/<NAME>/
#
# Reuses the same URL_BASE + cache dir + brotli auto-decompress as
# 3rdparty-fetch (those env vars already exist; we just point at the
# yetty repo's releases).

include_guard(GLOBAL)

# Reuse the URL base + cache dir from 3rdparty-fetch — they're both
# scoped to "the zokrezyl/yetty repo's releases", and pulling them in
# means the same YETTY_3RDPARTY_URL_BASE override works for both.
include(${CMAKE_CURRENT_LIST_DIR}/3rdparty-fetch.cmake)

set(YETTY_ASSET_OUTPUT_DIR "${CMAKE_BINARY_DIR}/yetty-assets")
file(MAKE_DIRECTORY "${YETTY_ASSET_OUTPUT_DIR}")

#-----------------------------------------------------------------------------
# yetty_asset_fetch(NAME [DEST_VAR])
#
# Reads build-tools/yemu/<NAME>/version, downloads
#   ${URL_BASE}/<NAME>-<VER>/<NAME>-<VER>.tar.gz
# into the cache, extracts into ${YETTY_ASSET_OUTPUT_DIR}/<NAME>/. Sets
# DEST_VAR (if provided) and the cache var YETTY_ASSET_<NAME>_DIR /
# YETTY_ASSET_<NAME>_VERSION in the parent scope. Stamp-guarded so a
# re-configure doesn't re-extract unless the version changed.
#-----------------------------------------------------------------------------
function(yetty_asset_fetch NAME)
    set(_VER_FILE "${YETTY_ROOT}/build-tools/yemu/${NAME}/version")
    if(NOT EXISTS "${_VER_FILE}")
        message(FATAL_ERROR
            "yetty-asset-fetch(${NAME}): version file not found: ${_VER_FILE}")
    endif()
    file(READ "${_VER_FILE}" _VER)
    string(STRIP "${_VER}" _VER)
    if(NOT _VER)
        message(FATAL_ERROR "yetty-asset-fetch(${NAME}): ${_VER_FILE} is empty")
    endif()

    set(_FILENAME "${NAME}-${_VER}.tar.gz")
    set(_TAG      "${NAME}-${_VER}")
    set(_URL      "${YETTY_3RDPARTY_URL_BASE}/${_TAG}/${_FILENAME}")
    set(_TARBALL  "${YETTY_3RDPARTY_CACHE_DIR}/${_FILENAME}")
    set(_DEST     "${YETTY_ASSET_OUTPUT_DIR}/${NAME}")
    set(_STAMP    "${_DEST}/.fetched-${_VER}")

    if(NOT EXISTS "${_STAMP}")
        if(NOT EXISTS "${_TARBALL}")
            message(STATUS "yetty-asset-fetch: downloading ${_FILENAME}")
            file(DOWNLOAD "${_URL}" "${_TARBALL}"
                SHOW_PROGRESS
                STATUS _DL_STATUS
                TLS_VERIFY ON
            )
            list(GET _DL_STATUS 0 _DL_CODE)
            if(NOT _DL_CODE EQUAL 0)
                file(REMOVE "${_TARBALL}")
                # Hard-fail. The old warn-and-continue path produced builds
                # that linked cleanly but failed at runtime — tinyemu cfg
                # still references drive1 (yetty-riscv-disk.img), so a
                # missing release silently broke every webasm VM start.
                # Fail at configure time instead so the missing release is
                # obvious. To unblock locally: bump
                # build-tools/yemu/${NAME}/version back to the last released
                # tag, or cut the missing release.
                message(FATAL_ERROR
                    "yetty-asset-fetch(${NAME}): download failed for ${_URL} (${_DL_STATUS}).\n"
                    "  Pinned version: ${_VER}\n"
                    "  Check that the GitHub release '${_TAG}' exists and the build CI succeeded.")
            endif()
        endif()

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
                "yetty-asset-fetch: failed to extract ${_TARBALL} into ${_DEST}")
        endif()

        # Auto-decompress *.br files alongside (matches 3rdparty-fetch's
        # behaviour). Embed pipeline reads .br as-is; runtime path mode
        # reads the decompressed sibling.
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
                                "yetty-asset-fetch(${NAME}): failed to decompress ${_BR}")
                        endif()
                    endif()
                endforeach()
            endif()
        endif()

        file(WRITE "${_STAMP}" "${_VER}\n")
        message(STATUS "yetty-asset-fetch: ${NAME} ${_VER} ready")
    endif()

    if(ARGC GREATER 1)
        set(${ARGV1} "${_DEST}" PARENT_SCOPE)
    endif()
    set(YETTY_ASSET_${NAME}_VERSION "${_VER}"  CACHE INTERNAL "")
    set(YETTY_ASSET_${NAME}_DIR     "${_DEST}" CACHE INTERNAL "")
endfunction()
