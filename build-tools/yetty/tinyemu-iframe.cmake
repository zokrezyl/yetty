# tinyemu-iframe.cmake — separate wasm artifact running TinyEMU in an iframe.
#
# This produces tinyemu.{js,wasm,data} alongside yetty.{js,wasm}. The
# iframe page (build-tools/web/tinyemu-iframe.html) loads tinyemu.js, and
# the parent yetty.wasm talks to it over postMessage via the iframe-pty.c
# backend. Single-threaded — no -pthread, plain libc, no pthread shim
# overhead. See src/yetty/yplatform/webasm/tinyemu-bridge.c for the entry
# point + protocol.
#
# Only built when the parent project is webasm and tinyemu is enabled.
if(NOT EMSCRIPTEN)
    return()
endif()
if(TARGET tinyemu_vm)
    return()
endif()

# tinyemu (the static lib) is already built by tinyemu.cmake with the same
# global flags as everything else. On the webasm parent build we DROP
# -pthread globally, so this static lib is also built without -pthread —
# perfect for linking into a single-threaded iframe wasm.
include(${YETTY_ROOT}/build-tools/yetty/tinyemu.cmake)

# brotli decoder for the iframe's runtime asset preload — kernel /
# opensbi / rootfs are shipped brotli-compressed and decoded into MEMFS
# from the iframe's pre-js. WEBASM-ONLY include — desktop / iOS /
# Android tinyemu builds are unaffected (they live in their own
# tinyemu-pty.c paths and don't pull this cmake file at all).
include(${YETTY_ROOT}/build-tools/yetty/libs/brotli.cmake)

set(TINYEMU_BRIDGE_SOURCE ${YETTY_ROOT}/src/yetty/yplatform/webasm/tinyemu-bridge.c)

# Reuse the same brotli wrapper yetty.wasm uses — symbol name
# `yetty_brotli_decode` is exported on this Module too. JS callers
# inside the iframe page get their own Module instance, so there's no
# cross-wasm collision.
set(TINYEMU_BROTLI_GLUE ${YETTY_ROOT}/src/yetty/yplatform/webasm/brotli-glue.c)

add_executable(tinyemu_vm ${TINYEMU_BRIDGE_SOURCE} ${TINYEMU_BROTLI_GLUE})
set_target_properties(tinyemu_vm PROPERTIES
    OUTPUT_NAME "tinyemu"
    SUFFIX ".js")

target_link_libraries(tinyemu_vm PRIVATE
    tinyemu
    brotlidec
    brotlicommon)

target_compile_definitions(tinyemu_vm PRIVATE
    CONFIG_SLIRP=1
    _GNU_SOURCE)

# IMPORTANT: no -pthread, no shared memory, no Asyncify. This wasm runs
# single-threaded inside its iframe, so libc syscalls (pipe, select,
# fread, …) hit MEMFS directly with zero cross-thread coordination.
target_link_options(tinyemu_vm PRIVATE
    -sFILESYSTEM=1
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=64MB
    -sSTACK_SIZE=1048576
    -sWASM_BIGINT
    -sASSERTIONS=1
    -sEXIT_RUNTIME=0
    # HEAPU32 is needed by the brotli wrapper — JS reads back the
    # (out_ptr, out_len) pair via Module.HEAPU32[scratch >> 2] / [+1].
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','HEAPU8','HEAPU32','callMain']"
    # _yetty_brotli_decode is the JS-callable wrapper around brotlidec
    # (src/yetty/yplatform/webasm/brotli-glue.c) — used by the iframe's
    # runtime preload to decompress kernel / opensbi / rootfs from
    # tinyemu-assets/.
    # Session API (tinyemu_session_open / send / close) lets the
    # iframe page fan out multiple yetty terminals onto one in-VM
    # telnetd via slirp's chr-backend hook. See tinyemu-bridge.c.
    "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_tinyemu_bridge_input','_tinyemu_bridge_resize','_tinyemu_bridge_start','_tinyemu_session_open','_tinyemu_session_send','_tinyemu_session_close','_yetty_brotli_decode']"
)

# VM runtime assets are NOT bundled into a tinyemu.data sidecar
# (--preload-file produces an *uncompressed* archive — the unified
# rootfs alone would be ~300 MB on the wire). Instead, ship the brotli'd
# originals as standalone files under build/tinyemu-assets/, fetched by
# the iframe page at startup and decoded into MEMFS via the brotli
# decoder linked into tinyemu.wasm.
#
# Layout under ${CMAKE_BINARY_DIR}/tinyemu-assets/ :
#
#   kernel-riscv64.bin.br
#   opensbi-fw_jump.elf.br
#   yetty-rootfs-riscv.img.br        (alpine-extended + /yetty/{bin,repo})
#   yetty-temu-extended.cfg          (uncompressed — tiny)
#   manifest.json
#
# manifest.json schema mirrors yetty-assets:
#   { "version": "<stamp>",
#     "entries": [ { "url": "X.br", "dest": "/yetty-vm/yemu/X", "brotli": true,
#                    "sha256": "<hash of the staged file>" }, ... ] }
#
# The per-entry sha256 is the browser-side cache key (Cache Storage API
# in tinyemu-iframe.html) — the ~150 MB rootfs only re-downloads when
# its bytes actually change, not on every emulator start or deploy.
if(NOT TINYEMU_KERNEL_PATH OR NOT TINYEMU_OPENSBI_PATH)
    message(FATAL_ERROR
        "tinyemu-iframe: missing kernel/opensbi paths — shared.cmake "
        "must be included before tinyemu-iframe.cmake")
endif()

# The .br files live next to the decompressed ones in 3rdparty-fetch's
# output (the linux / opensbi tarballs ship both flavours; the unified
# yetty-rootfs-riscv tarball ships only .br + manifest).
#
# Kernel + opensbi are stable 3rdparty pins — required. The unified rootfs
# (yetty-rootfs-riscv) follows the same warn-and-skip pattern the old
# yetty-tools-riscv asset did: a fresh checkout (no release tagged yet) or
# an explicit BUILD_ROOTFS_RISCV=false in CI can omit it, in which case
# the iframe ships without the riscv VM bundle.
set(_KERNEL_BR  "${TINYEMU_KERNEL_PATH}.br")
set(_OPENSBI_BR "${TINYEMU_OPENSBI_PATH}.br")
foreach(_F "${_KERNEL_BR}" "${_OPENSBI_BR}")
    if(NOT EXISTS "${_F}")
        message(FATAL_ERROR
            "tinyemu-iframe: expected pre-brotli'd VM asset not found: ${_F}\n"
            "  the 3rdparty-fetch tarballs should ship both raw + .br files.")
    endif()
endforeach()

set(_ROOTFS_BR "")
if(YETTY_ROOTFS_RISCV_IMG AND EXISTS "${YETTY_ROOTFS_RISCV_IMG}.br")
    set(_ROOTFS_BR "${YETTY_ROOTFS_RISCV_IMG}.br")
else()
    message(WARNING
        "tinyemu-iframe: yetty-rootfs-riscv.img.br not found — VM bundle "
        "will be absent from the webasm iframe. Build with the "
        "yetty-rootfs-riscv asset present (release the tag or stage the "
        "tarball into the 3rdparty cache) to include it.")
endif()

set(YETTY_TINYEMU_ASSETS_DIR "${CMAKE_BINARY_DIR}/tinyemu-assets" CACHE INTERNAL "")
file(REMOVE_RECURSE "${YETTY_TINYEMU_ASSETS_DIR}")
file(MAKE_DIRECTORY "${YETTY_TINYEMU_ASSETS_DIR}")

# Copy at configure time — these inputs change very rarely (a different
# yetty-rootfs-riscv version is a release tag bump, which requires
# re-config anyway). Same pattern as yetty_stage_webasm_assets.
configure_file("${_KERNEL_BR}"
    "${YETTY_TINYEMU_ASSETS_DIR}/kernel-riscv64.bin.br" COPYONLY)
configure_file("${_OPENSBI_BR}"
    "${YETTY_TINYEMU_ASSETS_DIR}/opensbi-fw_jump.elf.br" COPYONLY)
configure_file("${YETTY_ROOT}/assets/yemu/temu/yetty-temu-extended.cfg"
    "${YETTY_TINYEMU_ASSETS_DIR}/yetty-temu-extended.cfg" COPYONLY)
if(_ROOTFS_BR)
    configure_file("${_ROOTFS_BR}"
        "${YETTY_TINYEMU_ASSETS_DIR}/yetty-rootfs-riscv.img.br" COPYONLY)
    file(SHA256 "${YETTY_TINYEMU_ASSETS_DIR}/yetty-rootfs-riscv.img.br" _ROOTFS_SHA)
    set(_ROOTFS_MANIFEST_ENTRY
        "    { \"url\": \"yetty-rootfs-riscv.img.br\",   \"dest\": \"/yetty-vm/yemu/yetty-rootfs-riscv.img\",   \"brotli\": true,  \"sha256\": \"${_ROOTFS_SHA}\" },\n")
else()
    set(_ROOTFS_MANIFEST_ENTRY "")
endif()

# Manifest. dest paths match BRIDGE_CFG_PATH ("/yetty-vm/...") in
# tinyemu-bridge.c and the cfg's $YETTY_DATA_DIR expansion.
file(SHA256 "${YETTY_TINYEMU_ASSETS_DIR}/kernel-riscv64.bin.br" _KERNEL_SHA)
file(SHA256 "${YETTY_TINYEMU_ASSETS_DIR}/opensbi-fw_jump.elf.br" _OPENSBI_SHA)
file(SHA256 "${YETTY_TINYEMU_ASSETS_DIR}/yetty-temu-extended.cfg" _TEMU_CFG_SHA)
string(TIMESTAMP _BUILD_STAMP "%Y%m%d%H%M%S")
file(WRITE "${YETTY_TINYEMU_ASSETS_DIR}/manifest.json"
"{
  \"version\": \"${_BUILD_STAMP}\",
  \"entries\": [
    { \"url\": \"kernel-riscv64.bin.br\",       \"dest\": \"/yetty-vm/yemu/kernel-riscv64.bin\",       \"brotli\": true,  \"sha256\": \"${_KERNEL_SHA}\" },
    { \"url\": \"opensbi-fw_jump.elf.br\",      \"dest\": \"/yetty-vm/yemu/opensbi-fw_jump.elf\",      \"brotli\": true,  \"sha256\": \"${_OPENSBI_SHA}\" },
${_ROOTFS_MANIFEST_ENTRY}    { \"url\": \"yetty-temu-extended.cfg\",     \"dest\": \"/yetty-vm/yetty-temu-extended.cfg\",       \"brotli\": false, \"sha256\": \"${_TEMU_CFG_SHA}\" }
  ]
}
")

if(_ROOTFS_BR)
    message(STATUS "tinyemu-iframe: staged 4 VM assets in ${YETTY_TINYEMU_ASSETS_DIR}")
else()
    message(STATUS "tinyemu-iframe: staged 3 VM assets in ${YETTY_TINYEMU_ASSETS_DIR} (no rootfs — VM bundle absent)")
endif()

# Place tinyemu.{js,wasm,data} next to yetty.{js,wasm} so tinyemu-iframe.html
# can load them with relative URLs.
set_target_properties(tinyemu_vm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

message(STATUS "tinyemu-iframe: building tinyemu.{js,wasm,data} for iframe VM")
