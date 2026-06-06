# minimp4 — header-only minimalistic MP4 demuxer (MIT, lieff).
#
# Consumes a prebuilt noarch tarball (just minimp4.h) from the 3rdparty
# release published by build-3rdparty-minimp4.yml.

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

if(TARGET minimp4)
    return()
endif()

yetty_3rdparty_fetch(minimp4 _MINIMP4_DIR)

if(NOT EXISTS "${_MINIMP4_DIR}/include/minimp4.h")
    message(FATAL_ERROR
        "minimp4: minimp4.h not found in ${_MINIMP4_DIR}/include/ — tarball layout changed?")
endif()

# Compile the minimp4 implementation EXACTLY ONCE into a real static
# library. Previously this target was INTERFACE, leaving each consumer
# (vnc-server.c + yvideo-mp4.c) to `#define MINIMP4_IMPLEMENTATION`
# and inline-instantiate the implementation. That works as long as
# only one of them ends up in a given link, but as soon as both libs
# (libyetty_vnc.a + libyetty_yvideo_core.a) appear in the same binary
# the linker hits MP4E_open / MP4D_open / … duplicates.
#
# Generate one impl.c, build a STATIC lib, and have consumers PUBLIC
# include the header — no more in-TU implementation defines.
set(_MINIMP4_IMPL_C "${CMAKE_BINARY_DIR}/minimp4-impl.c")
file(WRITE "${_MINIMP4_IMPL_C}"
     "#define MINIMP4_IMPLEMENTATION\n"
     "#include <minimp4.h>\n")

add_library(minimp4 STATIC "${_MINIMP4_IMPL_C}")

# Vendored single-header 3rdparty: its own code emits -Wsign-compare /
# -Wunused-{variable,function}. We do not patch upstream — silence the
# whole TU (matches the vterm / imgui / wasm3 / cdb vendored targets).
if(MSVC)
    target_compile_options(minimp4 PRIVATE /w)
else()
    target_compile_options(minimp4 PRIVATE -w)
endif()

# SYSTEM so consumers that PUBLIC-include <minimp4.h> (yvideo-mp4.c,
# vnc-server.c) likewise don't inherit warnings from the vendored header.
target_include_directories(minimp4 SYSTEM PUBLIC "${_MINIMP4_DIR}/include")

message(STATUS "minimp4: prebuilt @${YETTY_3RDPARTY_minimp4_VERSION} (${_MINIMP4_DIR}/include/minimp4.h)")
