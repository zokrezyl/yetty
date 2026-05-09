# tvOS build target — mirrors platforms/ios.cmake. Reuses iOS Objective-C
# platform sources (UIKit subset that exists on tvOS); only the bundle
# metadata + deployment-target attributes differ. YETTY_IOS=1 is set so
# any `#if YETTY_IOS` branches in the platform code stay active; YETTY_TVOS=1
# is added for tvOS-only branches.

# Disable desktop-only / sandbox-incompatible libraries, same as iOS.
set(YETTY_ENABLE_LIB_GLFW OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_LIB_QEMU OFF CACHE BOOL "" FORCE)

include(${YETTY_ROOT}/build-tools/cmake/platforms/shared.cmake)

include(${YETTY_ROOT}/build-tools/cmake/tinyemu.cmake)
include(${YETTY_ROOT}/build-tools/cmake/tinyemu-runtime.cmake)

set(TVOS_ASSETS_DIR "${CMAKE_BINARY_DIR}/tvos-assets")
file(MAKE_DIRECTORY ${TVOS_ASSETS_DIR})

# Platform sources — same as iOS (the .m files use UIKit primitives that
# exist on tvOS too: UIWindow, UIView, UIViewController, MetalKit). If a
# tvOS-specific override is needed later, add it under yplatform/tvos/ and
# branch here on YETTY_TVOS.
set(YETTY_PLATFORM_SOURCES
    ${YETTY_ROOT}/src/yetty/ymain/ios-tvos.m
    ${YETTY_ROOT}/src/yetty/yplatform/ios/platform-paths.m
    ${YETTY_ROOT}/src/yetty/yplatform/ios/surface.m
    ${YETTY_ROOT}/src/yetty/yplatform/ios/tinyemu-pty.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/libuv-event-loop.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/ycoroutine.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/ywebgpu.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/unix-pipe.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/unix-socket.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/extract-assets.c
    ${YETTY_ROOT}/src/yetty/yncbin/incbin-assets.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/thread.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/term.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/fs.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/time.c
)

add_executable(yetty MACOSX_BUNDLE
    ${YETTY_SOURCES}
    ${YETTY_CORE_SOURCES}
    ${YETTY_PLATFORM_SOURCES}
)

target_include_directories(yetty PRIVATE ${YETTY_INCLUDES} ${YETTY_RENDERER_INCLUDES} ${JPEG_INCLUDE_DIRS} ${BROTLI_INCLUDE_DIR})

yetty_embed_assets(yetty)

add_custom_target(copy-shaders-for-incbin)
add_custom_target(copy-fonts-for-incbin)

target_compile_definitions(yetty PRIVATE
    ${YETTY_DEFINITIONS}
    YETTY_WEB=0
    YETTY_ANDROID=0
    YETTY_IOS=1
    YETTY_TVOS=1
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

set_target_properties(yetty PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.yetty.terminal"
    MACOSX_BUNDLE_BUNDLE_NAME "Yetty"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_INFO_PLIST "${YETTY_ROOT}/build-tools/tvos/Info.plist"
    XCODE_ATTRIBUTE_TVOS_DEPLOYMENT_TARGET "17.0"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "3"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
)

find_library(UIKIT_LIBRARY UIKit REQUIRED)
find_library(CORETEXT_LIBRARY CoreText REQUIRED)
find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)
find_library(COREGRAPHICS_LIBRARY CoreGraphics REQUIRED)
find_library(METAL_LIBRARY Metal REQUIRED)
find_library(QUARTZCORE_LIBRARY QuartzCore REQUIRED)
# GameController: needed for GCKeyboard — UIPress doesn't reach the
# render view via the focus engine on tvOS, so HW kb input goes through
# GCKeyboard instead. See setupGCKeyboard in src/yetty/yplatform/ios/main.m.
find_library(GAMECONTROLLER_LIBRARY GameController REQUIRED)

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
    ${GAMECONTROLLER_LIBRARY}
)

if(YETTY_ENABLE_FEATURE_DEMO)
    message(STATUS "Generating demo outputs for tvOS...")
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -DYETTY_ROOT=${YETTY_ROOT}
            -DOUTPUT_DIR=${TVOS_ASSETS_DIR}
            -P ${YETTY_ROOT}/build-tools/cmake/generate-demo-outputs.cmake
        WORKING_DIRECTORY ${YETTY_ROOT}
    )
endif()

if(YETTY_ENABLE_FEATURE_ASSETS)
    file(GLOB ASSET_FILES "${YETTY_ROOT}/assets/*")
    file(COPY ${ASSET_FILES} DESTINATION ${TVOS_ASSETS_DIR})
endif()

if(YETTY_ENABLE_FEATURE_ASSETS)
    add_dependencies(yetty copy-shaders copy-shaders-for-incbin copy-fonts-for-incbin)
endif()

if(YETTY_ENABLE_FEATURE_CDB_GEN AND DEFINED YETTY_3RDPARTY_cdb_DIR)
    add_custom_command(TARGET yetty POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${YETTY_3RDPARTY_cdb_DIR}" "${TVOS_ASSETS_DIR}/msdf-fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/assets/shaders" "${TVOS_ASSETS_DIR}/shaders"
        COMMENT "Copying CDB fonts (3rdparty fetch) + shaders to tvOS assets"
    )
endif()
