# Linux desktop build target

# Libraries consumed by feature subdirs must be included BEFORE shared.cmake —
# shared.cmake's add_subdirectory(src/yetty) processes those subdirs, and
# their `if(TARGET ...)` guards only see platforms declared before that point.
# (Tree-sitter is already wired via shared.cmake → TreeSitter.cmake.)
if(YETTY_ENABLE_LIB_LIBMAGIC)
    include(${YETTY_ROOT}/build-tools/cmake/Libmagic.cmake)
endif()
if(YETTY_ENABLE_LIB_LIBCURL)
    include(${YETTY_ROOT}/build-tools/cmake/libs/libcurl.cmake)
endif()

include(${YETTY_ROOT}/build-tools/cmake/platforms/shared.cmake)

# Linux-specific libraries needed only by the main yetty executable.
if(YETTY_ENABLE_LIB_GLFW)
    include(${YETTY_ROOT}/build-tools/cmake/libs/glfw.cmake)
endif()

# TinyEMU - in-process RISC-V emulator for --temu flag
if(YETTY_ENABLE_LIB_TINYEMU)
    include(${YETTY_ROOT}/build-tools/cmake/tinyemu.cmake)
    include(${YETTY_ROOT}/build-tools/cmake/tinyemu-runtime.cmake)
endif()

# The shared RISC-V runtime (OpenSBI firmware, Linux kernel, Alpine rootfs)
# and the QEMU binary are now fetched as prebuilt release assets from
# shared.cmake via 3rdparty-fetch.cmake — no local toolchain build here.

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

# Platform sources — linux-specific + shared GLFW/Unix (C)
set(YETTY_PLATFORM_SOURCES
    ${YETTY_ROOT}/src/yetty/ymain/glfw.c
    ${YETTY_ROOT}/src/yetty/yplatform/paths/linux.c
    ${YETTY_ROOT}/src/yetty/yplatform/os-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/clipboard/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/window/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/coroutine/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu/default.c
    ${YETTY_ROOT}/src/yetty/ypty/forkpty.c
    ${YETTY_ROOT}/src/yetty/yplatform/pty-factory/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/pipe/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/extract-assets/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/socket/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/process/default.c
    ${YETTY_ROOT}/src/yetty/yncbin/incbin-assets.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/fs/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/time/default.c
)

# TinyEMU PTY source (for --virtual flag)
if(YETTY_ENABLE_LIB_TINYEMU)
    list(APPEND YETTY_PLATFORM_SOURCES
        ${YETTY_ROOT}/src/yetty/ypty/temu-pty.c
    )
endif()

# The main `yetty` exec needs WebGPU/Dawn, GLFW, X11/Wayland, fontconfig
# etc. — all the GUI stack. Skip declaring the target entirely when WebGPU
# is disabled (e.g. linux-riscv64 cross-build, where we only build demos
# and non-GUI tools). That also avoids configure-time pkg_check_modules
# / find_library calls below for libs the cross sysroot doesn't have.
if(YETTY_ENABLE_LIB_WEBGPU)

# Create executable with core sources + platform
add_executable(yetty
    ${YETTY_SOURCES}
    ${YETTY_CORE_SOURCES}
    ${YETTY_DESKTOP_SOURCES}
    ${YETTY_PLATFORM_SOURCES}
)

target_include_directories(yetty PRIVATE ${YETTY_INCLUDES} ${YETTY_RENDERER_INCLUDES} ${JPEG_INCLUDE_DIRS} ${BROTLI_INCLUDE_DIR})

# Embed all assets (logo, shaders, fonts, CDB files, TinyEMU).
# Shared RISC-V runtime (OpenSBI, kernel, Alpine rootfs) and QEMU binary have
# already been fetched at configure time in shared.cmake.
yetty_embed_assets(yetty)

# Dummy platforms for dependency tracking (legacy)
add_custom_target(copy-shaders-for-incbin)
add_custom_target(copy-fonts-for-incbin)

target_compile_definitions(yetty PRIVATE
    ${YETTY_DEFINITIONS}
    YETTY_WEB=0
    YETTY_ANDROID=0
    YETTY_USE_PREBUILT_ATLAS=0
    YETTY_USE_FONTCONFIG=1
    YETTY_USE_FORKPTY=1
    YETTY_HAS_VNC=1
    $<$<BOOL:${YETTY_ENABLE_LIB_TINYEMU}>:YETTY_HAS_TINYEMU=1>
    $<$<BOOL:${YETTY_ENABLE_LIB_TINYEMU}>:CONFIG_SLIRP>
    $<$<BOOL:${YETTY_ENABLE_LIB_QEMU}>:YETTY_HAS_QEMU=1>
    $<$<BOOL:${YETTY_ENABLE_FEATURE_SSH}>:YETTY_HAS_SSH=1>
)

set_target_properties(yetty PROPERTIES ENABLE_EXPORTS TRUE)

# Fontconfig linking
find_package(PkgConfig REQUIRED)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
target_include_directories(yetty PRIVATE ${FONTCONFIG_INCLUDE_DIRS})

if(DEFINED ENV{NIX_CC})
    # Nix: use static libs from env vars set by flake.nix
    set(FONTCONFIG_STATIC_LIB "$ENV{FONTCONFIG_STATIC_LIB}")
    set(EXPAT_STATIC_LIB "$ENV{EXPAT_STATIC_LIB}")
    set(UUID_STATIC_LIB "$ENV{UUID_STATIC_LIB}")
    message(STATUS "Nix detected - using static fontconfig: ${FONTCONFIG_STATIC_LIB}")
    set(FONTCONFIG_LINK_LIBS ${FONTCONFIG_STATIC_LIB} ${EXPAT_STATIC_LIB} ${UUID_STATIC_LIB})
else()
    # System: use static libs (search arch-specific dirs first, then default paths)
    find_library(FONTCONFIG_STATIC_LIB libfontconfig.a
        PATHS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE} /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /usr/lib REQUIRED)
    find_library(EXPAT_STATIC_LIB libexpat.a
        PATHS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE} /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /usr/lib REQUIRED)
    find_library(UUID_STATIC_LIB libuuid.a
        PATHS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE} /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /usr/lib REQUIRED)
    message(STATUS "Using static fontconfig/expat/uuid")
    set(FONTCONFIG_LINK_LIBS ${FONTCONFIG_STATIC_LIB} ${EXPAT_STATIC_LIB} ${UUID_STATIC_LIB})
endif()

target_link_libraries(yetty PRIVATE
    ${YETTY_LIBS}
    ${BROTLIDEC_LIBRARIES}
    ${FONTCONFIG_LINK_LIBS}
    $<$<BOOL:${YETTY_ENABLE_LIB_TINYEMU}>:tinyemu>
    $<$<BOOL:${YETTY_ENABLE_LIB_QEMU}>:yetty_qemu>
    yetty_telnet
    yetty_yco
    rt
    util
)

# Copy runtime assets to build directory
if(YETTY_ENABLE_FEATURE_ASSETS)
    add_subdirectory(${YETTY_ROOT}/assets ${CMAKE_BINARY_DIR}/assets-build)
endif()

# Ensure all runtime assets are in build output before yetty
if(YETTY_ENABLE_FEATURE_ASSETS)
    add_dependencies(yetty copy-shaders copy-assets copy-shaders-for-incbin copy-fonts-for-incbin)
endif()

# Verify all required assets are present.
# CDB is NOT checked for desktop — the binary embeds it via incbin and
# there's no separate runtime asset dir to verify (other platforms stage
# into APK/bundle/wasm-fs locations and check those). If incbin can't
# find a cdb file at link time it fails the build directly, so the
# embed pipeline is its own implicit check.
if(YETTY_ENABLE_FEATURE_ASSETS)
    add_custom_command(TARGET yetty POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGET_TYPE=desktop -P ${YETTY_ROOT}/build-tools/cmake/verify-assets.cmake
        COMMENT "Verifying build assets..."
    )
endif()

endif()  # YETTY_ENABLE_LIB_WEBGPU

