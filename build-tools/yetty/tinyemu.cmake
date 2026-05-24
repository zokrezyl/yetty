# TinyEMU - RISC-V emulator (MIT license)
#
# Builds TinyEMU as a static library for embedding in yetty.
# Provides RISC-V emulation with virtio-console for PTY on platforms
# that don't support fork/exec (iOS).
#
if(TARGET tinyemu)
    return()
endif()

set(TINYEMU_DIR ${YETTY_ROOT}/src/tinyemu)

if(NOT EXISTS ${TINYEMU_DIR}/machine.c)
    message(FATAL_ERROR "TinyEMU source not found at ${TINYEMU_DIR}")
endif()

# Core emulator sources
set(TINYEMU_SOURCES
    ${TINYEMU_DIR}/virtio.c
    ${TINYEMU_DIR}/pci.c
    ${TINYEMU_DIR}/fs.c
    ${TINYEMU_DIR}/cutils.c
    ${TINYEMU_DIR}/iomem.c
    ${TINYEMU_DIR}/simplefb.c
    ${TINYEMU_DIR}/json.c
    ${TINYEMU_DIR}/machine.c
    ${TINYEMU_DIR}/riscv_machine.c
    ${TINYEMU_DIR}/softfp.c
    ${TINYEMU_DIR}/elf.c
    ${TINYEMU_DIR}/smp.c
)

# SLIRP user-space networking. POSIX sockets only — disabled on Windows
# until somebody Winsock-ports the 16 source files.
if(NOT WIN32)
    set(TINYEMU_SLIRP_SOURCES
        ${TINYEMU_DIR}/slirp/bootp.c
        ${TINYEMU_DIR}/slirp/ip_icmp.c
        ${TINYEMU_DIR}/slirp/mbuf.c
        ${TINYEMU_DIR}/slirp/slirp.c
        ${TINYEMU_DIR}/slirp/tcp_output.c
        ${TINYEMU_DIR}/slirp/cksum.c
        ${TINYEMU_DIR}/slirp/ip_input.c
        ${TINYEMU_DIR}/slirp/misc.c
        ${TINYEMU_DIR}/slirp/socket.c
        ${TINYEMU_DIR}/slirp/tcp_subr.c
        ${TINYEMU_DIR}/slirp/udp.c
        ${TINYEMU_DIR}/slirp/if.c
        ${TINYEMU_DIR}/slirp/ip_output.c
        ${TINYEMU_DIR}/slirp/sbuf.c
        ${TINYEMU_DIR}/slirp/tcp_input.c
        ${TINYEMU_DIR}/slirp/tcp_timer.c
        ${TINYEMU_DIR}/slirp/chr-backend.c
    )
else()
    set(TINYEMU_SLIRP_SOURCES "")
endif()

# fs_disk: POSIX hosts use fs_disk.c, Windows uses fs_disk_win32.c. iOS/tvOS
# do have host-FS access inside the app sandbox (Application Support, Caches,
# tmp), and 9p is required on all platforms so the in-process tinyemu can
# share a host directory into the guest VM. Include the POSIX impl
# everywhere except Windows.
if(WIN32)
    list(APPEND TINYEMU_SOURCES ${TINYEMU_DIR}/fs_disk_win32.c)
else()
    list(APPEND TINYEMU_SOURCES ${TINYEMU_DIR}/fs_disk.c)
endif()

# Get version
file(READ ${TINYEMU_DIR}/VERSION TINYEMU_VERSION)
string(STRIP "${TINYEMU_VERSION}" TINYEMU_VERSION)

# Create static library
add_library(tinyemu STATIC ${TINYEMU_SOURCES} ${TINYEMU_SLIRP_SOURCES})

# RISC-V CPU variants - need to compile riscv_cpu.c multiple times with different MAX_XLEN
# Use custom commands to generate the object files

set(RISCV_CPU_SRC ${TINYEMU_DIR}/riscv_cpu.c)

# riscv_cpu32.c - wrapper that sets MAX_XLEN=32
set(RISCV_CPU32_WRAPPER ${CMAKE_CURRENT_BINARY_DIR}/tinyemu_riscv_cpu32.c)
file(WRITE ${RISCV_CPU32_WRAPPER} "#define MAX_XLEN 32\n#include \"${RISCV_CPU_SRC}\"\n")

# riscv_cpu64.c - wrapper that sets MAX_XLEN=64
set(RISCV_CPU64_WRAPPER ${CMAKE_CURRENT_BINARY_DIR}/tinyemu_riscv_cpu64.c)
file(WRITE ${RISCV_CPU64_WRAPPER} "#define MAX_XLEN 64\n#include \"${RISCV_CPU_SRC}\"\n")

# riscv_cpu128.c - wrapper that sets MAX_XLEN=128 (requires int128 support)
set(RISCV_CPU128_WRAPPER ${CMAKE_CURRENT_BINARY_DIR}/tinyemu_riscv_cpu128.c)
file(WRITE ${RISCV_CPU128_WRAPPER} "#define MAX_XLEN 128\n#include \"${RISCV_CPU_SRC}\"\n")

target_sources(tinyemu PRIVATE
    ${RISCV_CPU32_WRAPPER}
    ${RISCV_CPU64_WRAPPER}
)
# RV128 needs __int128, which MSVC lacks. Only build it on toolchains that
# support it. Alpine boot is RV64; RV128 is a research-only profile.
if(NOT MSVC)
    target_sources(tinyemu PRIVATE ${RISCV_CPU128_WRAPPER})
endif()

# Include directories - set up so <tinyemu/...> works
# Create a wrapper include directory structure
set(TINYEMU_INCLUDE_WRAPPER ${CMAKE_CURRENT_BINARY_DIR}/tinyemu-include)
file(MAKE_DIRECTORY ${TINYEMU_INCLUDE_WRAPPER}/tinyemu)

# Symlink or copy headers to tinyemu subdirectory
set(TINYEMU_HEADERS
    cutils.h
    iomem.h
    virtio.h
    machine.h
    json.h
    fs.h
    list.h
    fbuf.h
    softfp.h
    riscv_cpu.h
    pci.h
    elf.h
    smp.h
    ydebug.h
)

foreach(HEADER ${TINYEMU_HEADERS})
    if(EXISTS ${TINYEMU_DIR}/${HEADER})
        configure_file(${TINYEMU_DIR}/${HEADER} ${TINYEMU_INCLUDE_WRAPPER}/tinyemu/${HEADER} COPYONLY)
    endif()
endforeach()

# SLIRP headers
file(MAKE_DIRECTORY ${TINYEMU_INCLUDE_WRAPPER}/tinyemu/slirp)
if(EXISTS ${TINYEMU_DIR}/slirp/libslirp.h)
    configure_file(${TINYEMU_DIR}/slirp/libslirp.h ${TINYEMU_INCLUDE_WRAPPER}/tinyemu/slirp/libslirp.h COPYONLY)
endif()
# Yetty addition: chr-backend header is part of slirp's public surface
# now (used by tinyemu-bridge.c on wasm to inject inbound TCP without
# an OS socket — see slirp/chr-backend.{h,c}).
if(EXISTS ${TINYEMU_DIR}/slirp/chr-backend.h)
    configure_file(${TINYEMU_DIR}/slirp/chr-backend.h ${TINYEMU_INCLUDE_WRAPPER}/tinyemu/slirp/chr-backend.h COPYONLY)
endif()

target_include_directories(tinyemu PUBLIC
    ${TINYEMU_INCLUDE_WRAPPER}
)

target_include_directories(tinyemu PRIVATE
    ${TINYEMU_DIR}
    ${TINYEMU_DIR}/slirp
)

# Compile definitions. Windows skips the 128-bit RISC-V variant (needs
# __int128); the dispatcher is emitted by the wrapper whose MAX_XLEN equals
# CONFIG_RISCV_MAX_XLEN, so cap at 64 there.
target_compile_definitions(tinyemu PRIVATE
    _FILE_OFFSET_BITS=64
    _LARGEFILE_SOURCE
    CONFIG_VERSION="${TINYEMU_VERSION}"
    $<$<NOT:$<BOOL:${WIN32}>>:CONFIG_SLIRP>
    $<IF:$<BOOL:${MSVC}>,CONFIG_RISCV_MAX_XLEN=64,CONFIG_RISCV_MAX_XLEN=128>
)

# Force-include the Win32 compat shim so we don't have to edit every .c file
# to swap pthread_mutex / gettimeofday / sleep / read-write-close.
# Also drop satisfying-but-empty stubs for the POSIX headers tinyemu's
# sources reach for (<unistd.h>, <pthread.h>, <sys/time.h>, <sys/select.h>,
# <dirent.h>): they all funnel into win32-compat.h, which is force-included
# above so the symbols are already in scope.
if(WIN32)
    if(MSVC)
        target_compile_options(tinyemu PRIVATE /FI${TINYEMU_DIR}/win32-compat.h)
    else()
        target_compile_options(tinyemu PRIVATE -include ${TINYEMU_DIR}/win32-compat.h)
    endif()

    set(_TE_WIN32_STUBS ${CMAKE_CURRENT_BINARY_DIR}/tinyemu-win32-stubs)
    file(MAKE_DIRECTORY ${_TE_WIN32_STUBS}/sys)
    foreach(_h unistd.h pthread.h dirent.h sys/param.h sys/time.h sys/select.h sys/uio.h)
        set(_path ${_TE_WIN32_STUBS}/${_h})
        if(NOT EXISTS ${_path})
            file(WRITE ${_path}
                 "/* Stub: tinyemu's win32-compat.h is force-included. */\n")
        endif()
    endforeach()
    target_include_directories(tinyemu BEFORE PRIVATE ${_TE_WIN32_STUBS})

    # /Zi gives CodeView debug info so stack traces from temu-test show
    # tinyemu function names; /Od disables optimization so line numbers
    # line up with source. (C11 + atomics flags come from the global MSVC
    # block in build-tools/yetty/platform/windows/cmake.cmake.)
    if(MSVC)
        target_compile_options(tinyemu PRIVATE /Zi /Od)
    endif()
endif()

# Platform-specific definitions
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(tinyemu PRIVATE
        CONFIG_IOS
        _DARWIN_C_SOURCE
    )
elseif(APPLE)
    target_compile_definitions(tinyemu PRIVATE
        _DARWIN_C_SOURCE
    )
else()
    target_compile_definitions(tinyemu PRIVATE
        _GNU_SOURCE
    )
endif()

# Suppress warnings in third-party code (GCC/Clang flags; ignored on MSVC).
if(NOT MSVC)
    target_compile_options(tinyemu PRIVATE
        -Wno-deprecated-declarations
        -Wno-unused-function
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        -Wno-sign-compare
    )
endif()

# Link libraries
# Linux glibc splits clock/shm functions into librt. Android bionic,
# macOS, iOS, Windows all keep them in libc/kernel32 — no separate librt.
if(NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT ANDROID AND NOT WIN32)
    target_link_libraries(tinyemu PRIVATE rt)
endif()

message(STATUS "TinyEMU: RISC-V emulator v${TINYEMU_VERSION}")

# Standalone temu executable for testing — POSIX-only main loop on Linux/macOS.
# iOS/tvOS skip it: the standalone CLI binary has no place inside an iOS app
# bundle. (The tinyemu *library* does build on iOS, with fs_disk included.)
if(NOT WIN32 AND NOT YETTY_IOS AND NOT YETTY_TVOS AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "tvOS")
    add_executable(temu ${TINYEMU_DIR}/temu.c)
    target_link_libraries(temu PRIVATE tinyemu)
    target_include_directories(temu PRIVATE ${TINYEMU_DIR} ${TINYEMU_DIR}/slirp)
    target_compile_definitions(temu PRIVATE
        _FILE_OFFSET_BITS=64
        _LARGEFILE_SOURCE
        CONFIG_VERSION="${TINYEMU_VERSION}"
        CONFIG_SLIRP
        CONFIG_RISCV_MAX_XLEN=128
        _GNU_SOURCE
    )
elseif(WIN32)
    # Windows: build a minimal driver instead. temu.c is termios+TUN+select-
    # based and would need a substantial port; temu-test.c is just enough to
    # exercise the tinyemu library headlessly so we can isolate VM init bugs.
    add_executable(temu-test ${TINYEMU_DIR}/temu-test.c)
    target_link_libraries(temu-test PRIVATE tinyemu)
    target_include_directories(temu-test PRIVATE ${TINYEMU_DIR})
    target_compile_definitions(temu-test PRIVATE
        _FILE_OFFSET_BITS=64
        _LARGEFILE_SOURCE
        CONFIG_VERSION="${TINYEMU_VERSION}"
        CONFIG_RISCV_MAX_XLEN=64
    )
    if(MSVC)
        target_compile_options(temu-test PRIVATE
            /FI${TINYEMU_DIR}/win32-compat.h
            /Zi      # CodeView debug info regardless of build type
            /Od)     # disable opt so the stack trace lines up with source
        target_link_options(temu-test PRIVATE /DEBUG)
        target_include_directories(temu-test BEFORE PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR}/tinyemu-win32-stubs)
    endif()
endif()
