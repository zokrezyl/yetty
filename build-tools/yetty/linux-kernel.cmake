# Linux Kernel Builder for TinyEMU
#
# Downloads and builds Linux kernel v7.0 for RISC-V 64-bit
#
# License: GPL v2
# Source: https://github.com/torvalds/linux/archive/refs/tags/v7.0.tar.gz
#

include_guard(GLOBAL)

# Version
set(LINUX_VERSION "7.0" CACHE STRING "Linux kernel version")

# URL
set(LINUX_URL "https://github.com/torvalds/linux/archive/refs/tags/v${LINUX_VERSION}.tar.gz")

# Paths
set(LINUX_KERNEL_CONFIG "${YETTY_ROOT}/poc/qemu/configs/linux-kernel-7.0.config")
set(LINUX_KERNEL_OUTPUT_DIR "${CMAKE_BINARY_DIR}/assets/yemu" CACHE PATH "Kernel output directory")

# Cross-compiler (must be installed on system)
set(LINUX_CROSS_COMPILE "riscv64-linux-gnu-" CACHE STRING "Cross-compiler prefix for RISC-V")

# Number of parallel jobs
include(ProcessorCount)
ProcessorCount(NCPU)
if(NCPU EQUAL 0)
    set(NCPU 4)
endif()

#-----------------------------------------------------------------------------
# linux_kernel_build()
#
# Downloads and builds Linux kernel for RISC-V at CONFIGURE time
# Output: ${LINUX_KERNEL_OUTPUT_DIR}/kernel-riscv64.bin
#-----------------------------------------------------------------------------
function(linux_kernel_build)
    set(_OUTPUT "${LINUX_KERNEL_OUTPUT_DIR}/kernel-riscv64.bin")
    set(_SOURCE_DIR "${CMAKE_BINARY_DIR}/linux-${LINUX_VERSION}")
    set(_TARBALL "${CMAKE_BINARY_DIR}/downloads/linux-${LINUX_VERSION}.tar.gz")

    # Skip if already built
    if(EXISTS "${_OUTPUT}")
        message(STATUS "Linux kernel v${LINUX_VERSION} already built: ${_OUTPUT}")
        set(TINYEMU_KERNEL_PATH "${_OUTPUT}" CACHE FILEPATH "" FORCE)
        return()
    endif()

    if(NOT EXISTS "${LINUX_KERNEL_CONFIG}")
        message(FATAL_ERROR "Linux kernel config not found: ${LINUX_KERNEL_CONFIG}")
    endif()

    # Check for cross-compiler
    find_program(RISCV_GCC "${LINUX_CROSS_COMPILE}gcc")
    if(NOT RISCV_GCC)
        message(WARNING "RISC-V cross-compiler not found (${LINUX_CROSS_COMPILE}gcc)")
        message(WARNING "Install with: sudo apt install gcc-riscv64-linux-gnu")
        message(WARNING "Kernel will not be built")
        return()
    endif()

    file(MAKE_DIRECTORY ${LINUX_KERNEL_OUTPUT_DIR})
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/downloads)

    # Download at configure time
    if(NOT EXISTS "${_TARBALL}")
        message(STATUS "Downloading Linux kernel v${LINUX_VERSION}...")
        file(DOWNLOAD ${LINUX_URL} ${_TARBALL} SHOW_PROGRESS STATUS _DL_STATUS)
        list(GET _DL_STATUS 0 _DL_CODE)
        if(NOT _DL_CODE EQUAL 0)
            message(FATAL_ERROR "Failed to download Linux kernel: ${_DL_STATUS}")
        endif()
    endif()

    # Extract at configure time
    if(NOT EXISTS "${_SOURCE_DIR}/Makefile")
        message(STATUS "Extracting Linux kernel...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf ${_TARBALL}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            RESULT_VARIABLE _TAR_RESULT
        )
        if(NOT _TAR_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract Linux kernel")
        endif()
    endif()

    # Copy config
    file(COPY_FILE ${LINUX_KERNEL_CONFIG} ${_SOURCE_DIR}/.config)

    # Build at configure time
    message(STATUS "Building Linux kernel v${LINUX_VERSION} (this may take a while)...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env ARCH=riscv CROSS_COMPILE=${LINUX_CROSS_COMPILE}
            make -j${NCPU} Image
        WORKING_DIRECTORY ${_SOURCE_DIR}
        RESULT_VARIABLE _BUILD_RESULT
    )
    if(NOT _BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to build Linux kernel")
    endif()

    # Copy output
    file(COPY_FILE
        ${_SOURCE_DIR}/arch/riscv/boot/Image
        ${_OUTPUT}
    )

    # Export path
    set(TINYEMU_KERNEL_PATH "${_OUTPUT}" CACHE FILEPATH "" FORCE)

    message(STATUS "Linux kernel v${LINUX_VERSION} built: ${_OUTPUT}")
    message(STATUS "  License: GPL v2 - Source: ${LINUX_URL}")
endfunction()
