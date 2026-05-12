# BBL (Berkeley Boot Loader) for TinyEMU
#
# Downloads BBL from Bellard's JSLinux
#
# License: BSD
# Source: https://bellard.org/jslinux/
#

include_guard(GLOBAL)

# URL
set(BBL_URL "https://bellard.org/jslinux/bbl64.bin")

# Output path
set(BBL_OUTPUT_DIR "${CMAKE_BINARY_DIR}/assets/tinyemu" CACHE PATH "BBL output directory")

#-----------------------------------------------------------------------------
# bbl_download()
#
# Downloads BBL bootloader for RISC-V
# Output: ${BBL_OUTPUT_DIR}/bbl64.bin
#-----------------------------------------------------------------------------
function(bbl_download)
    set(_BBL_FILE "${BBL_OUTPUT_DIR}/bbl64.bin")

    file(MAKE_DIRECTORY ${BBL_OUTPUT_DIR})

    if(NOT EXISTS "${_BBL_FILE}")
        message(STATUS "Downloading BBL bootloader...")
        file(DOWNLOAD
            ${BBL_URL}
            ${_BBL_FILE}
            SHOW_PROGRESS
            STATUS _DL_STATUS
        )
        list(GET _DL_STATUS 0 _DL_CODE)
        if(NOT _DL_CODE EQUAL 0)
            message(FATAL_ERROR "Failed to download BBL: ${_DL_STATUS}")
        endif()
    endif()

    # Export path
    set(TINYEMU_BBL_PATH "${_BBL_FILE}" CACHE FILEPATH "" FORCE)

    message(STATUS "BBL bootloader: ${_BBL_FILE}")
endfunction()
