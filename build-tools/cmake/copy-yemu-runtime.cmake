# Helper invoked by tinyemu-iframe.cmake at PRE_LINK time.
#
# Copies kernel-riscv64.bin, opensbi-fw_jump.elf, and the alpine rootfs
# image from the build's assets/yemu/ tree into the bundle directory the
# emscripten --preload-file step picks up. Tolerates files not yet
# existing — alpine-rootfs.cmake produces them via add_custom_target, so
# the copy is best-effort and re-runs on each build.

if(NOT DEFINED SRC OR NOT DEFINED DST)
    message(FATAL_ERROR "copy-yemu-runtime.cmake: SRC and DST required")
endif()

file(MAKE_DIRECTORY "${DST}")

# Files we expect to ship with the iframe wasm. If any are missing the
# emscripten preload step will still proceed; the VM will then fail at
# runtime with a clear "open failed" log from bridge_block_device_init.
set(_FILES
    kernel-riscv64.bin
    opensbi-fw_jump.elf
    alpine-extended-rootfs.img)

foreach(_f ${_FILES})
    if(EXISTS "${SRC}/${_f}")
        configure_file("${SRC}/${_f}" "${DST}/${_f}" COPYONLY)
    else()
        message(STATUS "copy-yemu-runtime: skipping (not yet built): ${_f}")
    endif()
endforeach()
