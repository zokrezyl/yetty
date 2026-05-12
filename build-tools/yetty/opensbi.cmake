# OpenSBI - RISC-V Open Source Supervisor Binary Interface
#
# Builds OpenSBI firmware for RISC-V 64-bit
#
# License: BSD-2-Clause
# Source: https://github.com/riscv-software-src/opensbi
#

include_guard(GLOBAL)

# Version
set(OPENSBI_VERSION "1.4" CACHE STRING "OpenSBI version")

# URL
set(OPENSBI_URL "https://github.com/riscv-software-src/opensbi/archive/refs/tags/v${OPENSBI_VERSION}.tar.gz")

# Output path (shared RISC-V runtime assets — used by both TinyEMU and QEMU)
set(OPENSBI_OUTPUT_DIR "${CMAKE_BINARY_DIR}/assets/yemu" CACHE PATH "OpenSBI output directory")

# Cross-compiler (same as kernel)
set(OPENSBI_CROSS_COMPILE "riscv64-linux-gnu-" CACHE STRING "Cross-compiler prefix for RISC-V")

# Number of parallel jobs
include(ProcessorCount)
ProcessorCount(NCPU)
if(NCPU EQUAL 0)
    set(NCPU 4)
endif()

#-----------------------------------------------------------------------------
# opensbi_build()
#
# Downloads and builds OpenSBI for RISC-V at CONFIGURE time
# Output: ${OPENSBI_OUTPUT_DIR}/opensbi-fw_jump.elf
#-----------------------------------------------------------------------------
function(opensbi_build)
    set(_OUTPUT "${OPENSBI_OUTPUT_DIR}/opensbi-fw_jump.elf")
    set(_SOURCE_DIR "${CMAKE_BINARY_DIR}/opensbi-${OPENSBI_VERSION}")
    set(_TARBALL "${CMAKE_BINARY_DIR}/downloads/opensbi-${OPENSBI_VERSION}.tar.gz")

    # Skip if already built
    if(EXISTS "${_OUTPUT}")
        message(STATUS "OpenSBI v${OPENSBI_VERSION} already built: ${_OUTPUT}")
        set(TINYEMU_OPENSBI_PATH "${_OUTPUT}" CACHE FILEPATH "" FORCE)
        return()
    endif()

    # Check for cross-compiler
    find_program(RISCV_GCC "${OPENSBI_CROSS_COMPILE}gcc")
    if(NOT RISCV_GCC)
        message(WARNING "RISC-V cross-compiler not found (${OPENSBI_CROSS_COMPILE}gcc)")
        message(WARNING "Install with: sudo apt install gcc-riscv64-linux-gnu")
        message(WARNING "OpenSBI will not be built")
        return()
    endif()

    file(MAKE_DIRECTORY ${OPENSBI_OUTPUT_DIR})
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/downloads)

    # Download at configure time
    if(NOT EXISTS "${_TARBALL}")
        message(STATUS "Downloading OpenSBI v${OPENSBI_VERSION}...")
        file(DOWNLOAD ${OPENSBI_URL} ${_TARBALL} SHOW_PROGRESS STATUS _DL_STATUS)
        list(GET _DL_STATUS 0 _DL_CODE)
        if(NOT _DL_CODE EQUAL 0)
            message(FATAL_ERROR "Failed to download OpenSBI: ${_DL_STATUS}")
        endif()
    endif()

    # Extract at configure time
    if(NOT EXISTS "${_SOURCE_DIR}/Makefile")
        message(STATUS "Extracting OpenSBI...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf ${_TARBALL}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            RESULT_VARIABLE _TAR_RESULT
        )
        if(NOT _TAR_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract OpenSBI")
        endif()
    endif()

    # Build at configure time
    message(STATUS "Building OpenSBI v${OPENSBI_VERSION}...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env CROSS_COMPILE=${OPENSBI_CROSS_COMPILE}
            make -j${NCPU} PLATFORM=generic FW_JUMP=y
        WORKING_DIRECTORY ${_SOURCE_DIR}
        RESULT_VARIABLE _BUILD_RESULT
    )
    if(NOT _BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to build OpenSBI")
    endif()

    # Copy fw_jump.elf (for TinyEMU)
    file(COPY_FILE
        ${_SOURCE_DIR}/build/platform/generic/firmware/fw_jump.elf
        ${_OUTPUT}
    )

    # Copy fw_dynamic.bin (for QEMU)
    set(_FW_DYNAMIC_SRC "${_SOURCE_DIR}/build/platform/generic/firmware/fw_dynamic.bin")
    set(_FW_DYNAMIC_DST "${OPENSBI_OUTPUT_DIR}/opensbi-fw_dynamic.bin")
    if(EXISTS "${_FW_DYNAMIC_SRC}")
        file(COPY_FILE "${_FW_DYNAMIC_SRC}" "${_FW_DYNAMIC_DST}")
        message(STATUS "OpenSBI: copied fw_dynamic.bin for QEMU")
    endif()

    # Export paths
    set(TINYEMU_OPENSBI_PATH "${_OUTPUT}" CACHE FILEPATH "" FORCE)
    set(QEMU_OPENSBI_PATH "${_FW_DYNAMIC_DST}" CACHE FILEPATH "" FORCE)

    message(STATUS "OpenSBI v${OPENSBI_VERSION} built: ${_OUTPUT}")
endfunction()
