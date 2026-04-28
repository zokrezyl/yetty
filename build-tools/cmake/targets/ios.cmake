# iOS build target

# Disable desktop-only libraries.
# - GLFW: desktop window system, not used on iOS (UIKit owns the window).
# - QEMU: --qemu uses fork+exec to spawn qemu-system-riscv64 — neither
#   primitive is available in the iOS sandbox, and the qemu-ios-* asset
#   tarball isn't published. Disable up-front so assets_fetch_qemu() in
#   shared.cmake doesn't try to download a non-existent file. Use --temu
#   (in-process TinyEMU) instead.
set(YETTY_ENABLE_LIB_GLFW OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_LIB_QEMU OFF CACHE BOOL "" FORCE)

include(${YETTY_ROOT}/build-tools/cmake/targets/shared.cmake)

# TinyEMU - RISC-V emulator for iOS (provides PTY via Linux VM)
include(${YETTY_ROOT}/build-tools/cmake/tinyemu.cmake)
include(${YETTY_ROOT}/build-tools/cmake/tinyemu-runtime.cmake)

# Set iOS assets directory
set(IOS_ASSETS_DIR "${CMAKE_BINARY_DIR}/ios-assets")
file(MAKE_DIRECTORY ${IOS_ASSETS_DIR})

# Platform sources — iOS-specific (Objective-C) + shared Unix (C). iOS uses
# TinyEMU for PTY (RISC-V Linux VM) instead of forkpty, so unix-pty-factory.c
# / fork-pty.c / glfw-* are intentionally excluded. The shared files are the
# same set linux.cmake uses, minus the desktop-only pieces.
set(YETTY_PLATFORM_SOURCES
    ${YETTY_ROOT}/src/yetty/yplatform/ios/main.m
    ${YETTY_ROOT}/src/yetty/yplatform/ios/platform-paths.m
    ${YETTY_ROOT}/src/yetty/yplatform/ios/surface.m
    ${YETTY_ROOT}/src/yetty/yplatform/ios/tinyemu-pty.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/libuv-event-loop.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/ycoroutine.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/ywebgpu.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/unix-pipe.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/unix-socket.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/extract-assets.c
    ${YETTY_ROOT}/src/yetty/incbin-assets.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/thread.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/term.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/fs.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/time.c
)

# Create iOS app bundle
add_executable(yetty MACOSX_BUNDLE
    ${YETTY_SOURCES}
    ${YETTY_CORE_SOURCES}
    ${YETTY_PLATFORM_SOURCES}
)

target_include_directories(yetty PRIVATE ${YETTY_INCLUDES} ${YETTY_RENDERER_INCLUDES} ${JPEG_INCLUDE_DIRS} ${BROTLI_INCLUDE_DIR})

# Embed all assets (logo, shaders, fonts, CDB files)
yetty_embed_assets(yetty)

# Dummy targets for dependency tracking (legacy)
add_custom_target(copy-shaders-for-incbin)
add_custom_target(copy-fonts-for-incbin)

target_compile_definitions(yetty PRIVATE
    ${YETTY_DEFINITIONS}
    YETTY_WEB=0
    YETTY_ANDROID=0
    YETTY_IOS=1
    YETTY_USE_PREBUILT_ATLAS=1
    YETTY_ASSETS_FROM_BUNDLE=1
    YETTY_USE_CORETEXT=1
    YETTY_USE_FORKPTY=0
    YETTY_HAS_VNC=1
    YETTY_HAS_YVIDEO=1
    $<$<BOOL:${YETTY_ENABLE_LIB_TINYEMU}>:YETTY_HAS_TINYEMU=1>
    # CONFIG_SLIRP must be defined for src/yetty/yplatform/ios/tinyemu-pty.c
    # so its slirp_open() ifdef block compiles in. Without it, p->tab_eth[i].net
    # is never set and virtio_net_init derefs NULL → SIGSEGV at vm init.
    $<$<BOOL:${YETTY_ENABLE_LIB_TINYEMU}>:CONFIG_SLIRP>
)

# iOS app bundle properties
set_target_properties(yetty PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.yetty.terminal"
    MACOSX_BUNDLE_BUNDLE_NAME "Yetty"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_INFO_PLIST "${YETTY_ROOT}/build-tools/ios/Info.plist"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
)

# Apple frameworks for iOS
find_library(UIKIT_LIBRARY UIKit REQUIRED)
find_library(CORETEXT_LIBRARY CoreText REQUIRED)
find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)
find_library(COREGRAPHICS_LIBRARY CoreGraphics REQUIRED)
find_library(METAL_LIBRARY Metal REQUIRED)
find_library(QUARTZCORE_LIBRARY QuartzCore REQUIRED)

target_link_libraries(yetty PRIVATE
    ${YETTY_LIBS}
    tinyemu
    yetty_telnet
    yetty_yco
    ${CORETEXT_LIBRARY}
    ${COREFOUNDATION_LIBRARY}
    ${COREGRAPHICS_LIBRARY}
    ${UIKIT_LIBRARY}
    ${METAL_LIBRARY}
    ${QUARTZCORE_LIBRARY}
)

# TinyEMU runtime (kernel, opensbi, alpine-rootfs.img) is embedded via incbin
# in shared.cmake's yetty_embed_assets() under the "yemu" prefix; extracted
# to <data_dir>/yemu/ at startup by yetty_yplatform_extract_assets(). The
# VM cfg is auto-generated under <config_dir>/temu/ at runtime — same model
# as Linux desktop. Do NOT bundle the files as Bundle/Resources.

# Generate demo outputs (pre-run demo scripts to capture output for iOS)
if(YETTY_ENABLE_FEATURE_DEMO)
    message(STATUS "Generating demo outputs for iOS...")
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -DYETTY_ROOT=${YETTY_ROOT}
            -DOUTPUT_DIR=${IOS_ASSETS_DIR}
            -P ${YETTY_ROOT}/build-tools/cmake/generate-demo-outputs.cmake
        WORKING_DIRECTORY ${YETTY_ROOT}
    )
endif()

# Copy static assets to iOS build directory
if(YETTY_ENABLE_FEATURE_ASSETS)
    file(GLOB ASSET_FILES "${YETTY_ROOT}/assets/*")
    file(COPY ${ASSET_FILES} DESTINATION ${IOS_ASSETS_DIR})
endif()

# Ensure shaders and assets are built before yetty
if(YETTY_ENABLE_FEATURE_ASSETS)
    add_dependencies(yetty copy-shaders copy-shaders-for-incbin copy-fonts-for-incbin)
endif()

# Copy prebuilt CDB fonts and shaders to iOS assets dir after build.
# CDB source: 3rdparty fetch dir (yetty_3rdparty_fetch(cdb)).
# Shaders source: ${CMAKE_BINARY_DIR}/assets/shaders, populated by yetty's
# own `copy-shaders` target in src/yetty/yshaders/CMakeLists.txt.
if(YETTY_ENABLE_FEATURE_CDB_GEN AND DEFINED YETTY_3RDPARTY_cdb_DIR)
    add_custom_command(TARGET yetty POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${YETTY_3RDPARTY_cdb_DIR}" "${IOS_ASSETS_DIR}/msdf-fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/assets/shaders" "${IOS_ASSETS_DIR}/shaders"
        COMMENT "Copying CDB fonts (3rdparty fetch) + shaders to iOS assets"
    )
endif()

# iOS embeds all assets via incbin - no runtime asset verification needed
