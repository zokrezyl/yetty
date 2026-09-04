# yinstall — the installer executables.
#
# Builds on the desktop platforms only (Linux / macOS / Windows). An installer
# carries the yetty product as an embedded payload and unpacks it on first
# run. Three variants are built from the same sources; they differ only in
# which components they embed (see src/yetty/yinstall/README.md, "Variants"):
#
#   yinstall-min   yetty alone: the terminal binary (plus the runtime DLLs it
#                  needs on Windows), its shaders, the raw TTF/OTF fonts and
#                  the default config. No companion tools, no MSDF font
#                  databases, no greeter/demo assets, no VM runtime.
#   yinstall       the desktop toolset: every shipped executable (yetty, the
#                  companion CLIs, the demo programs), shaders, the raw fonts
#                  AND the MSDF font databases, config, greeter + demo assets.
#                  Everything except the RISC-V VM runtime.
#   yinstall-max   everything yinstall has, plus the RISC-V VM runtime
#                  (kernel, firmware, root filesystem) and the QEMU emulator.
#
# Payload comes from two places:
#   - data / yconfig / yemu / qemu  → the same configure-time staging the
#     main yetty binary uses (yetty_stage_embed_assets() in shared.cmake),
#     staged once here and embedded per variant.
#   - bin/ (the executables)        → a build-time mechanism, because the
#     binaries are build products that don't exist at configure time
#     (yinstall_embed_executables() below).
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT WIN32 AND NOT APPLE)
    return()  # webasm and anything else without a desktop install model
endif()
if(YETTY_IOS OR YETTY_TVOS OR YETTY_ANDROID OR ANDROID)
    # Apple is true for iOS/tvOS too; those (and Android) package via the app
    # bundle / rootfs, not an installer. Match the module guard in
    # src/yetty/CMakeLists.txt, which builds yetty_yinstall only on desktop.
    return()
endif()
if(NOT YETTY_ENABLE_LIB_INCBIN)
    # Without embedding there is no payload to install; skip the tool rather
    # than ship an installer that installs nothing.
    return()
endif()

include(${YETTY_ROOT}/build-tools/yetty/incbin.cmake)

find_program(BROTLI_EXECUTABLE brotli)
if(NOT BROTLI_EXECUTABLE)
    message(FATAL_ERROR "yinstall: brotli not found — needed to compress the embedded executables")
endif()
if(NOT DEFINED BROTLI_QUALITY)
    set(BROTLI_QUALITY 6)
endif()

# ---------------------------------------------------------------------
# yinstall_compress_executable(ITEM OUT_BR OUT_ASSET OUT_SAFE)
#
# Resolves one bin/ payload item — a build target (an executable we ship)
# or a literal file path (a runtime DLL the apps need next to them on
# Windows) — to its brotli'd blob path, its logical asset name and a
# C-identifier-safe symbol stem, and declares the build-time compression
# rule for it exactly once. Every installer variant that embeds the same
# item shares the one .br (the input is identical), so the rule is keyed
# on the directory property YINSTALL_COMPRESSED_ITEMS instead of being
# redeclared per target — CMake refuses two rules for one OUTPUT.
#
# The logical asset name must carry the platform executable suffix so the
# installed file is runnable: `bin/yetty.exe` on Windows, `bin/yetty`
# elsewhere. $<TARGET_FILE:exe> already produces the .exe binary; only its
# embedded *name* needs the suffix (mirrors qemu_embed_runtime's .exe pick).
# ---------------------------------------------------------------------
function(yinstall_compress_executable ITEM OUT_BR OUT_ASSET OUT_SAFE)
    if(WIN32)
        set(_exe_suffix ".exe")
    else()
        set(_exe_suffix "")
    endif()

    # Quote ITEM: literal DLL paths contain spaces and parentheses
    # ("Program Files (x86)") that would otherwise split the if() args.
    if(TARGET "${ITEM}")
        set(_source "$<TARGET_FILE:${ITEM}>")
        set(_asset "${ITEM}${_exe_suffix}")
        string(MAKE_C_IDENTIFIER "${ITEM}" _safe)
    else()
        # Literal file (e.g. a runtime DLL from the Windows SDK). The asset
        # keeps the file's basename.
        set(_source "${ITEM}")
        get_filename_component(_asset "${ITEM}" NAME)
        string(MAKE_C_IDENTIFIER "${_asset}" _safe)
    endif()

    set(_br_dir "${CMAKE_CURRENT_BINARY_DIR}/embed-bin")
    set(_br "${_br_dir}/${_safe}.br")

    get_property(_declared DIRECTORY PROPERTY YINSTALL_COMPRESSED_ITEMS)
    if(NOT "${_safe}" IN_LIST _declared)
        file(MAKE_DIRECTORY "${_br_dir}")
        # The .br path is fixed and the rule reruns whenever the source changes.
        add_custom_command(
            OUTPUT "${_br}"
            COMMAND "${BROTLI_EXECUTABLE}" -q ${BROTLI_QUALITY} -f -o "${_br}" "${_source}"
            DEPENDS "${_source}"
            COMMENT "yinstall: compressing ${_asset}"
            VERBATIM)
        set_property(DIRECTORY APPEND PROPERTY YINSTALL_COMPRESSED_ITEMS "${_safe}")
    endif()

    set(${OUT_BR} "${_br}" PARENT_SCOPE)
    set(${OUT_ASSET} "${_asset}" PARENT_SCOPE)
    set(${OUT_SAFE} "${_safe}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------
# yinstall_embed_executables(TARGET item1 item2 ...)
#
# Embeds built executables under the "bin/" prefix. Unlike incbin_add_directory
# (which globs + compresses at configure time), this works on build products:
# each binary is brotli-compressed at build time and embedded via .incbin
# (GCC/Clang) or RT_RCDATA (MSVC, which has no inline asm — mirrors the split
# in incbin.cmake). The runtime inflates + chmods on extraction. Emits a
# <target>_bin_manifest.h matching the shape the platform backend consumes
# (register_bin_assets_c + HAS_BIN_MANIFEST), every entry marked compressed.
# ---------------------------------------------------------------------
function(yinstall_embed_executables TARGET)
    set(_manifest "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_bin_manifest.h")

    # `_entries` carries the C-identifier-safe symbol stem, the *final* asset
    # name (already suffixed for targets, basename for files) and the .br
    # path per item, so the two backends below just emit
    # cb("bin/<asset>", ...) verbatim.
    set(_br_files "")
    set(_entries "")  # "<safe>|<asset>|<br>" per item
    foreach(_item ${ARGN})
        yinstall_compress_executable("${_item}" _br _asset _safe)
        if(TARGET "${_item}")
            add_dependencies(${TARGET} ${_item})
        endif()
        list(APPEND _br_files "${_br}")
        list(APPEND _entries "${_safe}|${_asset}|${_br}")
    endforeach()

    if(MSVC)
        # RCDATA path: rc.exe streams each .br into the PE at build time;
        # the generated register_bin_assets_c reads them back via FindResource.
        enable_language(RC)
        set(_rc "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_bin.rc")
        set(_reg "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_bin_register.c")
        file(WRITE "${_rc}" "// GENERATED — do not edit.\n")
        file(WRITE "${_reg}"
"// GENERATED — do not edit.
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <stddef.h>

typedef void (*bin_asset_callback_t)(const char* name, const uint8_t* data, size_t size, int compressed);

void register_bin_assets_c(bin_asset_callback_t cb)
{
    HMODULE module_handle = GetModuleHandleW(NULL);
    HRSRC res_handle;
    HGLOBAL res_global;
    DWORD res_size;
    const void *res_ptr;
")
        foreach(_entry ${_entries})
            string(REPLACE "|" ";" _parts "${_entry}")
            list(GET _parts 0 _safe)
            list(GET _parts 1 _asset)
            list(GET _parts 2 _br)
            string(TOUPPER "bin_${_safe}" _rcname)
            file(APPEND "${_rc}" "${_rcname} RCDATA \"${_br}\"\n")
            file(APPEND "${_reg}"
"    res_handle = FindResourceA(module_handle, \"${_rcname}\", (LPCSTR)RT_RCDATA);
    if (res_handle) {
        res_size = SizeofResource(module_handle, res_handle);
        res_global = LoadResource(module_handle, res_handle);
        if (res_global) {
            res_ptr = LockResource(res_global);
            if (res_ptr && res_size) cb(\"bin/${_asset}\", (const uint8_t *)res_ptr, (size_t)res_size, 1);
        }
    }
")
        endforeach()
        file(APPEND "${_reg}" "}\n")

        # rc.exe reads each .br at resource-compile time → the .rc must rebuild
        # after they're produced.
        set_source_files_properties("${_rc}" PROPERTIES OBJECT_DEPENDS "${_br_files}")
        target_sources(${TARGET} PRIVATE "${_rc}" "${_reg}")

        file(WRITE "${_manifest}"
"/* GENERATED — do not edit. yinstall embedded-executables manifest (RCDATA). */
#ifndef YINSTALL_BIN_MANIFEST_H
#define YINSTALL_BIN_MANIFEST_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern \"C\" {
#endif
typedef void (*bin_asset_callback_t)(const char* name, const uint8_t* data, size_t size, int compressed);
void register_bin_assets_c(bin_asset_callback_t cb);
#ifdef __cplusplus
}
#endif
#endif
")
    else()
        # GCC/Clang: .incbin the .br blobs; a static-inline register walks them.
        set(_incbin_src "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_bin_incbin.c")
        set(_incbin_content "/* GENERATED — do not edit. */\n#include \"incbin.h\"\n\n")
        set(_manifest_externs "")
        set(_manifest_calls "")
        foreach(_entry ${_entries})
            string(REPLACE "|" ";" _parts "${_entry}")
            list(GET _parts 0 _safe)
            list(GET _parts 1 _asset)
            list(GET _parts 2 _br)
            set(_symbol "bin_${_safe}")
            string(APPEND _incbin_content "INCBIN(${_symbol}, \"${_br}\");\n")
            string(APPEND _manifest_externs
                "extern const unsigned char g${_symbol}Data[];\nextern const unsigned int g${_symbol}Size;\n")
            string(APPEND _manifest_calls
                "    cb(\"bin/${_asset}\", g${_symbol}Data, (size_t)g${_symbol}Size, 1);\n")
        endforeach()

        file(GENERATE OUTPUT "${_incbin_src}" CONTENT "${_incbin_content}")
        set_source_files_properties("${_incbin_src}" PROPERTIES OBJECT_DEPENDS "${_br_files}")
        target_sources(${TARGET} PRIVATE "${_incbin_src}")
        target_include_directories(${TARGET} PRIVATE ${INCBIN_INCLUDE_DIR})

        file(WRITE "${_manifest}"
"/* GENERATED — do not edit. yinstall embedded-executables manifest (incbin). */
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
    endif()

    target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_definitions(${TARGET} PRIVATE HAS_BIN_MANIFEST=1)
endfunction()

# ---------------------------------------------------------------------
# yinstall_write_manifest_umbrella(TARGET)
#
# incbin_add_directory() names its generated manifest header after the
# target (<target>_<prefix>_manifest.h) and defines HAS_<PREFIX>_MANIFEST on
# that target; yinstall_embed_executables() follows the same shape for bin/.
# The platform backends (yplatform/install/{incbin,winres}.c) are shared by
# every variant, so they include one fixed name — yinstall_manifests.h —
# which this writes per target into a private include dir: a guarded include
# for every prefix an installer can carry. A prefix the variant does not
# embed has no HAS_ guard, so its include line compiles out.
# ---------------------------------------------------------------------
function(yinstall_write_manifest_umbrella TARGET)
    set(_dir "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}-manifests")
    file(MAKE_DIRECTORY "${_dir}")
    set(_content "/* GENERATED — do not edit. Manifest headers embedded in ${TARGET}. */\n")
    string(APPEND _content "#ifndef YINSTALL_MANIFESTS_H\n#define YINSTALL_MANIFESTS_H\n")
    foreach(_prefix bin data greeter demos yconfig yemu qemu)
        string(TOUPPER "${_prefix}" _upper)
        string(APPEND _content
            "#ifdef HAS_${_upper}_MANIFEST\n#include \"${TARGET}_${_prefix}_manifest.h\"\n#endif\n")
    endforeach()
    string(APPEND _content "#endif /* YINSTALL_MANIFESTS_H */\n")
    # Write via a staging copy so the header's timestamp only moves when its
    # content does — a plain file(WRITE) would recompile the backend on every
    # reconfigure.
    file(WRITE "${_dir}/yinstall_manifests.h.in" "${_content}")
    configure_file("${_dir}/yinstall_manifests.h.in" "${_dir}/yinstall_manifests.h" COPYONLY)
    target_include_directories(${TARGET} PRIVATE "${_dir}")
endfunction()

# ---------------------------------------------------------------------
# yinstall_collect_executables(OUT_VAR ROOT_DIR)
#
# Walks the CMake directory graph rooted at ROOT_DIR (breadth-first over the
# SUBDIRECTORIES directory property) and returns every EXECUTABLE target
# defined anywhere beneath it. This lets the installer embed *all* tool
# binaries built in this configuration without a hand-curated list: a tool
# gated off by a feature flag never defines its target, so it is simply
# absent from the walk — the set stays correct across every feature
# combination. Relies on yinstall being configured last (see the top-level
# CMakeLists), by which point every tool target already exists.
# ---------------------------------------------------------------------
function(yinstall_collect_executables out_var root_dir)
    set(_found "")
    set(_worklist "${root_dir}")
    while(_worklist)
        list(POP_FRONT _worklist _current_dir)
        get_property(_dir_targets DIRECTORY "${_current_dir}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(_target ${_dir_targets})
            get_target_property(_target_type ${_target} TYPE)
            if(_target_type STREQUAL "EXECUTABLE")
                list(APPEND _found ${_target})
            endif()
        endforeach()
        get_property(_child_dirs DIRECTORY "${_current_dir}" PROPERTY SUBDIRECTORIES)
        list(APPEND _worklist ${_child_dirs})
    endwhile()
    set(${out_var} "${_found}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------
# yinstall_add_installer(TARGET)
#
# Creates one installer executable: the shared main.c, the platform backend,
# ytrace and the yinstall module library. The target name is baked in as the
# installer's own name (banner, --version) so a user — and a bug report —
# can tell the variants apart. The payload is attached by
# yinstall_add_variant().
# ---------------------------------------------------------------------
function(yinstall_add_installer TARGET)
    # Platform backend: Win32 RT_RCDATA on Windows, .incbin elsewhere.
    if(WIN32)
        set(_install_backend ${YETTY_ROOT}/src/yetty/yplatform/install/winres.c)
    else()
        set(_install_backend ${YETTY_ROOT}/src/yetty/yplatform/install/incbin.c)
    endif()

    # ytrace.c is compiled in directly (it has no exported lib target); it
    # resolves its ymutex_* / format-timestamp deps against yetty_yplatform_core.
    add_executable(${TARGET}
        main.c
        ${_install_backend}
        ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    )

    target_include_directories(${TARGET} PRIVATE
        ${YETTY_ROOT}/include
        ${YETTY_ROOT}/src
        ${CMAKE_CURRENT_BINARY_DIR}
    )

    target_link_libraries(${TARGET} PRIVATE
        yetty_yinstall
        yetty_yplatform_core
        Threads::Threads
    )
    if(WIN32)
        # advapi32 → RegCreateKeyEx/RegSetValueEx (user PATH); user32 →
        # SendMessageTimeout (WM_SETTINGCHANGE broadcast). Both used by the
        # add-to-user-PATH step in winres.c.
        target_link_libraries(${TARGET} PRIVATE advapi32 user32)
    endif()

    if(NOT MSVC)
        target_compile_options(${TARGET} PRIVATE -Wall -Wextra)
    endif()
    if(_yinstall_link_options)
        target_link_options(${TARGET} PRIVATE ${_yinstall_link_options})
    endif()

    target_compile_definitions(${TARGET} PRIVATE
        YETTY_BUILD_VERSION="${YETTY_BUILD_VERSION_STR}"
        YETTY_YINSTALL_NAME="${TARGET}")

    yinstall_write_manifest_umbrella(${TARGET})
endfunction()

# ---------------------------------------------------------------------
# yinstall_add_variant(TARGET
#     DATA_DIR <dir> CONFIG_DIR <dir> EXECUTABLES <item>...
#     [GREETER] [DEMOS] [VM])
#
# One installer variant = the installer executable + its embedded
# components. DATA_DIR / CONFIG_DIR are staged trees for the data/ and
# yconfig/ prefixes; EXECUTABLES is the bin/ payload (targets and literal
# runtime files). The flags add the optional components: ygreeter's assets
# (greeter/), the demo tree (demos/) and the RISC-V VM runtime (yemu/ +
# qemu/). A component that is not embedded is simply absent from the
# installer's manifest set — the install loop skips it.
# ---------------------------------------------------------------------
function(yinstall_add_variant TARGET)
    cmake_parse_arguments(ARG "GREETER;DEMOS;VM" "DATA_DIR;CONFIG_DIR" "EXECUTABLES" ${ARGN})

    yinstall_add_installer(${TARGET})

    incbin_add_directory(${TARGET} "data" "${ARG_DATA_DIR}" "*" TRUE)
    incbin_add_directory(${TARGET} "yconfig" "${ARG_CONFIG_DIR}" "*" TRUE)

    if(ARG_GREETER)
        # greeter/ — ygreeter's own assets (logos, intro video, pdf/readme
        # samples). ygreeter is thin on desktop and reads these from
        # <data_dir>/, so the installer carries them.
        incbin_add_directory(${TARGET} "greeter" "${_yinstall_greeter_dir}" "*" TRUE)
    endif()

    if(ARG_DEMOS AND EXISTS "${YETTY_ROOT}/demo")
        # demos/ — the demo tree (assets, scripts, sources) → <data_dir>/demos,
        # so the installed demo programs find their assets and the user can
        # explore them.
        incbin_add_directory(${TARGET} "demos" "${YETTY_ROOT}/demo" "*" TRUE)
    endif()

    if(ARG_VM)
        # yemu/ — kernel, firmware, rootfs; shipped pre-brotli'd, embedded
        # as-is. qemu/ — the emulator binary (brotli'd by qemu_embed_runtime).
        if(YETTY_EMBED_YEMU_DIR)
            incbin_add_directory(${TARGET} "yemu" "${YETTY_EMBED_YEMU_DIR}" "*" FALSE)
        endif()
        if(YETTY_ENABLE_LIB_QEMU_BINARY)
            qemu_embed_runtime(${TARGET})
        endif()
    endif()

    if(ARG_EXECUTABLES)
        yinstall_embed_executables(${TARGET} ${ARG_EXECUTABLES})
    else()
        message(WARNING "${TARGET}: no embeddable executables found in this configuration")
    endif()
endfunction()

# ---------------------------------------------------------------------
# Staging (configure time, shared by every variant)
# ---------------------------------------------------------------------

# Bakes YETTY_BUILD_VERSION (git short hash) for the banner + version marker.
yetty_compute_build_version()

# data / yconfig / yemu — the same staging the main binary uses, done once.
# Sets YETTY_EMBED_DATA_DIR / YETTY_EMBED_CONFIG_DIR / YETTY_EMBED_YEMU_DIR.
yetty_stage_embed_assets()

# data-min: shaders + the raw TTF/OTF fonts only — the full data/ tree minus
# the MSDF font databases (msdf-fonts/), which are the bulk of it.
set(_yinstall_data_min_dir "${CMAKE_CURRENT_BINARY_DIR}/embed-data-min")
file(REMOVE_RECURSE "${_yinstall_data_min_dir}")
file(MAKE_DIRECTORY "${_yinstall_data_min_dir}")
foreach(_sub shaders fonts)
    if(EXISTS "${YETTY_EMBED_DATA_DIR}/${_sub}")
        file(COPY "${YETTY_EMBED_DATA_DIR}/${_sub}" DESTINATION "${_yinstall_data_min_dir}")
    endif()
endforeach()

# config-min: config.yaml + defaults.yaml without the temu/ VM configs, which
# only matter alongside the yemu/ runtime the min variant does not carry.
set(_yinstall_config_min_dir "${CMAKE_CURRENT_BINARY_DIR}/embed-config-min")
file(REMOVE_RECURSE "${_yinstall_config_min_dir}")
file(MAKE_DIRECTORY "${_yinstall_config_min_dir}")
foreach(_file config.yaml defaults.yaml)
    if(EXISTS "${YETTY_EMBED_CONFIG_DIR}/${_file}")
        file(COPY "${YETTY_EMBED_CONFIG_DIR}/${_file}" DESTINATION "${_yinstall_config_min_dir}")
    endif()
endforeach()

# greeter/ — staged with basenames to match what ygreeter opens
# (e.g. <data_dir>/logo-1.jpeg).
set(_yinstall_greeter_dir "${CMAKE_CURRENT_BINARY_DIR}/embed-greeter")
file(REMOVE_RECURSE "${_yinstall_greeter_dir}")
file(MAKE_DIRECTORY "${_yinstall_greeter_dir}")
foreach(_F
        "assets/logo-1.jpeg"
        "assets/logo-2.jpeg"
        "assets/logo-3.jpeg"
        "assets/logo-4.jpeg"
        "assets/yetty-unchained-2.mp4"
        "test/ut/ypdf/pdf-sample.pdf"
        "README.md")
    if(EXISTS "${YETTY_ROOT}/${_F}")
        get_filename_component(_NAME "${_F}" NAME)
        configure_file("${YETTY_ROOT}/${_F}" "${_yinstall_greeter_dir}/${_NAME}" COPYONLY)
    endif()
endforeach()

# ---------------------------------------------------------------------
# The executable sets
# ---------------------------------------------------------------------

# bin/ — every executable built under tools/, plus the main yetty binary and
# the demo programs (which live outside tools/). The full installers ship the
# whole toolset rather than a curated subset; anything switched off in this
# configuration is absent from the directory walk, so the set stays robust
# across feature flags. An installer never embeds itself or its siblings.
yinstall_collect_executables(_yinstall_tool_exes "${YETTY_ROOT}/tools")

# Executables that must also ship but live outside tools/: the main yetty app
# and the demo programs (built under demo/ when YETTY_ENABLE_FEATURE_DEMO).
set(_yinstall_extra_exes
    yetty
    demo-ygui
    demo-ymgui
    demo-yplot
    demo-yvideo
)

set(_yinstall_all_exes "")
foreach(_candidate ${_yinstall_tool_exes} ${_yinstall_extra_exes})
    if(_candidate MATCHES "^yinstall(-min|-max)?$")
        continue()  # never embed an installer into an installer
    endif()
    if(TARGET ${_candidate})
        list(APPEND _yinstall_all_exes ${_candidate})
    endif()
endforeach()

# The min variant ships the terminal alone.
set(_yinstall_min_exes "")
if(TARGET yetty)
    list(APPEND _yinstall_min_exes yetty)
endif()

# Windows runtime DLLs — yetty and the other GPU tools link against the
# DirectX shader compiler (dxcompiler / dxil / d3dcompiler_47), which is NOT
# part of a stock Windows and must sit next to the .exe. The yetty build
# copies them beside its own binary at build time; the installer must carry
# them into BIN so the *installed* apps launch too — every variant, since
# yetty itself needs them. Embed straight from the resolved Windows SDK
# originals (present at configure time) — no dependency on any target's
# POST_BUILD copy, and DLLs the SDK lacks are skipped.
if(WIN32)
    include(${YETTY_ROOT}/build-tools/yetty/dawn-runtime-dlls.cmake)
    yetty_find_dawn_runtime_dlls(_yinstall_dawn_dlls)
    if(_yinstall_dawn_dlls)
        list(APPEND _yinstall_all_exes ${_yinstall_dawn_dlls})
        list(APPEND _yinstall_min_exes ${_yinstall_dawn_dlls})
    endif()

    # Mesa lavapipe ICD + Vulkan loader: the software-Vulkan fallback that
    # yframework prefers over WARP when no hardware adapter exists (Windows
    # ships neither a Vulkan loader nor any software Vulkan driver).
    include(${YETTY_ROOT}/build-tools/yetty/mesa-lavapipe.cmake)
    yetty_find_lavapipe_runtime_files(_yinstall_lavapipe_files)
    if(_yinstall_lavapipe_files)
        list(APPEND _yinstall_all_exes ${_yinstall_lavapipe_files})
        list(APPEND _yinstall_min_exes ${_yinstall_lavapipe_files})
    endif()
endif()

list(REMOVE_DUPLICATES _yinstall_all_exes)
list(REMOVE_DUPLICATES _yinstall_min_exes)

# ---------------------------------------------------------------------
# macOS x86_64 linker workaround.
#
# An installer carries the whole product — every executable (brotli-compressed)
# plus all data/qemu/rootfs/greeter/demo assets — as an embedded incbin
# payload, making these the largest links in the build. Apple's new linker
# (ld-prime) segfaults on this link on the x86_64 target specifically (Apple
# clang 17 / Xcode 16.4 on the Intel runner); the arm64 build links the
# identical payload without issue, so this is an ld-prime x86_64 defect, not a
# size limit. Fall back to the classic Apple linker for these targets on
# x86_64, where the same payload links cleanly. Probe for the flag first so a
# future toolchain that drops -ld_classic degrades to the default linker rather
# than hard-erroring on an unknown option. yinstall_add_installer() applies
# it to every variant.
# ---------------------------------------------------------------------
set(_yinstall_link_options "")
if(APPLE AND (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64"
              OR CMAKE_OSX_ARCHITECTURES MATCHES "x86_64"))
    include(CheckLinkerFlag)
    check_linker_flag(C "-Wl,-ld_classic" YETTY_YINSTALL_HAS_LD_CLASSIC)
    if(YETTY_YINSTALL_HAS_LD_CLASSIC)
        set(_yinstall_link_options "-Wl,-ld_classic")
    endif()
endif()

# ---------------------------------------------------------------------
# The three installers
# ---------------------------------------------------------------------

yinstall_add_variant(yinstall-min
    DATA_DIR    "${_yinstall_data_min_dir}"
    CONFIG_DIR  "${_yinstall_config_min_dir}"
    EXECUTABLES ${_yinstall_min_exes}
)

yinstall_add_variant(yinstall
    DATA_DIR    "${YETTY_EMBED_DATA_DIR}"
    CONFIG_DIR  "${YETTY_EMBED_CONFIG_DIR}"
    EXECUTABLES ${_yinstall_all_exes}
    GREETER DEMOS
)

yinstall_add_variant(yinstall-max
    DATA_DIR    "${YETTY_EMBED_DATA_DIR}"
    CONFIG_DIR  "${YETTY_EMBED_CONFIG_DIR}"
    EXECUTABLES ${_yinstall_all_exes}
    GREETER DEMOS VM
)
