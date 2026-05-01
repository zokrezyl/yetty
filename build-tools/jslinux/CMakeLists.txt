# JSLinux integration for WebAssembly builds
# Downloads and copies JSLinux files for iframe-based emulator

set(JSLINUX_DIR "${CMAKE_CURRENT_BINARY_DIR}/jslinux")
set(JSLINUX_URL "https://bellard.org/jslinux")

# Files we download from bellard.org/jslinux.
# Legacy (x86_64) variant: x86_64emu-wasm.* + kernel-x86_64.bin downloaded
# as-is — bellard's prebuilt wasm + kernel. Has been working for a while.
# Note: riscvemu64-wasm.* is NOT downloaded anymore — we now build it
# ourselves from src/tinyemu (see Makefile.js + js/lib.js). That gives us
# yetty's elf.c (so cfg can use bios: opensbi-fw_jump.elf without an
# objcopy hack) and a matching SBI implementation for yetty's kernel.
set(JSLINUX_FILES
    x86_64emu-wasm.js
    x86_64emu-wasm.wasm
    kernel-x86_64.bin
    jslinux.js
    term.js
)

# Download JSLinux files during configure
file(MAKE_DIRECTORY ${JSLINUX_DIR})

foreach(FILE ${JSLINUX_FILES})
    set(LOCAL_FILE "${JSLINUX_DIR}/${FILE}")
    if(NOT EXISTS "${LOCAL_FILE}")
        message(STATUS "Downloading ${FILE}...")
        file(DOWNLOAD "${JSLINUX_URL}/${FILE}" "${LOCAL_FILE}"
             STATUS DOWNLOAD_STATUS
             TIMEOUT 60)
        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        if(NOT STATUS_CODE EQUAL 0)
            message(WARNING "Failed to download ${FILE}")
        endif()
    endif()
endforeach()

# --- Variant cfgs ----------------------------------------------------------
# legacy: x86_64, 9p root from yetty-alpine vfsync (current default).
set(CONFIG_FILE "${JSLINUX_DIR}/alpine-x86_64.cfg")
file(WRITE ${CONFIG_FILE} "/* VM configuration file - uses external GitHub Pages vfsync filesystem */
{
    version: 1,
    machine: \"pc\",
    memory_size: 256,
    kernel: \"kernel-x86_64.bin\",
    cmdline: \"loglevel=3 console=hvc0 root=root rootfstype=9p rootflags=trans=virtio ro TZ=\${TZ}\",
    fs0: { file: \"../vfsync/u/os/yetty-alpine\" },
    eth0: { driver: \"user\" },
}
")

# alpine-disk: riscv64, ext4 root on virtio-blk0
set(CONFIG_ALPINE_DISK "${JSLINUX_DIR}/alpine-disk.cfg")
file(WRITE ${CONFIG_ALPINE_DISK} "/* alpine-disk variant: riscv64, ext4 root on /dev/vda */
{
    version: 1,
    machine: \"riscv64\",
    memory_size: 256,
    bios: \"opensbi-fw_jump.elf\",
    kernel: \"kernel-riscv64.bin\",
    cmdline: \"earlycon=sbi console=hvc0 root=/dev/vda rootfstype=ext4 rw init=/init TZ=\${TZ}\",
    drive0: { file: \"alpine-rootfs.img\" },
    width: 1024,
    height: 768,
    eth0: { driver: \"user\" },
}
")

# alpine-extended-disk: same shape, larger image (Alpine + dev tools).
# The chunked image exceeds GitHub Pages' 1 GB budget alongside vfsync, so
# this cfg points at an externally-hosted splitimg dir. Override
# YETTY_ALPINE_EXTENDED_DISK_URL at configure time once the host is wired up;
# until then the variant is selectable in the UI but boot will fail with a
# clear network error rather than silently doing the wrong thing.
set(YETTY_ALPINE_EXTENDED_DISK_URL "alpine-extended-disk-blocks/blk.txt"
    CACHE STRING "URL of the splitimg blk.txt manifest for the alpine-extended-disk variant")
set(CONFIG_ALPINE_EXTENDED "${JSLINUX_DIR}/alpine-extended-disk.cfg")
file(WRITE ${CONFIG_ALPINE_EXTENDED} "/* alpine-extended-disk variant: riscv64, ext4 root on /dev/vda */
{
    version: 1,
    machine: \"riscv64\",
    memory_size: 256,
    bios: \"opensbi-fw_jump.elf\",
    kernel: \"kernel-riscv64.bin\",
    cmdline: \"earlycon=sbi console=hvc0 root=/dev/vda rootfstype=ext4 rw init=/init TZ=\${TZ}\",
    drive0: { file: \"alpine-extended-rootfs.img\" },
    width: 1024,
    height: 768,
    eth0: { driver: \"user\" },
}
")

# Copy our bridge files
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/term-bridge.js
    ${JSLINUX_DIR}/term-bridge.js
    COPYONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/vm-bridge.html
    ${JSLINUX_DIR}/vm-bridge.html
    COPYONLY
)

# --- riscv64 boot assets (kernel + opensbi) ----------------------------------
# Pulled from the linux/opensbi 3rdparty fetches that shared.cmake already
# wires when YETTY_ENABLE_LIB_TINYEMU is on. We just stage the raw (already
# brotli-decompressed alongside) files into the jslinux/ output directory so
# the emitted alpine-disk*.cfg can reference them with a relative path.
set(_KERNEL_RISCV64_SRC "${TINYEMU_KERNEL_PATH}")
set(_OPENSBI_SRC        "${TINYEMU_OPENSBI_PATH}")
set(_ALPINE_DISK_IMG    "${TINYEMU_ROOTFS_IMG}")
set(_ALPINE_EXTENDED_IMG "${TINYEMU_ALPINE_EXTENDED_IMG}")

if(_KERNEL_RISCV64_SRC AND EXISTS "${_KERNEL_RISCV64_SRC}")
    configure_file(${_KERNEL_RISCV64_SRC} ${JSLINUX_DIR}/kernel-riscv64.bin COPYONLY)
else()
    message(WARNING "jslinux: kernel-riscv64.bin not found at '${_KERNEL_RISCV64_SRC}' — alpine-disk variants will not boot")
endif()

if(_OPENSBI_SRC AND EXISTS "${_OPENSBI_SRC}")
    configure_file(${_OPENSBI_SRC} ${JSLINUX_DIR}/opensbi-fw_jump.elf COPYONLY)
else()
    message(WARNING "jslinux: opensbi-fw_jump.elf not found at '${_OPENSBI_SRC}' — alpine-disk variants will not boot")
endif()

if(_ALPINE_DISK_IMG AND EXISTS "${_ALPINE_DISK_IMG}")
    configure_file(${_ALPINE_DISK_IMG} ${JSLINUX_DIR}/alpine-rootfs.img COPYONLY)
else()
    message(WARNING "jslinux: alpine-rootfs.img not found at '${_ALPINE_DISK_IMG}' — alpine-disk variant will be unavailable")
endif()

if(_ALPINE_EXTENDED_IMG AND EXISTS "${_ALPINE_EXTENDED_IMG}")
    configure_file(${_ALPINE_EXTENDED_IMG} ${JSLINUX_DIR}/alpine-extended-rootfs.img COPYONLY)
else()
    message(WARNING "jslinux: alpine-extended-rootfs.img not found at '${_ALPINE_EXTENDED_IMG}' — alpine-extended-disk variant will be unavailable")
endif()

# --- riscvemu64-wasm (built from yetty's src/tinyemu) ------------------------
# We invoke src/tinyemu/Makefile.js inside the .#web nix shell. The Makefile
# emits js/riscvemu64-wasm.{js,wasm} alongside the source; we then copy them
# into JSLINUX_DIR so the iframe loader picks them up. This replaces the
# previous bellard.org download — yetty's tinyemu has elf.c (so BIOS can be
# the .elf directly) and matches the SBI implementation that yetty's kernel
# was built against.
set(_TINYEMU_SRC_DIR "${YETTY_ROOT}/src/tinyemu")
set(_TINYEMU_WASM_JS   "${_TINYEMU_SRC_DIR}/js/riscvemu64-wasm.js")
set(_TINYEMU_WASM_WASM "${_TINYEMU_SRC_DIR}/js/riscvemu64-wasm.wasm")

# All .c files in src/tinyemu that the wasm link consumes (kept loose; if a
# new file is needed by a future cfg/feature, add it here so DEPENDS is right).
file(GLOB _TINYEMU_WASM_DEPS
    "${_TINYEMU_SRC_DIR}/*.c"
    "${_TINYEMU_SRC_DIR}/*.h"
    "${_TINYEMU_SRC_DIR}/js/lib.js"
    "${_TINYEMU_SRC_DIR}/Makefile.js")

# Use plain `make`, not `$(MAKE)` — the latter is meaningful only inside a
# Make-invoked recipe and trips ninja's `$`-escape parser at build.ninja
# generation time.
find_program(_MAKE_BIN NAMES gmake make REQUIRED)
add_custom_command(
    OUTPUT ${_TINYEMU_WASM_JS} ${_TINYEMU_WASM_WASM}
    COMMAND ${CMAKE_COMMAND} -E env EMCC_SKIP_SANITY_CHECK=1
            ${_MAKE_BIN} -f Makefile.js js/riscvemu64-wasm.js
    WORKING_DIRECTORY ${_TINYEMU_SRC_DIR}
    DEPENDS ${_TINYEMU_WASM_DEPS}
    COMMENT "Building yetty riscvemu64-wasm.{js,wasm} from src/tinyemu"
    VERBATIM
)

add_custom_command(
    OUTPUT ${JSLINUX_DIR}/riscvemu64-wasm.js ${JSLINUX_DIR}/riscvemu64-wasm.wasm
    COMMAND ${CMAKE_COMMAND} -E copy ${_TINYEMU_WASM_JS} ${JSLINUX_DIR}/riscvemu64-wasm.js
    COMMAND ${CMAKE_COMMAND} -E copy ${_TINYEMU_WASM_WASM} ${JSLINUX_DIR}/riscvemu64-wasm.wasm
    DEPENDS ${_TINYEMU_WASM_JS} ${_TINYEMU_WASM_WASM}
    COMMENT "Staging yetty riscvemu64-wasm into ${JSLINUX_DIR}"
    VERBATIM
)
add_custom_target(jslinux-riscvemu-wasm
    DEPENDS ${JSLINUX_DIR}/riscvemu64-wasm.js ${JSLINUX_DIR}/riscvemu64-wasm.wasm
)

# --- splitimg host tool ------------------------------------------------------
# Bellard's wasm jslinux fetches drive0 in chunks via the format produced by
# tinyemu's splitimg.c (one chunk per `blk000000NNN.bin`, manifest in
# blk.txt). splitimg is a single-file C program with no deps. We need it as a
# *host* binary even under emcmake — emcmake retargets CMAKE_C_COMPILER to
# emcc, so we drop down to a raw shell command using $HOSTCC (set by the
# webasm build) or, failing that, any cc/gcc/clang on PATH.
#
# NO splitimg — use raw images directly, like desktop tinyemu does
# Browser downloads whole disk in ONE request

# --- alpine-disk (raw image) -----
if(_ALPINE_DISK_IMG AND EXISTS "${_ALPINE_DISK_IMG}")
    add_custom_target(jslinux-alpine-disk
        DEPENDS ${_ALPINE_DISK_IMG}
    )
else()
    message(WARNING "jslinux: alpine-rootfs.img not found at '${_ALPINE_DISK_IMG}' — alpine-disk variant will be unavailable")
    add_custom_target(jslinux-alpine-disk)
endif()

# --- alpine-extended-disk (raw image) -----
if(_ALPINE_EXTENDED_IMG AND EXISTS "${_ALPINE_EXTENDED_IMG}")
    add_custom_target(jslinux-alpine-extended-disk
        DEPENDS ${_ALPINE_EXTENDED_IMG}
    )
else()
    message(WARNING "jslinux: alpine-extended-rootfs.img not found at '${_ALPINE_EXTENDED_IMG}' — alpine-extended-disk variant will be unavailable")
    add_custom_target(jslinux-alpine-extended-disk)
endif()
add_custom_command(
    OUTPUT ${LOCAL_FILES_DIR}/filelist.txt
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build-local-vfsync.sh ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${YETTY_ROOT}
    COMMENT "Building local files for JSLinux VM import (demo/tools)"
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/build-local-vfsync.sh
)

add_custom_target(local-files
    DEPENDS ${LOCAL_FILES_DIR}/filelist.txt
)

# Create custom target for jslinux assets
add_custom_target(jslinux-assets
    DEPENDS
        ${JSLINUX_DIR}/x86_64emu-wasm.js
        ${JSLINUX_DIR}/x86_64emu-wasm.wasm
        ${JSLINUX_DIR}/kernel-x86_64.bin
        ${JSLINUX_DIR}/alpine-x86_64.cfg
        ${JSLINUX_DIR}/alpine-disk.cfg
        ${JSLINUX_DIR}/alpine-extended-disk.cfg
        ${JSLINUX_DIR}/term-bridge.js
        ${JSLINUX_DIR}/vm-bridge.html
        jslinux-riscvemu-wasm
        jslinux-alpine-disk
        jslinux-alpine-extended-disk
        local-files
    COMMENT "JSLinux assets ready"
)

# Export disk image path for incbin embedding in webasm target
set(JSLINUX_ALPINE_DISK_IMG "${JSLINUX_DIR}/alpine-rootfs.img" PARENT_SCOPE)
set(JSLINUX_ALPINE_DISK_CFG "${JSLINUX_DIR}/alpine-disk.cfg" PARENT_SCOPE)
