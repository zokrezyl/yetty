# TinyEMU Runtime Assets
#
# Handles embedding or copying of TinyEMU runtime files:
# - RISC-V kernel binary
# - Root filesystem (disk image or 9p directory)
# - VM configuration file
#
# For iOS: assets are embedded via incbin
# For desktop: assets are copied to build directory
#

include_guard(GLOBAL)

# Default paths for TinyEMU runtime assets
# These can be overridden by setting cache variables before including this file
set(TINYEMU_KERNEL_PATH "" CACHE FILEPATH "Path to RISC-V kernel binary")
set(TINYEMU_ROOTFS_PATH "" CACHE FILEPATH "Path to root filesystem image")
set(TINYEMU_CONFIG_PATH "" CACHE FILEPATH "Path to VM config file (.cfg)")

# Asset directory (where pre-built assets are stored)
set(TINYEMU_ASSETS_DIR ${YETTY_ROOT}/assets/tinyemu CACHE PATH "TinyEMU assets directory")

#-----------------------------------------------------------------------------
# tinyemu_copy_runtime_to_bundle(target)
#
# For iOS: embeds TinyEMU runtime assets into the app bundle
# For desktop: copies assets to the build output directory
#-----------------------------------------------------------------------------
function(tinyemu_copy_runtime_to_bundle TARGET)
    # Determine asset paths (use defaults if not specified)
    if(TINYEMU_KERNEL_PATH AND EXISTS "${TINYEMU_KERNEL_PATH}")
        set(_KERNEL "${TINYEMU_KERNEL_PATH}")
    elseif(EXISTS "${TINYEMU_ASSETS_DIR}/kernel-riscv64.bin")
        set(_KERNEL "${TINYEMU_ASSETS_DIR}/kernel-riscv64.bin")
    else()
        message(WARNING "TinyEMU: kernel not found, VM will not boot")
        return()
    endif()

    if(TINYEMU_ROOTFS_PATH AND EXISTS "${TINYEMU_ROOTFS_PATH}")
        set(_ROOTFS "${TINYEMU_ROOTFS_PATH}")
    elseif(EXISTS "${TINYEMU_ASSETS_DIR}/root-riscv64.bin")
        set(_ROOTFS "${TINYEMU_ASSETS_DIR}/root-riscv64.bin")
    else()
        message(WARNING "TinyEMU: rootfs not found")
        set(_ROOTFS "")
    endif()

    if(TINYEMU_CONFIG_PATH AND EXISTS "${TINYEMU_CONFIG_PATH}")
        set(_CONFIG "${TINYEMU_CONFIG_PATH}")
    elseif(EXISTS "${TINYEMU_ASSETS_DIR}/root-riscv64.cfg")
        set(_CONFIG "${TINYEMU_ASSETS_DIR}/root-riscv64.cfg")
    else()
        message(WARNING "TinyEMU: config not found")
        set(_CONFIG "")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        # iOS: Add as bundle resources
        if(_KERNEL)
            target_sources(${TARGET} PRIVATE ${_KERNEL})
            set_source_files_properties(${_KERNEL} PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources"
            )
        endif()

        if(_ROOTFS)
            target_sources(${TARGET} PRIVATE ${_ROOTFS})
            set_source_files_properties(${_ROOTFS} PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources"
            )
        endif()

        if(_CONFIG)
            target_sources(${TARGET} PRIVATE ${_CONFIG})
            set_source_files_properties(${_CONFIG} PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources"
            )
        endif()

        message(STATUS "TinyEMU runtime: bundled for iOS")
        if(_KERNEL)
            message(STATUS "  Kernel: ${_KERNEL}")
        endif()
        if(_ROOTFS)
            message(STATUS "  Rootfs: ${_ROOTFS}")
        endif()
        if(_CONFIG)
            message(STATUS "  Config: ${_CONFIG}")
        endif()

    else()
        # Desktop: Copy to build directory
        set(_OUTPUT_DIR "${CMAKE_BINARY_DIR}/tinyemu-runtime")
        file(MAKE_DIRECTORY ${_OUTPUT_DIR})

        if(_KERNEL)
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_KERNEL}" "${_OUTPUT_DIR}/kernel-riscv64.bin"
                COMMENT "Copying TinyEMU kernel"
            )
        endif()

        if(_ROOTFS)
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_ROOTFS}" "${_OUTPUT_DIR}/root-riscv64.bin"
                COMMENT "Copying TinyEMU rootfs"
            )
        endif()

        if(_CONFIG)
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_CONFIG}" "${_OUTPUT_DIR}/root-riscv64.cfg"
                COMMENT "Copying TinyEMU config"
            )
        endif()

        message(STATUS "TinyEMU runtime: copying to ${_OUTPUT_DIR}")
    endif()
endfunction()

#-----------------------------------------------------------------------------
# tinyemu_embed_runtime(target)
#
# Embeds TinyEMU runtime assets using incbin.
# Creates symbols: gTinyemuKernelData/Size, gTinyemuOpensbiData/Size,
#                  gTinyemuConfigData/Size, gTinyemuRootfsTarData/Size
#-----------------------------------------------------------------------------
function(tinyemu_embed_runtime TARGET)
    # Check if incbin is available
    if(NOT COMMAND incbin_add_resources)
        include(${YETTY_ROOT}/build-tools/cmake/incbin.cmake)
    endif()

    # Kernel
    if(TINYEMU_KERNEL_PATH AND EXISTS "${TINYEMU_KERNEL_PATH}")
        set(_KERNEL "${TINYEMU_KERNEL_PATH}")
    elseif(EXISTS "${TINYEMU_ASSETS_DIR}/kernel-riscv64.bin")
        set(_KERNEL "${TINYEMU_ASSETS_DIR}/kernel-riscv64.bin")
    else()
        message(FATAL_ERROR "TinyEMU: kernel not found for embedding")
    endif()

    # OpenSBI
    if(EXISTS "${TINYEMU_ASSETS_DIR}/opensbi-fw_jump.elf")
        set(_OPENSBI "${TINYEMU_ASSETS_DIR}/opensbi-fw_jump.elf")
    else()
        message(FATAL_ERROR "TinyEMU: opensbi not found for embedding")
    endif()

    # Config
    if(TINYEMU_CONFIG_PATH AND EXISTS "${TINYEMU_CONFIG_PATH}")
        set(_CONFIG "${TINYEMU_CONFIG_PATH}")
    elseif(EXISTS "${TINYEMU_ASSETS_DIR}/root-riscv64.cfg")
        set(_CONFIG "${TINYEMU_ASSETS_DIR}/root-riscv64.cfg")
    else()
        message(FATAL_ERROR "TinyEMU: config not found for embedding")
    endif()

    # Rootfs - pack directory as tarball for embedding
    set(_ROOTFS_DIR "${TINYEMU_ASSETS_DIR}/alpine-rootfs")
    set(_ROOTFS_TAR "${CMAKE_CURRENT_BINARY_DIR}/tinyemu-rootfs.tar")
    if(EXISTS "${_ROOTFS_DIR}")
        # Create tarball of rootfs directory
        add_custom_command(
            OUTPUT "${_ROOTFS_TAR}"
            COMMAND ${CMAKE_COMMAND} -E tar cf "${_ROOTFS_TAR}" .
            WORKING_DIRECTORY "${_ROOTFS_DIR}"
            DEPENDS "${_ROOTFS_DIR}"
            COMMENT "Packing TinyEMU rootfs into tarball"
        )
        add_custom_target(tinyemu_rootfs_tar DEPENDS "${_ROOTFS_TAR}")
        add_dependencies(${TARGET} tinyemu_rootfs_tar)
    else()
        message(FATAL_ERROR "TinyEMU: rootfs directory not found for embedding")
    endif()

    # Embed using incbin
    incbin_add_resources(${TARGET}
        TinyemuKernel "${_KERNEL}"
        TinyemuOpensbi "${_OPENSBI}"
        TinyemuConfig "${_CONFIG}"
        TinyemuRootfsTar "${_ROOTFS_TAR}"
    )

    message(STATUS "TinyEMU: embedding runtime assets")
    message(STATUS "  Kernel: ${_KERNEL}")
    message(STATUS "  OpenSBI: ${_OPENSBI}")
    message(STATUS "  Config: ${_CONFIG}")
    message(STATUS "  Rootfs: ${_ROOTFS_DIR} -> ${_ROOTFS_TAR}")
endfunction()
