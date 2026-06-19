# macOS desktop build target
include(${CMAKE_CURRENT_LIST_DIR}/variables.cmake)

include(${YETTY_ROOT}/build-tools/yetty/platform/shared.cmake)

# macOS-specific libraries (guarded by variables.cmake)
if(YETTY_ENABLE_LIB_GLFW)
    include(${YETTY_ROOT}/build-tools/yetty/libs/glfw.cmake)
endif()
if(YETTY_ENABLE_LIB_LIBMAGIC)
    include(${YETTY_ROOT}/build-tools/yetty/Libmagic.cmake)
endif()

# TinyEMU - in-process RISC-V emulator for --virtual flag
if(YETTY_ENABLE_LIB_TINYEMU)
    include(${YETTY_ROOT}/build-tools/yetty/tinyemu.cmake)
    include(${YETTY_ROOT}/build-tools/yetty/tinyemu-runtime.cmake)
endif()

# Desktop-specific subdirectories
if(YETTY_ENABLE_FEATURE_GPU)
    add_subdirectory(${YETTY_ROOT}/src/yetty/gpu ${CMAKE_BINARY_DIR}/src/yetty/gpu)
endif()
if(YETTY_ENABLE_FEATURE_CLIENT)
    add_subdirectory(${YETTY_ROOT}/src/yetty/client ${CMAKE_BINARY_DIR}/src/yetty/client)
endif()
if(YETTY_ENABLE_FEATURE_YTOP)
    add_subdirectory(${YETTY_ROOT}/src/yetty/ytop ${CMAKE_BINARY_DIR}/src/yetty/ytop)
endif()

# Platform sources — macOS-specific + shared GLFW/Unix (C)
set(YETTY_PLATFORM_SOURCES
    # Platform-object startup (shared GLFW desktop bootstrap): the glfw_platform
    # yclass run() brings up window (ywindow) + clipboard (yclipboard) directly.
    ${YETTY_ROOT}/src/yetty/yplatform/ymain/glfw.c
    ${YETTY_ROOT}/src/yetty/yplatform/yplatform/platform.c
    ${YETTY_ROOT}/src/yetty/yplatform/yplatform/glfw.c
    ${YETTY_ROOT}/src/yetty/yetty/app.c
    ${YETTY_ROOT}/src/yetty/yetty/rpc.gen.c
    ${YETTY_ROOT}/src/yetty/yplatform/ywindow/window.c
    ${YETTY_ROOT}/src/yetty/yplatform/ywindow/glfw.c
    ${YETTY_ROOT}/src/yetty/yplatform/yclipboard/glfw.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/os-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/coroutine/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/yworkpool/default.c
    ${YETTY_ROOT}/src/yetty/ypty/forkpty.c
    ${YETTY_ROOT}/src/yetty/ypty/memory-pty.c
    ${YETTY_ROOT}/src/yetty/yplatform/pty-factory/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/pipe/default.c
    ${YETTY_ROOT}/src/yetty/yncbin/incbin-assets.c
)

# TinyEMU PTY source (for --virtual flag)
if(YETTY_ENABLE_LIB_TINYEMU)
    list(APPEND YETTY_PLATFORM_SOURCES
        ${YETTY_ROOT}/src/yetty/ypty/temu-pty.c
    )
endif()

# Create executable with core sources + platform
add_executable(yetty
    ${YETTY_SOURCES}
    ${YETTY_CORE_SOURCES}
    ${YETTY_DESKTOP_SOURCES}
    ${YETTY_PLATFORM_SOURCES}
)

target_include_directories(yetty PRIVATE ${YETTY_INCLUDES} ${YETTY_RENDERER_INCLUDES} ${JPEG_INCLUDE_DIRS} ${BROTLI_INCLUDE_DIR})

# Embed all assets only for a standalone build. By default the desktop yetty
# is thin — the yinstall installer carries the assets (and yetty itself).
# The build version is stamped either way.
if(YETTY_EMBED_ASSETS_IN_BINARIES)
    yetty_embed_assets(yetty)
else()
    yetty_compute_build_version()
    target_compile_definitions(yetty PRIVATE YETTY_BUILD_VERSION="${YETTY_BUILD_VERSION_STR}")
endif()

# Dummy platforms for dependency tracking (legacy)
add_custom_target(copy-shaders-for-incbin)
add_custom_target(copy-fonts-for-incbin)

target_compile_definitions(yetty PRIVATE
    ${YETTY_DEFINITIONS}
    YETTY_WEB=0
    YETTY_ANDROID=0
    YETTY_USE_PREBUILT_ATLAS=0
    YETTY_USE_CORETEXT=1
    YETTY_USE_FORKPTY=1
    YETTY_HAS_VNC=1
    $<$<BOOL:${YETTY_ENABLE_LIB_TINYEMU}>:YETTY_HAS_TINYEMU=1>
    $<$<BOOL:${YETTY_ENABLE_LIB_TINYEMU}>:CONFIG_SLIRP>
    $<$<BOOL:${YETTY_ENABLE_LIB_QEMU}>:YETTY_HAS_QEMU=1>
    $<$<BOOL:${YETTY_ENABLE_FEATURE_SSH}>:YETTY_HAS_SSH=1>
)

set_target_properties(yetty PROPERTIES ENABLE_EXPORTS TRUE)

# Core Text for font discovery
find_library(CORETEXT_LIBRARY CoreText REQUIRED)
find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)

target_link_libraries(yetty PRIVATE
    ${YETTY_LIBS}
    ${CORETEXT_LIBRARY}
    ${COREFOUNDATION_LIBRARY}
    $<$<BOOL:${YETTY_ENABLE_LIB_TINYEMU}>:tinyemu>
    $<$<BOOL:${YETTY_ENABLE_LIB_QEMU}>:yetty_qemu>
    yetty_telnet
    yetty_yco
    yetty_yplatform_core
    yetty_yplatform_move_resize
    yetty_yplatform_window_chrome
)

# Copy runtime assets to build directory
if(YETTY_ENABLE_FEATURE_ASSETS)
    add_subdirectory(${YETTY_ROOT}/assets ${CMAKE_BINARY_DIR}/assets-build)
endif()

# Ensure all runtime assets are in build output before yetty
if(YETTY_ENABLE_FEATURE_ASSETS)
    add_dependencies(yetty copy-shaders copy-assets copy-shaders-for-incbin copy-fonts-for-incbin)
endif()

# Verify all required assets are present
if(YETTY_ENABLE_FEATURE_ASSETS)
    add_custom_command(TARGET yetty POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGET_TYPE=desktop -P ${YETTY_ROOT}/build-tools/yetty/verify-assets.cmake
        COMMENT "Verifying build assets..."
    )
endif()
