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
include(${YETTY_ROOT}/build-tools/cmake/tinyemu.cmake)

# brotli decoder for the iframe's runtime asset preload — kernel /
# opensbi / rootfs are shipped brotli-compressed and decoded into MEMFS
# from the iframe's pre-js. WEBASM-ONLY include — desktop / iOS /
# Android tinyemu builds are unaffected (they live in their own
# tinyemu-pty.c paths and don't pull this cmake file at all).
include(${YETTY_ROOT}/build-tools/cmake/libs/brotli.cmake)

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
    "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_tinyemu_bridge_input','_tinyemu_bridge_resize','_tinyemu_bridge_start','_yetty_brotli_decode']"
)

# VM runtime assets are NOT bundled into a tinyemu.data sidecar
# (--preload-file produces an *uncompressed* archive — alpine-extended
# alone would be 707 MB on the wire). Instead, ship the brotli'd
# originals as standalone files under build/tinyemu-assets/, fetched
# by the iframe page at startup and decoded into MEMFS via the brotli
# decoder linked into tinyemu.wasm. ~700 MB raw → ~250 MB on the wire.
#
# Layout under ${CMAKE_BINARY_DIR}/tinyemu-assets/ :
#
#   kernel-riscv64.bin.br
#   opensbi-fw_jump.elf.br
#   alpine-extended-rootfs.img.br
#   yetty-temu-extended.cfg          (uncompressed — tiny)
#   manifest.json
#
# manifest.json schema mirrors yetty-assets:
#   { "version": "<stamp>",
#     "entries": [ { "url": "X.br", "dest": "/yetty-vm/yemu/X", "brotli": true }, ... ] }
if(NOT TINYEMU_KERNEL_PATH OR NOT TINYEMU_OPENSBI_PATH OR NOT TINYEMU_ALPINE_EXTENDED_IMG)
    message(FATAL_ERROR
        "tinyemu-iframe: missing kernel/opensbi/rootfs paths — shared.cmake "
        "must be included before tinyemu-iframe.cmake")
endif()

# The .br files live next to the decompressed ones in 3rdparty-fetch's
# output (the alpine-disk / linux / opensbi tarballs ship both flavours).
set(_KERNEL_BR  "${TINYEMU_KERNEL_PATH}.br")
set(_OPENSBI_BR "${TINYEMU_OPENSBI_PATH}.br")
set(_ROOTFS_BR  "${TINYEMU_ALPINE_EXTENDED_IMG}.br")
foreach(_F "${_KERNEL_BR}" "${_OPENSBI_BR}" "${_ROOTFS_BR}")
    if(NOT EXISTS "${_F}")
        message(FATAL_ERROR
            "tinyemu-iframe: expected pre-brotli'd VM asset not found: ${_F}\n"
            "  the 3rdparty-fetch tarballs should ship both raw + .br files.")
    endif()
endforeach()

set(YETTY_TINYEMU_ASSETS_DIR "${CMAKE_BINARY_DIR}/tinyemu-assets" CACHE INTERNAL "")
file(REMOVE_RECURSE "${YETTY_TINYEMU_ASSETS_DIR}")
file(MAKE_DIRECTORY "${YETTY_TINYEMU_ASSETS_DIR}")

# Copy at configure time — these inputs change very rarely (different
# alpine-disk version is a 3rdparty-tag bump, which requires re-config
# anyway). Same pattern as yetty_stage_webasm_assets.
configure_file("${_KERNEL_BR}"
    "${YETTY_TINYEMU_ASSETS_DIR}/kernel-riscv64.bin.br" COPYONLY)
configure_file("${_OPENSBI_BR}"
    "${YETTY_TINYEMU_ASSETS_DIR}/opensbi-fw_jump.elf.br" COPYONLY)
configure_file("${_ROOTFS_BR}"
    "${YETTY_TINYEMU_ASSETS_DIR}/alpine-extended-rootfs.img.br" COPYONLY)
configure_file("${YETTY_ROOT}/assets/yemu/temu/yetty-temu-extended.cfg"
    "${YETTY_TINYEMU_ASSETS_DIR}/yetty-temu-extended.cfg" COPYONLY)

# Manifest. dest paths match BRIDGE_CFG_PATH ("/yetty-vm/...") in
# tinyemu-bridge.c and the cfg's $YETTY_DATA_DIR expansion.
string(TIMESTAMP _BUILD_STAMP "%Y%m%d%H%M%S")
file(WRITE "${YETTY_TINYEMU_ASSETS_DIR}/manifest.json"
"{
  \"version\": \"${_BUILD_STAMP}\",
  \"entries\": [
    { \"url\": \"kernel-riscv64.bin.br\",       \"dest\": \"/yetty-vm/yemu/kernel-riscv64.bin\",       \"brotli\": true  },
    { \"url\": \"opensbi-fw_jump.elf.br\",      \"dest\": \"/yetty-vm/yemu/opensbi-fw_jump.elf\",      \"brotli\": true  },
    { \"url\": \"alpine-extended-rootfs.img.br\", \"dest\": \"/yetty-vm/yemu/alpine-extended-rootfs.img\", \"brotli\": true  },
    { \"url\": \"yetty-temu-extended.cfg\",     \"dest\": \"/yetty-vm/yetty-temu-extended.cfg\",       \"brotli\": false }
  ]
}
")

message(STATUS "tinyemu-iframe: staged 4 VM assets in ${YETTY_TINYEMU_ASSETS_DIR}")

# Place tinyemu.{js,wasm,data} next to yetty.{js,wasm} so tinyemu-iframe.html
# can load them with relative URLs.
set_target_properties(tinyemu_vm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

message(STATUS "tinyemu-iframe: building tinyemu.{js,wasm,data} for iframe VM")
