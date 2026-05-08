# QEMU - RISC-V system emulator (external binary, launched via telnet chardev)
#
# Builds a minimal qemu-system-riscv64 with the poc/qemu device config
# (see poc/qemu/configs/riscv64-softmmu/default.mak). Produces a single
# stripped binary that is later embedded into the yetty app via incbin.
#
# License: GPL v2
# Source: https://download.qemu.org/
#
# Companion shell script (behavior mirror): poc/qemu/build-tools/build-linux-minimal.sh
#

include_guard(GLOBAL)

# Version
set(QEMU_VERSION "11.0.0-rc4" CACHE STRING "QEMU version")

# URL
set(QEMU_URL "https://download.qemu.org/qemu-${QEMU_VERSION}.tar.xz")

# Output path (embedded into app via incbin — see targets/shared.cmake)
set(QEMU_OUTPUT_DIR "${CMAKE_BINARY_DIR}/assets/qemu" CACHE PATH "QEMU output directory")

# Minimal device config (prunes riscv64-softmmu devices to just what we need)
set(QEMU_DEVICE_CONFIG "${YETTY_ROOT}/poc/qemu/configs/riscv64-softmmu/default.mak")

# Host compiler — hardcoded to system gcc so /usr/include is searched even
# inside a Nix shell (matches build-linux-minimal.sh).
set(QEMU_SYSTEM_CC "/usr/bin/gcc")
set(QEMU_SYSTEM_CXX "/usr/bin/g++")

# Number of parallel jobs
include(ProcessorCount)
ProcessorCount(NCPU)
if(NCPU EQUAL 0)
    set(NCPU 4)
endif()

#-----------------------------------------------------------------------------
# qemu_build()
#
# Downloads and builds qemu-system-riscv64 at CONFIGURE time.
# Output: ${QEMU_OUTPUT_DIR}/qemu-system-riscv64
#
# Honors env vars USE_CCACHE=1 / USE_DISTCC=1 to wrap the compiler.
#-----------------------------------------------------------------------------
function(qemu_build)
    set(_OUTPUT "${QEMU_OUTPUT_DIR}/qemu-system-riscv64")
    set(_SOURCE_DIR "${CMAKE_BINARY_DIR}/qemu-${QEMU_VERSION}")
    set(_BUILD_DIR "${CMAKE_BINARY_DIR}/qemu-build")
    set(_TARBALL "${CMAKE_BINARY_DIR}/downloads/qemu-${QEMU_VERSION}.tar.xz")
    set(_LOG_DIR "${CMAKE_BINARY_DIR}/tmp")

    # Skip if already built
    if(EXISTS "${_OUTPUT}")
        message(STATUS "QEMU v${QEMU_VERSION} already built: ${_OUTPUT}")
        return()
    endif()

    if(NOT EXISTS "${QEMU_DEVICE_CONFIG}")
        message(FATAL_ERROR "QEMU device config not found: ${QEMU_DEVICE_CONFIG}")
    endif()

    # Check for host compiler
    if(NOT EXISTS "${QEMU_SYSTEM_CC}")
        message(WARNING "QEMU: system gcc not found at ${QEMU_SYSTEM_CC}")
        message(WARNING "Install with: sudo apt install build-essential")
        message(WARNING "QEMU will not be built")
        return()
    endif()

    file(MAKE_DIRECTORY ${QEMU_OUTPUT_DIR})
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/downloads)
    file(MAKE_DIRECTORY ${_LOG_DIR})

    # Download at configure time
    if(NOT EXISTS "${_TARBALL}")
        message(STATUS "Downloading QEMU v${QEMU_VERSION}...")
        file(DOWNLOAD ${QEMU_URL} ${_TARBALL} SHOW_PROGRESS STATUS _DL_STATUS)
        list(GET _DL_STATUS 0 _DL_CODE)
        if(NOT _DL_CODE EQUAL 0)
            message(FATAL_ERROR "Failed to download QEMU: ${_DL_STATUS}")
        endif()
    endif()

    # Extract at configure time
    if(NOT EXISTS "${_SOURCE_DIR}/configure")
        message(STATUS "Extracting QEMU...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xJf ${_TARBALL}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            RESULT_VARIABLE _TAR_RESULT
        )
        if(NOT _TAR_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract QEMU")
        endif()
    endif()

    # Install minimal device config
    set(_DEVCFG_DIR "${_SOURCE_DIR}/configs/devices/riscv64-softmmu")
    file(MAKE_DIRECTORY "${_DEVCFG_DIR}")
    file(COPY_FILE "${QEMU_DEVICE_CONFIG}" "${_DEVCFG_DIR}/default.mak")

    # Clean build directory (QEMU's configure doesn't tolerate stale state well)
    file(REMOVE_RECURSE "${_BUILD_DIR}")
    file(MAKE_DIRECTORY "${_BUILD_DIR}")

    # Compiler wrapper (ccache/distcc) — same convention as build-linux-minimal.sh
    set(_CC "${QEMU_SYSTEM_CC}")
    set(_CXX "${QEMU_SYSTEM_CXX}")
    if("$ENV{USE_CCACHE}" STREQUAL "1")
        message(STATUS "QEMU: using ccache for build acceleration")
        set(_CC "ccache ${QEMU_SYSTEM_CC}")
        set(_CXX "ccache ${QEMU_SYSTEM_CXX}")
    elseif("$ENV{USE_DISTCC}" STREQUAL "1")
        message(STATUS "QEMU: using distcc for build acceleration")
        set(_CC "distcc ${QEMU_SYSTEM_CC}")
        set(_CXX "distcc ${QEMU_SYSTEM_CXX}")
    endif()

    # Configure — flags mirror poc/qemu/build-tools/build-linux-minimal.sh
    message(STATUS "Configuring QEMU v${QEMU_VERSION}...")
    execute_process(
        COMMAND ${_SOURCE_DIR}/configure
            --target-list=riscv64-softmmu
            --without-default-features
            --enable-tcg
            --enable-slirp
            --enable-virtfs
            --enable-attr
            --enable-fdt=internal
            --enable-trace-backends=nop
            --disable-werror
            --disable-docs
            --disable-guest-agent
            --disable-tools
            --disable-qom-cast-debug
            --disable-coroutine-pool
            "--cc=${_CC}"
            "--cxx=${_CXX}"
            "--extra-cflags=-Os -ffunction-sections -fdata-sections"
            "--extra-cxxflags=-Os -ffunction-sections -fdata-sections"
            "--extra-ldflags=-Wl,--gc-sections"
        WORKING_DIRECTORY ${_BUILD_DIR}
        OUTPUT_FILE ${_LOG_DIR}/configure-qemu.log
        ERROR_FILE ${_LOG_DIR}/configure-qemu.log
        RESULT_VARIABLE _CONFIG_RESULT
    )
    if(NOT _CONFIG_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to configure QEMU (see ${_LOG_DIR}/configure-qemu.log)")
    endif()

    # Build
    message(STATUS "Building QEMU v${QEMU_VERSION} (-j${NCPU}, this may take a while)...")
    execute_process(
        COMMAND make -j${NCPU}
        WORKING_DIRECTORY ${_BUILD_DIR}
        OUTPUT_FILE ${_LOG_DIR}/build-qemu.log
        ERROR_FILE ${_LOG_DIR}/build-qemu.log
        RESULT_VARIABLE _BUILD_RESULT
    )
    if(NOT _BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to build QEMU (see ${_LOG_DIR}/build-qemu.log)")
    endif()

    set(_BUILT_BIN "${_BUILD_DIR}/qemu-system-riscv64")
    if(NOT EXISTS "${_BUILT_BIN}")
        message(FATAL_ERROR "QEMU build completed but binary not found: ${_BUILT_BIN}")
    endif()

    # Strip (best-effort — don't fail the build if strip is missing)
    find_program(STRIP_BIN strip)
    if(STRIP_BIN)
        execute_process(
            COMMAND ${STRIP_BIN} ${_BUILT_BIN}
            RESULT_VARIABLE _STRIP_RESULT
        )
    endif()

    # Install into output dir (consumed by targets/shared.cmake)
    file(COPY_FILE "${_BUILT_BIN}" "${_OUTPUT}")

    message(STATUS "QEMU v${QEMU_VERSION} built: ${_OUTPUT}")
    message(STATUS "  License: GPL v2 - Source: ${QEMU_URL}")
endfunction()

#-----------------------------------------------------------------------------
# qemu_embed_runtime(target)
#
# Embeds the built qemu-system-riscv64 binary into the given target via
# incbin, under the "qemu" asset prefix. Must be called after qemu_build().
# The binary is copied into a staging directory; callers consume it at
# runtime as asset "qemu/qemu-system-riscv64" (extracted on first launch).
#-----------------------------------------------------------------------------
function(qemu_embed_runtime TARGET)
    if(NOT COMMAND incbin_add_directory)
        include(${YETTY_ROOT}/build-tools/cmake/incbin.cmake)
    endif()

    set(_STAGING "${CMAKE_BINARY_DIR}/embed-qemu")
    file(MAKE_DIRECTORY "${_STAGING}")

    # Pick binary name. Windows ships qemu-system-riscv64.exe alongside a set
    # of MSVC/MSYS2 runtime DLLs; other targets ship just the bare ELF/Mach-O
    # binary.
    if(WIN32)
        set(_BIN_NAME "qemu-system-riscv64.exe")
    else()
        set(_BIN_NAME "qemu-system-riscv64")
    endif()

    # Source: prefer the prebuilt 3rdparty fetch; fall back to the
    # legacy from-source QEMU_OUTPUT_DIR if a developer kept that path.
    set(_SRC_DIR "")
    if(DEFINED YETTY_3RDPARTY_qemu_DIR AND IS_DIRECTORY "${YETTY_3RDPARTY_qemu_DIR}")
        set(_SRC_DIR "${YETTY_3RDPARTY_qemu_DIR}")
    elseif(DEFINED QEMU_OUTPUT_DIR AND IS_DIRECTORY "${QEMU_OUTPUT_DIR}")
        set(_SRC_DIR "${QEMU_OUTPUT_DIR}")
    endif()

    set(_BIN "${_SRC_DIR}/${_BIN_NAME}")
    if(_SRC_DIR AND EXISTS "${_BIN}")
        file(COPY "${_BIN}" DESTINATION "${_STAGING}")
        # On Windows, also stage the runtime DLLs the .exe needs (glib,
        # iconv, pcre, pixman, zlib, winpthread, libc++, ...). The tarball
        # ships them alongside the .exe; everything in the source dir except
        # the binary itself + cmake's fetch stamp gets copied.
        if(WIN32)
            file(GLOB _QEMU_DLLS "${_SRC_DIR}/*.dll")
            if(_QEMU_DLLS)
                file(COPY ${_QEMU_DLLS} DESTINATION "${_STAGING}")
            endif()
        endif()
    else()
        message(WARNING "qemu_embed_runtime: ${_BIN_NAME} not found at '${_BIN}'; QEMU will be embedded empty")
    endif()

    # Not compressed — it's an executable, runtime extracts and chmods it
    incbin_add_directory(${TARGET} "qemu" "${_STAGING}" "*" FALSE)
endfunction()
