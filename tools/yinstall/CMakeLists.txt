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

# ---------------------------------------------------------------------
# yinstall_embed_executables(TARGET exe1 exe2 ...)
#
# Embeds built executables under the "bin/" prefix. Unlike incbin_add_directory
# (which globs + compresses at configure time), this works on build products:
# each binary is brotli-compressed at build time and embedded via .incbin
# (GCC/Clang) or RT_RCDATA (MSVC, which has no inline asm — mirrors the split
# in incbin.cmake). The runtime inflates + chmods on extraction. Emits a
# yinstall_bin_manifest.h matching the shape the platform backend consumes
# (register_bin_assets_c + HAS_BIN_MANIFEST), every entry marked compressed.
# ---------------------------------------------------------------------
function(yinstall_embed_executables TARGET)
    find_program(BROTLI_EXECUTABLE brotli)
    if(NOT BROTLI_EXECUTABLE)
        message(FATAL_ERROR "yinstall: brotli not found — needed to compress the embedded executables")
    endif()
    if(NOT DEFINED BROTLI_QUALITY)
        set(BROTLI_QUALITY 6)
    endif()

    # The logical asset name must carry the platform executable suffix so the
    # installed file is runnable: `bin/yetty.exe` on Windows, `bin/yetty`
    # elsewhere. $<TARGET_FILE:exe> already produces the .exe binary; only its
    # embedded *name* needed fixing (mirrors qemu_embed_runtime's .exe pick).
    if(WIN32)
        set(_exe_suffix ".exe")
    else()
        set(_exe_suffix "")
    endif()

    set(_manifest "${CMAKE_CURRENT_BINARY_DIR}/yinstall_bin_manifest.h")
    set(_br_dir "${CMAKE_CURRENT_BINARY_DIR}/embed-bin")
    file(MAKE_DIRECTORY "${_br_dir}")

    # Build-time brotli of each item — common to both backends. Each ARGN
    # entry is either a build target (an executable we ship) or a literal
    # file path (a runtime DLL the apps need next to them on Windows). The
    # .br path is fixed and the input reruns whenever the source changes.
    #
    # `_pairs` carries the C-identifier-safe symbol and the *final* asset
    # name (already suffixed for targets, basename for files), so the two
    # backends below just emit cb("bin/<asset>", ...) verbatim.
    set(_br_files "")
    set(_pairs "")  # "<safe>|<asset>" per item
    foreach(_item ${ARGN})
        # Quote _item: literal DLL paths contain spaces and parentheses
        # ("Program Files (x86)") that would otherwise split the if() args.
        if(TARGET "${_item}")
            set(_src "$<TARGET_FILE:${_item}>")
            set(_asset "${_item}${_exe_suffix}")
            string(MAKE_C_IDENTIFIER "${_item}" _safe)
            add_dependencies(${TARGET} ${_item})
        else()
            # Literal file (e.g. a runtime DLL from the Windows SDK). The
            # asset keeps the file's basename; the source may contain spaces
            # ("Program Files (x86)"), so it stays quoted below.
            set(_src "${_item}")
            get_filename_component(_asset "${_item}" NAME)
            string(MAKE_C_IDENTIFIER "${_asset}" _safe)
        endif()
        set(_br "${_br_dir}/${_safe}.br")
        add_custom_command(
            OUTPUT "${_br}"
            COMMAND "${BROTLI_EXECUTABLE}" -q ${BROTLI_QUALITY} -f -o "${_br}" "${_src}"
            DEPENDS "${_src}"
            COMMENT "yinstall: compressing ${_asset}"
            VERBATIM)
        list(APPEND _br_files "${_br}")
        list(APPEND _pairs "${_safe}|${_asset}")
    endforeach()

    if(MSVC)
        # RCDATA path: rc.exe streams each .br into the PE at build time;
        # the generated register_bin_assets_c reads them back via FindResource.
        enable_language(RC)
        set(_rc "${CMAKE_CURRENT_BINARY_DIR}/yinstall_bin.rc")
        set(_reg "${CMAKE_CURRENT_BINARY_DIR}/yinstall_bin_register.c")
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
        foreach(_pair ${_pairs})
            string(REPLACE "|" ";" _parts "${_pair}")
            list(GET _parts 0 _safe)
            list(GET _parts 1 _asset)
            string(TOUPPER "bin_${_safe}" _rcname)
            file(APPEND "${_rc}" "${_rcname} RCDATA \"${_br_dir}/${_safe}.br\"\n")
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
        set(_incbin_src "${CMAKE_CURRENT_BINARY_DIR}/yinstall_bin_incbin.c")
        set(_incbin_content "/* GENERATED — do not edit. */\n#include \"incbin.h\"\n\n")
        set(_manifest_externs "")
        set(_manifest_calls "")
        foreach(_pair ${_pairs})
            string(REPLACE "|" ";" _parts "${_pair}")
            list(GET _parts 0 _safe)
            list(GET _parts 1 _asset)
            set(_symbol "bin_${_safe}")
            string(APPEND _incbin_content "INCBIN(${_symbol}, \"${_br_dir}/${_safe}.br\");\n")
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
    # advapi32 → RegCreateKeyEx/RegSetValueEx (user PATH); user32 →
    # SendMessageTimeout (WM_SETTINGCHANGE broadcast). Both used by the
    # add-to-user-PATH step in winres.c.
    target_link_libraries(yinstall PRIVATE advapi32 user32)
endif()

if(NOT MSVC)
    target_compile_options(yinstall PRIVATE -Wall -Wextra)
endif()

# data / yconfig / yemu / qemu — reuse the main binary's asset staging
# verbatim. Bakes YETTY_BUILD_VERSION onto the target and generates the
# yinstall_{data,yconfig,yemu,qemu}_manifest.h headers the backend includes.
yetty_embed_assets(yinstall)

# greeter/ — ygreeter's own assets (logos, intro video, pdf/readme samples).
# ygreeter is thin on desktop and reads these from <data_dir>/, so the
# installer carries them. Staged with basenames to match what ygreeter opens
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
incbin_add_directory(yinstall "greeter" "${_yinstall_greeter_dir}" "*" TRUE)

# demos/ — the demo tree (assets, scripts, sources) → <data_dir>/demos, so the
# installed demo programs find their assets and the user can explore them.
if(EXISTS "${YETTY_ROOT}/demo")
    incbin_add_directory(yinstall "demos" "${YETTY_ROOT}/demo" "*" TRUE)
endif()

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

# Windows runtime DLLs — yetty and the other GPU tools link against the
# DirectX shader compiler (dxcompiler / dxil / d3dcompiler_47), which is NOT
# part of a stock Windows and must sit next to the .exe. The yetty build
# copies them beside its own binary at build time; the installer must carry
# them into BIN so the *installed* apps launch too. Embed straight from the
# resolved Windows SDK originals (present at configure time) — no dependency
# on any target's POST_BUILD copy, and DLLs the SDK lacks are skipped.
if(WIN32)
    include(${YETTY_ROOT}/build-tools/yetty/dawn-runtime-dlls.cmake)
    yetty_find_dawn_runtime_dlls(_yinstall_dawn_dlls)
    if(_yinstall_dawn_dlls)
        list(APPEND _yinstall_exes ${_yinstall_dawn_dlls})
    endif()
endif()

if(_yinstall_exes)
    yinstall_embed_executables(yinstall ${_yinstall_exes})
else()
    message(WARNING "yinstall: no embeddable executables found in this configuration")
endif()
