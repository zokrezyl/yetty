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
    ${YETTY_ROOT}/src/yetty/ymain/android.c
    ${YETTY_ROOT}/src/yetty/yplatform/paths/android.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/android.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/coroutine/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/yworkpool/default.c
    ${YETTY_ROOT}/src/yetty/ypty/forkpty.c
    ${YETTY_ROOT}/src/yetty/ypty/memory-pty.c
    ${YETTY_ROOT}/src/yetty/yplatform/pty-factory/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/pipe/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/process/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/socket/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/extract-assets/default.c
    ${YETTY_ROOT}/src/yetty/yncbin/incbin-assets.c
    ${YETTY_ROOT}/src/yetty/ypty/temu-pty.c
    ${YETTY_YPLATFORM_THREAD_SOURCES}
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
