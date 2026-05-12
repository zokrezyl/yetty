# Alpine Linux Rootfs for TinyEMU
#
# Downloads Alpine Linux minirootfs for RISC-V 64-bit
#
# License: Mix of GPL/MIT/BSD (see Alpine package licenses)
# Source: https://alpinelinux.org/
#

include_guard(GLOBAL)

# Version
set(ALPINE_VERSION "3.23" CACHE STRING "Alpine Linux version")
set(ALPINE_RELEASE "3.23.4" CACHE STRING "Alpine Linux release")

# URL
set(ALPINE_URL "https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION}/releases/riscv64/alpine-minirootfs-${ALPINE_RELEASE}-riscv64.tar.gz")

# Output paths (shared RISC-V runtime assets — used by both TinyEMU and QEMU)
set(ALPINE_ROOTFS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/assets/yemu" CACHE PATH "Rootfs output directory")

#-----------------------------------------------------------------------------
# alpine_rootfs_download()
#
# Downloads and extracts Alpine Linux minirootfs for RISC-V
# Output: ${ALPINE_ROOTFS_OUTPUT_DIR}/alpine-rootfs/
#-----------------------------------------------------------------------------
function(alpine_rootfs_download)
    set(_ROOTFS_TAR "${CMAKE_BINARY_DIR}/downloads/alpine-minirootfs-${ALPINE_RELEASE}-riscv64.tar.gz")
    set(_ROOTFS_DIR "${ALPINE_ROOTFS_OUTPUT_DIR}/alpine-rootfs")

    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/downloads)
    file(MAKE_DIRECTORY ${_ROOTFS_DIR})

    # Download at configure time (small file, ~3MB)
    if(NOT EXISTS "${_ROOTFS_TAR}")
        message(STATUS "Downloading Alpine Linux ${ALPINE_RELEASE} minirootfs...")
        file(DOWNLOAD
            ${ALPINE_URL}
            ${_ROOTFS_TAR}
            SHOW_PROGRESS
            STATUS _DL_STATUS
        )
        list(GET _DL_STATUS 0 _DL_CODE)
        if(NOT _DL_CODE EQUAL 0)
            message(FATAL_ERROR "Failed to download Alpine rootfs: ${_DL_STATUS}")
        endif()
    endif()

    # Extract at configure time
    if(NOT EXISTS "${_ROOTFS_DIR}/bin/busybox")
        message(STATUS "Extracting Alpine rootfs to ${_ROOTFS_DIR}...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf ${_ROOTFS_TAR}
            WORKING_DIRECTORY ${_ROOTFS_DIR}
            RESULT_VARIABLE _TAR_RESULT
        )
        if(NOT _TAR_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract Alpine rootfs")
        endif()
    endif()

    # Create init script for TinyEMU console boot
    set(_INIT_SCRIPT "${_ROOTFS_DIR}/init")
    if(NOT EXISTS "${_INIT_SCRIPT}")
        file(WRITE ${_INIT_SCRIPT}.tmp [=[
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev 2>/dev/null || true
hostname tinyemu

# Network setup (slirp provides 10.0.2.x network)
ip link set lo up
ip link set eth0 up 2>/dev/null
ip addr add 10.0.2.15/24 dev eth0 2>/dev/null
ip route add default via 10.0.2.2 2>/dev/null
echo "nameserver 10.0.2.3" > /etc/resolv.conf

exec /bin/sh
]=])
        # Make executable
        execute_process(COMMAND chmod 755 ${_INIT_SCRIPT}.tmp)
        execute_process(COMMAND ${CMAKE_COMMAND} -E rename ${_INIT_SCRIPT}.tmp ${_INIT_SCRIPT})
    endif()

    # Export path for other modules
    set(TINYEMU_ROOTFS_DIR "${_ROOTFS_DIR}" CACHE PATH "" FORCE)

    message(STATUS "Alpine Linux ${ALPINE_RELEASE} rootfs ready")
    message(STATUS "  Location: ${_ROOTFS_DIR}")
endfunction()

#-----------------------------------------------------------------------------
# alpine_rootfs_create_image()
#
# Creates a disk image from the rootfs directory
# Output: ${ALPINE_ROOTFS_OUTPUT_DIR}/root-riscv64.bin
#-----------------------------------------------------------------------------
function(alpine_rootfs_create_image)
    # No-op: we use virtio-9p to mount the rootfs directory directly
    # The rootfs directory is already extracted by alpine_rootfs_download()
    message(STATUS "TinyEMU rootfs: using virtio-9p for ${ALPINE_ROOTFS_OUTPUT_DIR}/alpine-rootfs")
endfunction()

#-----------------------------------------------------------------------------
# alpine_rootfs_create_config()
#
# Creates TinyEMU VM configuration file
# Output: ${ALPINE_ROOTFS_OUTPUT_DIR}/root-riscv64.cfg
#-----------------------------------------------------------------------------
function(alpine_rootfs_create_config)
    set(_CONFIG_FILE "${ALPINE_ROOTFS_OUTPUT_DIR}/root-riscv64.cfg")

    file(WRITE ${_CONFIG_FILE} [=[
/* TinyEMU VM Configuration */
{
    version: 1,
    machine: "riscv64",
    memory_size: 256,
    bios: "opensbi-fw_jump.elf",
    kernel: "kernel-riscv64.bin",
    cmdline: "earlycon=sbi console=hvc0 root=/dev/root rootfstype=9p rootflags=trans=virtio,cache=mmap,msize=8192 rw init=/init",
    fs0: { file: "alpine-rootfs", tag: "/dev/root" },
    eth0: { driver: "user" }
}
]=])

    # Export path
    set(TINYEMU_CONFIG_PATH "${_CONFIG_FILE}" CACHE FILEPATH "" FORCE)

    message(STATUS "TinyEMU config: ${_CONFIG_FILE}")
endfunction()
