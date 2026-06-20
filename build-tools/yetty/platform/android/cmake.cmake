# Android build target
include(${CMAKE_CURRENT_LIST_DIR}/variables.cmake)

if(YETTY_ENABLE_LIB_LIBCURL)
    include(${YETTY_ROOT}/build-tools/yetty/libs/libcurl.cmake)
endif()

include(${YETTY_ROOT}/build-tools/yetty/platform/shared.cmake)

# TinyEMU - in-process RISC-V emulator for --virtual flag. Not part of
# src/yetty, so shared.cmake doesn't pull it in; include explicitly like
# linux.cmake / macos.cmake do.
if(YETTY_ENABLE_LIB_TINYEMU)
    include(${YETTY_ROOT}/build-tools/yetty/tinyemu.cmake)
    include(${YETTY_ROOT}/build-tools/yetty/tinyemu-runtime.cmake)
endif()

# native_app_glue from Android NDK
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)
target_include_directories(native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue
)

# Toybox configuration
if(TOYBOX_PATH)
    add_compile_definitions(YETTY_TOYBOX_PATH="${TOYBOX_PATH}")
endif()

# Set ANDROID_ASSETS_DIR BEFORE adding yetty subdirectory so shaders/CMakeLists.txt can use it
# Note: Use CMAKE_BINARY_DIR not ANDROID_BUILD_DIR, as CMake operates within the cxx subdirectory
set(ANDROID_ASSETS_DIR "${CMAKE_BINARY_DIR}/assets")
file(MAKE_DIRECTORY ${ANDROID_ASSETS_DIR})

# Note: src/yetty is already added by shared.cmake (which recursively
# add_subdirectory's yvnc, yplot, etc. when their feature flags are ON —
# no need to repeat them here).

# Platform sources — Android-specific + shared Unix components (all C)
set(YETTY_PLATFORM_SOURCES
    ${YETTY_ROOT}/src/yetty/yplatform/ymain/android-glue.c
    ${YETTY_ROOT}/src/yetty/yplatform/ymain/android.c
    # The concrete app the android entry injects via yetty_yapp_create_app
    # (its run() builds the framework/terminal — same yetty:app as desktop/web).
    ${YETTY_ROOT}/src/yetty/yetty/app.c
    ${YETTY_ROOT}/src/yetty/yetty/rpc.gen.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/android.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/coroutine/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/yworkpool/default.c
    ${YETTY_ROOT}/src/yetty/ypty/forkpty.c
    ${YETTY_ROOT}/src/yetty/ypty/memory-pty.c
    ${YETTY_ROOT}/src/yetty/yplatform/pty-factory/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/pipe/default.c
    ${YETTY_ROOT}/src/yetty/yncbin/incbin-assets.c
    ${YETTY_ROOT}/src/yetty/ypty/temu-pty.c
)


# Create shared library with core sources + android platform
add_library(yetty SHARED
    ${YETTY_SOURCES}
    ${YETTY_CORE_SOURCES}
    ${YETTY_ANDROID_SOURCES}
    ${YETTY_PLATFORM_SOURCES}
)

target_include_directories(yetty PRIVATE ${YETTY_INCLUDES} ${YETTY_RENDERER_INCLUDES} ${JPEG_INCLUDE_DIRS} ${BROTLI_INCLUDE_DIR})

# Embed all assets (logo, shaders, fonts, CDB files)
yetty_embed_assets(yetty)

# Dummy platforms for dependency tracking (legacy)
add_custom_target(copy-shaders-for-incbin)
add_custom_target(copy-fonts-for-incbin)

target_compile_definitions(yetty PRIVATE
    ${YETTY_DEFINITIONS}
    YETTY_WEB=0
    YETTY_ANDROID=1
    YETTY_USE_PREBUILT_ATLAS=1
    YETTY_ASSETS_FROM_APK=1
    YETTY_HAS_VNC=1
    $<$<BOOL:${YETTY_ENABLE_LIB_TINYEMU}>:YETTY_HAS_TINYEMU=1>
    $<$<BOOL:${YETTY_ENABLE_LIB_TINYEMU}>:CONFIG_SLIRP>
    $<$<BOOL:${YETTY_ENABLE_LIB_QEMU}>:YETTY_HAS_QEMU=1>
    $<$<BOOL:${YETTY_ENABLE_FEATURE_SSH}>:YETTY_HAS_SSH=1>
)

set_target_properties(yetty PROPERTIES LIBRARY_OUTPUT_NAME "yetty")

target_link_libraries(yetty PRIVATE
    ${YETTY_LIBS}
    -Wl,--whole-archive
    native_app_glue
    -Wl,--no-whole-archive
    android
    log
    $<$<BOOL:${YETTY_ENABLE_LIB_TINYEMU}>:tinyemu>
    $<$<BOOL:${YETTY_ENABLE_LIB_QEMU}>:yetty_qemu>
    yetty_telnet
    yetty_yco
    yetty_yplatform_core
)

# Generate demo outputs (pre-run demo scripts to capture output for Android)
if(YETTY_ENABLE_FEATURE_DEMO)
    message(STATUS "Generating demo outputs for Android...")
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -DYETTY_ROOT=${YETTY_ROOT}
            -DOUTPUT_DIR=${ANDROID_ASSETS_DIR}
            -P ${YETTY_ROOT}/build-tools/yetty/generate-demo-outputs.cmake
        WORKING_DIRECTORY ${YETTY_ROOT}
    )
endif()

# Copy static assets to Android build directory
if(YETTY_ENABLE_FEATURE_ASSETS)
    file(GLOB ASSET_FILES "${YETTY_ROOT}/assets/*")
    file(COPY ${ASSET_FILES} DESTINATION ${ANDROID_ASSETS_DIR})
endif()

# Ensure shaders and assets are built before yetty
if(YETTY_ENABLE_FEATURE_ASSETS)
    add_dependencies(yetty copy-shaders copy-shaders-for-incbin copy-fonts-for-incbin)
endif()

# Copy prebuilt CDB fonts to Android assets dir after build. Source is the
# 3rdparty fetch dir populated by yetty_3rdparty_fetch(cdb). Files arrive
# brotli-pre-compressed (.cdb.br); the runtime decompresses on read.
if(YETTY_ENABLE_FEATURE_CDB_GEN AND DEFINED YETTY_3RDPARTY_cdb_DIR)
    add_custom_command(TARGET yetty POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${YETTY_3RDPARTY_cdb_DIR}" "${ANDROID_ASSETS_DIR}/msdf-fonts"
        COMMENT "Copying CDB fonts to Android assets (from 3rdparty fetch)"
    )
endif()

# Ship the QEMU binary as a JNI lib so it ends up in nativeLibraryDir at
# install time. SELinux denies execute_no_trans on plain app_data files
# on API 29+, so the binary in <data>/qemu/ can't be exec'd; the .so in
# /data/app/.../lib/<abi>/ can. Same trick toybox uses (libtoybox.so).
if(YETTY_ENABLE_LIB_QEMU_BINARY)
    if(NOT DEFINED ANDROID_BUILD_DIR)
        if(DEFINED ENV{ANDROID_BUILD_DIR})
            set(ANDROID_BUILD_DIR $ENV{ANDROID_BUILD_DIR})
        endif()
    endif()
    if(ANDROID_BUILD_DIR AND ANDROID_ABI)
        set(_QEMU_JNILIBS_DIR "${ANDROID_BUILD_DIR}/jniLibs/${ANDROID_ABI}")
        # Source: prebuilt qemu fetch dir (yetty_3rdparty_fetch(qemu)).
        set(_QEMU_SRC "${YETTY_3RDPARTY_qemu_DIR}/qemu-system-riscv64")
        set(_QEMU_DST "${_QEMU_JNILIBS_DIR}/libqemu-system-riscv64.so")
        if(EXISTS "${_QEMU_SRC}")
            file(MAKE_DIRECTORY "${_QEMU_JNILIBS_DIR}")
            file(COPY "${_QEMU_SRC}" DESTINATION "${_QEMU_JNILIBS_DIR}")
            file(RENAME "${_QEMU_JNILIBS_DIR}/qemu-system-riscv64" "${_QEMU_DST}")
            message(STATUS "QEMU: shipped ${_QEMU_DST} for executable lib dir")
        else()
            message(WARNING "QEMU: ${_QEMU_SRC} not present; libqemu-system-riscv64.so not staged")
        endif()
    endif()
endif()

# Verify all required assets are present
if(YETTY_ENABLE_FEATURE_ASSETS)
    add_custom_command(TARGET yetty POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${ANDROID_ASSETS_DIR}/.. -DTARGET_TYPE=android -P ${YETTY_ROOT}/build-tools/yetty/verify-assets.cmake
        COMMENT "Verifying build assets..."
    )
endif()

# =============================================================================
# libygreeter.so — the ygui feature-showcase app as a second NativeActivity.
#
# Mirrors the webasm ygreeter target (build-tools/yetty/platform/webasm/cmake.cmake):
# ygreeter's standalone mode rendered into its own surface, no terminal / shell /
# VM. The Android entry (yetty_android_program_init / _term) lives at the foot of
# tools/ygreeter/main.c and is driven by the SHARED NDK glue (android-glue.c,
# compiled in below) — the same android_main path libyetty.so uses. Both libs
# ship in the one APK; AndroidManifest's .Ygreeter activity-alias selects this
# lib via its android.app.lib_name meta-data.
# =============================================================================
add_library(ygreeter SHARED
    ${YETTY_ROOT}/tools/ygreeter/main.c
    ${YETTY_ROOT}/tools/ygreeter/embedded-assets.c
    ${YETTY_ROOT}/src/yetty/yshadertoy/demo-shaders.c
    # Shared NDK app glue — window/input/IME/looper + android_main, calls
    # ygreeter's yetty_android_program_init/_term (resolved here).
    ${YETTY_ROOT}/src/yetty/yplatform/ymain/android-glue.c
    # Core sources ygreeter compiles directly (not provided as libs) —
    # mirrors tools/ygreeter/CMakeLists.txt's YGREETER_SOURCES + the
    # Android platform backends (default.c, not glfw/webasm).
    ${YETTY_ROOT}/src/yetty/yframework/yframework.c
    ${YETTY_ROOT}/src/yetty/yconfig/config.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/ynotify/ynotify.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/android.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/pipe/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/coroutine/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/yworkpool/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu/default.c
    ${YETTY_ROOT}/src/yetty/ypty/memory-pty.c
)

target_include_directories(ygreeter BEFORE PRIVATE
    ${YETTY_ROOT}/src
    ${YETTY_ROOT}/include
    ${YETTY_INCLUDES}
    ${YETTY_RENDERER_INCLUDES}
    ${JPEG_INCLUDE_DIRS}
    ${BROTLI_INCLUDE_DIR}
    # incbin_add_directory emits ygreeter_data_manifest.h here;
    # embedded-assets.c includes it directly.
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_definitions(ygreeter PRIVATE
    ${YETTY_DEFINITIONS}
    YETTY_WEB=0
    YETTY_ANDROID=1
    YETTY_USE_PREBUILT_ATLAS=1
    YETTY_ASSETS_FROM_APK=1
    YETTY_YGREETER_HAS_STANDALONE
    YETTY_YGREETER_HAS_CHROME
)

set_target_properties(ygreeter PROPERTIES
    LIBRARY_OUTPUT_NAME "ygreeter"
    LINKER_LANGUAGE CXX)

if(NOT MSVC)
    target_compile_options(ygreeter PRIVATE -Wall -Wextra)
endif()

# Showcase lib set — mirrors the webasm ygreeter link line + the Android
# app glue libs (native_app_glue whole-archived so ANativeActivity_onCreate
# is retained; android + log for the NDK).
target_link_libraries(ygreeter PRIVATE
    yetty_ygui
    yetty_ywire
    yetty_ychrome
    yetty_ycircuit
    yetty_ymusic
    yetty_yfigure
    yetty_ygrid
    yetty_yshadertoy
    yetty_yfont_core
    yetty_ydraw_factory
    yetty_yimage
    yetty_yplot
    ${YETTY_LIBS}
    yetty_yco
    yetty_yplatform_core
    -Wl,--whole-archive
    native_app_glue
    -Wl,--no-whole-archive
    android
    log
)

# Embed ygreeter's runtime assets into libygreeter.so so it is self-sufficient
# (the terminal's incbin extractor is hardcoded to the yetty manifest, so
# ygreeter ships its own "data" manifest via embedded-assets.c). The MSDF
# font + shaders are taken from the set yetty_embed_assets(yetty) already
# staged into <build>/embed-data; the logos / intro video / sample pdf /
# README sit at the data-dir root. Extracted at first launch by
# ygreeter_embedded_assets_extract into <data_dir>/.
if(YETTY_ENABLE_LIB_INCBIN)
    yetty_compute_build_version()
    target_compile_definitions(ygreeter PRIVATE
        YETTY_BUILD_VERSION="${YETTY_BUILD_VERSION_STR}")

    set(_YGREETER_ANDROID_EMBED "${CMAKE_BINARY_DIR}/ygreeter-embed-data")
    file(REMOVE_RECURSE "${_YGREETER_ANDROID_EMBED}")
    file(MAKE_DIRECTORY "${_YGREETER_ANDROID_EMBED}")

    # shaders / fonts / msdf-fonts — reuse what yetty_embed_assets(yetty)
    # already collected for libyetty.so (same render stack, same fonts).
    foreach(_SUB shaders fonts msdf-fonts)
        if(EXISTS "${CMAKE_BINARY_DIR}/embed-data/${_SUB}")
            file(COPY "${CMAKE_BINARY_DIR}/embed-data/${_SUB}"
                 DESTINATION "${_YGREETER_ANDROID_EMBED}")
        endif()
    endforeach()

    # ygreeter's own showcase assets, extracted to the data-dir root.
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
            configure_file("${YETTY_ROOT}/${_F}"
                "${_YGREETER_ANDROID_EMBED}/${_NAME}" COPYONLY)
        endif()
    endforeach()

    include(${YETTY_ROOT}/build-tools/yetty/incbin.cmake)
    incbin_add_directory(ygreeter "data" "${_YGREETER_ANDROID_EMBED}" "*" TRUE)
endif()

# =============================================================================
# libyhello.so — the direct-yfigures greeter as a third NativeActivity.
#
# yhello is ygreeter's UI running standalone directly against yfigures: it opens
# its own surface and wires ygui to a local yfigure_container, with no terminal,
# no PTY, no OSC/DCS and no wire state machine. Its Android entry
# (yetty_android_program_init / _term) lives at the foot of tools/yhello/main.c
# and is driven by the SHARED NDK glue (android-glue.c, compiled in below) — the
# same android_main path libyetty.so / libygreeter.so use. The library ships in
# its own APK; AndroidManifest's ${appLibName} placeholder selects it per flavor.
#
# yhello is a strict subset of ygreeter (standalone only), so its source/link
# set drops ygreeter's client-mode pieces: no yetty_ywire, no memory-pty.c, no
# coroutine backend (none of yhello's compiled sources reference them).
# =============================================================================
add_library(yhello SHARED
    ${YETTY_ROOT}/tools/yhello/main.c
    ${YETTY_ROOT}/tools/yhello/embedded-assets.c
    ${YETTY_ROOT}/src/yetty/yshadertoy/demo-shaders.c
    # Shared NDK app glue — window/input/IME/looper + android_main, calls
    # yhello's yetty_android_program_init/_term (resolved here).
    ${YETTY_ROOT}/src/yetty/yplatform/ymain/android-glue.c
    # Core sources yhello compiles directly (not provided as libs) —
    # mirrors tools/yhello/CMakeLists.txt's YHELLO_SOURCES + the Android
    # platform backends (default.c / android.c, not glfw).
    ${YETTY_ROOT}/src/yetty/yframework/yframework.c
    ${YETTY_ROOT}/src/yetty/yconfig/config.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/ynotify/ynotify.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/android.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/pipe/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/yworkpool/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu/default.c
)

target_include_directories(yhello BEFORE PRIVATE
    ${YETTY_ROOT}/src
    ${YETTY_ROOT}/include
    ${YETTY_INCLUDES}
    ${YETTY_RENDERER_INCLUDES}
    ${JPEG_INCLUDE_DIRS}
    ${BROTLI_INCLUDE_DIR}
    # incbin_add_directory emits yhello_data_manifest.h here;
    # embedded-assets.c includes it directly.
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_definitions(yhello PRIVATE
    ${YETTY_DEFINITIONS}
    YETTY_WEB=0
    YETTY_ANDROID=1
    YETTY_USE_PREBUILT_ATLAS=1
    YETTY_ASSETS_FROM_APK=1
    YETTY_YHELLO_HAS_STANDALONE
    YETTY_YHELLO_HAS_CHROME
)

set_target_properties(yhello PROPERTIES
    LIBRARY_OUTPUT_NAME "yhello"
    LINKER_LANGUAGE CXX)

if(NOT MSVC)
    target_compile_options(yhello PRIVATE -Wall -Wextra)
endif()

# Showcase lib set — mirrors the desktop tools/yhello/CMakeLists.txt link line
# (minus the GLFW-only yetty_yplatform_move_resize / _window_chrome, whose
# window_chrome stubs resolve via yetty_yplatform_core on Android) + the
# Android app glue libs (native_app_glue whole-archived so
# ANativeActivity_onCreate is retained; android + log for the NDK).
target_link_libraries(yhello PRIVATE
    yetty_ygui
    yetty_ychrome
    yetty_ycircuit
    yetty_ymusic
    yetty_yfigure
    yetty_ygrid
    yetty_yshadertoy
    yetty_yfont_core
    yetty_ydraw_factory
    yetty_yimage
    yetty_yplot
    ${YETTY_LIBS}
    yetty_yco
    yetty_yplatform_core
    -Wl,--whole-archive
    native_app_glue
    -Wl,--no-whole-archive
    android
    log
)

# Embed yhello's runtime assets into libyhello.so so it is self-sufficient (the
# terminal's incbin extractor is hardcoded to the yetty manifest, so yhello
# ships its own "data" manifest via embedded-assets.c). The MSDF font + shaders
# are taken from the set yetty_embed_assets(yetty) already staged into
# <build>/embed-data; the logos / intro video / sample pdf / README sit at the
# data-dir root. Extracted at first launch by yhello_embedded_assets_extract
# into <data_dir>/.
if(YETTY_ENABLE_LIB_INCBIN)
    yetty_compute_build_version()
    target_compile_definitions(yhello PRIVATE
        YETTY_BUILD_VERSION="${YETTY_BUILD_VERSION_STR}")

    set(_YHELLO_ANDROID_EMBED "${CMAKE_BINARY_DIR}/yhello-embed-data")
    file(REMOVE_RECURSE "${_YHELLO_ANDROID_EMBED}")
    file(MAKE_DIRECTORY "${_YHELLO_ANDROID_EMBED}")

    # shaders / fonts / msdf-fonts — reuse what yetty_embed_assets(yetty)
    # already collected for libyetty.so (same render stack, same fonts).
    foreach(_SUB shaders fonts msdf-fonts)
        if(EXISTS "${CMAKE_BINARY_DIR}/embed-data/${_SUB}")
            file(COPY "${CMAKE_BINARY_DIR}/embed-data/${_SUB}"
                 DESTINATION "${_YHELLO_ANDROID_EMBED}")
        endif()
    endforeach()

    # yhello's own showcase assets, extracted to the data-dir root.
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
            configure_file("${YETTY_ROOT}/${_F}"
                "${_YHELLO_ANDROID_EMBED}/${_NAME}" COPYONLY)
        endif()
    endforeach()

    include(${YETTY_ROOT}/build-tools/yetty/incbin.cmake)
    incbin_add_directory(yhello "data" "${_YHELLO_ANDROID_EMBED}" "*" TRUE)
endif()

# Each gradle product flavor (yetty / ygreeter / yhello) drives its own CMake
# target (externalNativeBuild.cmake.targets), so the libraries build and package
# independently into separate APKs — no add_dependencies tying them together.
