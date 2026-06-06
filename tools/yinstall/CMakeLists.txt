# yinstall — the installer executable.
#
# Builds on the desktop platforms only (Linux / macOS / Windows). It carries
# the whole yetty product as an embedded payload and unpacks it on first run.
#
# Payload comes from two places:
#   - data / yconfig / yemu / qemu  → the same configure-time staging the
#     main yetty binary uses, reused verbatim via yetty_embed_assets().
#   - bin/ (the executables)        → a build-time mechanism, because the
#     binaries are build products that don't exist at configure time
#     (yinstall_embed_executables() below).
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT WIN32 AND NOT APPLE)
    return()
endif()
if(NOT YETTY_ENABLE_LIB_INCBIN)
    # Without embedding there is no payload to install; skip the tool rather
    # than ship an installer that installs nothing.
    return()
endif()

include(${YETTY_ROOT}/build-tools/yetty/incbin.cmake)

# ---------------------------------------------------------------------
# yinstall_embed_executables(TARGET exe1 exe2 ...)
#
# Embeds built executables under the "bin/" prefix. Unlike incbin_add_directory
# (which globs + compresses at configure time), this resolves each target's
# output path with a generator expression and lets the assembler pull the
# bytes in at build time via .incbin — so it works on build products. Each
# binary is brotli-compressed at build time (executables are the bulk of the
# installer; raw they dwarf everything else), and the runtime inflates +
# chmods on extraction. Emits a yinstall_bin_manifest.h matching the shape the
# platform backend consumes (register_bin_assets_c + HAS_BIN_MANIFEST), with
# every entry marked compressed.
# ---------------------------------------------------------------------
function(yinstall_embed_executables TARGET)
    find_program(BROTLI_EXECUTABLE brotli)
    if(NOT BROTLI_EXECUTABLE)
        message(FATAL_ERROR "yinstall: brotli not found — needed to compress the embedded executables")
    endif()
    if(NOT DEFINED BROTLI_QUALITY)
        set(BROTLI_QUALITY 6)
    endif()

    set(_incbin_src "${CMAKE_CURRENT_BINARY_DIR}/yinstall_bin_incbin.c")
    set(_manifest "${CMAKE_CURRENT_BINARY_DIR}/yinstall_bin_manifest.h")
    set(_br_dir "${CMAKE_CURRENT_BINARY_DIR}/embed-bin")
    file(MAKE_DIRECTORY "${_br_dir}")

    set(_incbin_content "/* GENERATED — do not edit. */\n#include \"incbin.h\"\n\n")
    set(_manifest_externs "")
    set(_manifest_calls "")
    set(_br_files "")

    foreach(_exe ${ARGN})
        string(MAKE_C_IDENTIFIER "${_exe}" _safe)
        set(_symbol "bin_${_safe}")
        set(_br "${_br_dir}/${_safe}.br")
        # Compress the built binary at build time. The .br path is fixed (no
        # genex), while the input is $<TARGET_FILE:exe>; the command reruns
        # whenever that binary changes.
        add_custom_command(
            OUTPUT "${_br}"
            COMMAND "${BROTLI_EXECUTABLE}" -q ${BROTLI_QUALITY} -f -o "${_br}" "$<TARGET_FILE:${_exe}>"
            DEPENDS ${_exe}
            COMMENT "yinstall: compressing ${_exe}"
            VERBATIM)
        list(APPEND _br_files "${_br}")
        # .incbin the compressed blob; mark the manifest entry compressed.
        string(APPEND _incbin_content "INCBIN(${_symbol}, \"${_br}\");\n")
        string(APPEND _manifest_externs
            "extern const unsigned char g${_symbol}Data[];\nextern const unsigned int g${_symbol}Size;\n")
        string(APPEND _manifest_calls
            "    cb(\"bin/${_exe}\", g${_symbol}Data, (size_t)g${_symbol}Size, 1);\n")
        add_dependencies(${TARGET} ${_exe})
    endforeach()

    file(GENERATE OUTPUT "${_incbin_src}" CONTENT "${_incbin_content}")
    # The incbin object reads the .br files at assemble time, so it must be
    # (re)built after they're produced.
    set_source_files_properties("${_incbin_src}" PROPERTIES OBJECT_DEPENDS "${_br_files}")

    file(WRITE "${_manifest}"
"/* GENERATED — do not edit. yinstall embedded-executables manifest. */
#ifndef YINSTALL_BIN_MANIFEST_H
#define YINSTALL_BIN_MANIFEST_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern \"C\" {
#endif
${_manifest_externs}typedef void (*bin_asset_callback_t)(const char* name, const uint8_t* data, size_t size, int compressed);
static inline void register_bin_assets_c(bin_asset_callback_t cb) {
${_manifest_calls}}
#ifdef __cplusplus
}
#endif
#endif
")

    target_sources(${TARGET} PRIVATE "${_incbin_src}")
    target_include_directories(${TARGET} PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${INCBIN_INCLUDE_DIR})
    target_compile_definitions(${TARGET} PRIVATE HAS_BIN_MANIFEST=1)
endfunction()

# ---------------------------------------------------------------------
# The installer target
# ---------------------------------------------------------------------

# Platform backend: Win32 RT_RCDATA on Windows, .incbin elsewhere.
if(WIN32)
    set(_install_backend ${YETTY_ROOT}/src/yetty/yplatform/install/winres.c)
else()
    set(_install_backend ${YETTY_ROOT}/src/yetty/yplatform/install/incbin.c)
endif()

# ytrace.c is compiled in directly (it has no exported lib target); it
# resolves its ymutex_* / format-timestamp deps against yetty_yplatform_core.
add_executable(yinstall
    main.c
    ${_install_backend}
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yinstall PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

find_package(Threads REQUIRED)
target_link_libraries(yinstall PRIVATE
    yetty_yinstall
    yetty_yplatform_core
    Threads::Threads
)
if(WIN32)
    target_link_libraries(yinstall PRIVATE advapi32)
endif()

if(NOT MSVC)
    target_compile_options(yinstall PRIVATE -Wall -Wextra)
endif()

# data / yconfig / yemu / qemu — reuse the main binary's asset staging
# verbatim. Bakes YETTY_BUILD_VERSION onto the target and generates the
# yinstall_{data,yconfig,yemu,qemu}_manifest.h headers the backend includes.
yetty_embed_assets(yinstall)

# bin/ — the curated set of user-facing executables. Each is embedded only
# if it exists in this configuration, so the list stays robust across the
# feature flags. Internal codegen / dev tools are intentionally absent.
set(_yinstall_exe_candidates
    yetty
    ycat
    yplot
    ygreeter
    ydoc
    ysheet
    yslide
    yvideo
    yecho
    ymesh
    ytop
    ybrowser
    ydiagram-cli
    demo-ygui
    demo-ymgui
    demo-yplot
    demo-yvideo
)
set(_yinstall_exes "")
foreach(_candidate ${_yinstall_exe_candidates})
    if(TARGET ${_candidate})
        list(APPEND _yinstall_exes ${_candidate})
    endif()
endforeach()
if(_yinstall_exes)
    yinstall_embed_executables(yinstall ${_yinstall_exes})
else()
    message(WARNING "yinstall: no embeddable executables found in this configuration")
endif()
