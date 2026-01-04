#=============================================================================
# yetty-poc - WebGPU Terminal Emulator
#=============================================================================
#
# BUILD TARGETS (use Makefile for convenience):
#
#   Desktop (Linux/macOS/Windows):
#     make release    # or: cmake -B build && cmake --build build
#
#   Web (Emscripten):
#     Prerequisites: Emscripten SDK, pre-built atlas (./yetty --generate-atlas)
#     make web        # or: emcmake cmake -B web-build && cmake --build web-build
#
#   Android:
#     Prerequisites: Android NDK, BusyBox cross-compiled
#     make android    # builds BusyBox + yetty shared library
#
#   Tests:
#     make test       # or: cmake --build build --target yetty_tests && ./build/test/ut/yetty_tests
#
# See Makefile for more targets and options.
#
#=============================================================================

cmake_minimum_required(VERSION 3.20)

# Allow dependencies with older cmake_minimum_required (yaml-cpp, etc.)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version for dependencies")

project(yetty-poc VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build target options
option(YETTY_ANDROID "Build for Android" OFF)

# BusyBox path for Android shell
set(BUSYBOX_PATH "" CACHE PATH "Path to BusyBox install directory for Android")

# Include CPM.cmake
include(build-tools/cmake/CPM.cmake)

#-----------------------------------------------------------------------------
# Dependencies
#-----------------------------------------------------------------------------

# WebGPU setup - use wgpu-native v27.0.4.0 for all platforms
if(YETTY_ANDROID)
    # Android: Use pre-built wgpu-native from build-android/wgpu-libs
    # Android cross-compilation requires manual setup due to NDK toolchain
    set(WGPU_LIB "${CMAKE_CURRENT_SOURCE_DIR}/build-android/wgpu-libs/arm64-v8a/libwgpu_native.so")
    set(WGPU_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/build-android/wgpu-include")

    if(NOT EXISTS "${WGPU_LIB}")
        message(FATAL_ERROR
            "wgpu-native Android library not found at ${WGPU_LIB}\n"
            "Run 'make android-wgpu' or 'android/build-wgpu.sh' first.")
    endif()

    if(NOT EXISTS "${WGPU_INCLUDE}/webgpu/webgpu.h")
        message(FATAL_ERROR
            "wgpu-native headers not found at ${WGPU_INCLUDE}\n"
            "Run 'make android-wgpu' first.")
    endif()

    # Create imported library target for Android
    add_library(webgpu SHARED IMPORTED GLOBAL)
    set_target_properties(webgpu PROPERTIES
        IMPORTED_LOCATION "${WGPU_LIB}"
        IMPORTED_NO_SONAME TRUE
        INTERFACE_INCLUDE_DIRECTORIES "${WGPU_INCLUDE}"
    )
    target_compile_definitions(webgpu INTERFACE WEBGPU_BACKEND_WGPU)

    # Mark webgpu as already added so CPM won't try to fetch it again
    set(CPM_PACKAGES "${CPM_PACKAGES};webgpu" CACHE INTERNAL "")
    set(webgpu_SOURCE_DIR "${WGPU_INCLUDE}" CACHE INTERNAL "")
    set(webgpu_VERSION "27.0.4.0" CACHE INTERNAL "")

    message(STATUS "Using wgpu-native v27.0.4.0 (Android):")
    message(STATUS "  Library: ${WGPU_LIB}")
    message(STATUS "  Headers: ${WGPU_INCLUDE}")

elseif(NOT EMSCRIPTEN)
    # Desktop (Linux/macOS/Windows): Auto-download wgpu-native from GitHub releases
    include(build-tools/cmake/WgpuNative.cmake)
endif()

# GLM for math
CPMAddPackage(
    NAME glm
    GITHUB_REPOSITORY g-truc/glm
    GIT_TAG 1.0.1
)

# stb for image loading
CPMAddPackage(
    NAME stb
    GITHUB_REPOSITORY nothings/stb
    GIT_TAG master
    DOWNLOAD_ONLY YES
)

if(stb_ADDED)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})
endif()

# yaml-cpp for config files
CPMAddPackage(
    NAME yaml-cpp
    GITHUB_REPOSITORY jbeder/yaml-cpp
    GIT_TAG 0.8.0
    OPTIONS "YAML_CPP_BUILD_TESTS OFF" "YAML_CPP_BUILD_TOOLS OFF"
)

# Taywee/args for command line parsing (desktop only)
if(NOT YETTY_ANDROID)
    CPMAddPackage(
        NAME args
        GITHUB_REPOSITORY Taywee/args
        GIT_TAG 6.4.6
    )
endif()

# spdlog for logging (all platforms)
CPMAddPackage(
    NAME spdlog
    GITHUB_REPOSITORY gabime/spdlog
    GIT_TAG v1.16.0
    OPTIONS
        "CMAKE_POSITION_INDEPENDENT_CODE ON"
)

# LZ4 for fast font cache compression
CPMAddPackage(
    NAME lz4
    GITHUB_REPOSITORY lz4/lz4
    GIT_TAG v1.10.0
    SOURCE_SUBDIR build/cmake
    OPTIONS
        "LZ4_BUILD_CLI OFF"
        "LZ4_BUILD_LEGACY_LZ4C OFF"
        "BUILD_SHARED_LIBS OFF"
        "BUILD_STATIC_LIBS ON"
)
if(TARGET lz4_static)
    set_target_properties(lz4_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

#-----------------------------------------------------------------------------
# Desktop-only dependencies (font atlas generation, GLFW windowing)
#-----------------------------------------------------------------------------
if(NOT EMSCRIPTEN AND NOT YETTY_ANDROID)
    # FreeType for font loading
    # NOTE: PNG support is required for color emoji fonts (CBDT/CBLC with PNG-compressed bitmaps)
    CPMAddPackage(
        NAME freetype
        GITHUB_REPOSITORY freetype/freetype
        GIT_TAG VER-2-13-2
        OPTIONS
            "FT_DISABLE_ZLIB OFF"
            "FT_DISABLE_BZIP2 ON"
            "FT_DISABLE_PNG OFF"
            "FT_DISABLE_HARFBUZZ ON"
            "FT_DISABLE_BROTLI ON"
    )
    # Create alias for msdfgen's find_package(Freetype)
    if(NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
    # Enable PIC for shared library plugins
    set_target_properties(freetype PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # msdfgen with FreeType support
    CPMAddPackage(
        NAME msdfgen
        GITHUB_REPOSITORY Chlumsky/msdfgen
        GIT_TAG v1.12
        OPTIONS
            "MSDFGEN_BUILD_STANDALONE OFF"
            "MSDFGEN_CORE_ONLY OFF"
            "MSDFGEN_USE_SKIA OFF"
            "MSDFGEN_USE_VCPKG OFF"
            "MSDFGEN_DISABLE_SVG ON"
            "MSDFGEN_DISABLE_PNG ON"
            "MSDFGEN_INSTALL OFF"
    )
    # Enable PIC for shared library plugins
    if(TARGET msdfgen-core)
        set_target_properties(msdfgen-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
    if(TARGET msdfgen-ext)
        set_target_properties(msdfgen-ext PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()

    # GLFW and glfw3webgpu adapter (desktop only, not Android)
    # Disable X11 on macOS to prevent glfw3webgpu from including X11 headers
    if(APPLE)
        set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
    endif()

    CPMAddPackage(
        NAME glfw
        GITHUB_REPOSITORY glfw/glfw
        GIT_TAG 3.4
        OPTIONS
            "GLFW_BUILD_DOCS OFF"
            "GLFW_BUILD_TESTS OFF"
            "GLFW_BUILD_EXAMPLES OFF"
            "GLFW_INSTALL OFF"
            "GLFW_BUILD_WAYLAND OFF"
    )

    CPMAddPackage(
        NAME glfw3webgpu
        GITHUB_REPOSITORY eliemichel/glfw3webgpu
        GIT_TAG main  # v27 API compatible
    )

    # glfw3webgpu needs _GLFW_X11 on Linux for X11 surface creation
    if(UNIX AND NOT APPLE)
        target_compile_definitions(glfw3webgpu PRIVATE _GLFW_X11)
    endif()

endif()

# libuv for async I/O (PTY reading, event loops) - needed for all platforms except Emscripten
if(NOT EMSCRIPTEN)
    CPMAddPackage(
        NAME libuv
        GITHUB_REPOSITORY libuv/libuv
        GIT_TAG v1.48.0
        OPTIONS
            "LIBUV_BUILD_TESTS OFF"
            "LIBUV_BUILD_BENCH OFF"
    )
endif()

#-----------------------------------------------------------------------------
# Android-specific dependencies
#-----------------------------------------------------------------------------
if(YETTY_ANDROID)
    # Add native_app_glue from Android NDK
    add_library(native_app_glue STATIC
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
    )
    target_include_directories(native_app_glue PUBLIC
        ${ANDROID_NDK}/sources/android/native_app_glue
    )

    # BusyBox configuration
    if(BUSYBOX_PATH)
        message(STATUS "BusyBox path: ${BUSYBOX_PATH}")
        add_compile_definitions(YETTY_BUSYBOX_PATH="${BUSYBOX_PATH}")
    else()
        message(WARNING "BUSYBOX_PATH not set - Android build will not have shell")
    endif()
endif()

#-----------------------------------------------------------------------------
# libvterm (all platforms - terminal emulation library)
# Note: Web builds won't use PTY, but need vterm for types/parsing
#-----------------------------------------------------------------------------
set(LIBVTERM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/libvterm-0.3.3)
if(EXISTS ${LIBVTERM_DIR})
    add_library(vterm STATIC
        ${LIBVTERM_DIR}/src/encoding.c
        ${LIBVTERM_DIR}/src/keyboard.c
        ${LIBVTERM_DIR}/src/mouse.c
        ${LIBVTERM_DIR}/src/parser.c
        ${LIBVTERM_DIR}/src/pen.c
        ${LIBVTERM_DIR}/src/screen.c
        ${LIBVTERM_DIR}/src/state.c
        ${LIBVTERM_DIR}/src/unicode.c
        ${LIBVTERM_DIR}/src/vterm.c
    )
    target_include_directories(vterm PUBLIC
        ${LIBVTERM_DIR}/include
        ${LIBVTERM_DIR}/src
    )
    target_compile_options(vterm PRIVATE -w)  # Suppress warnings in third-party code
endif()

#-----------------------------------------------------------------------------
# Core shared library (for plugins to link against on desktop)
#-----------------------------------------------------------------------------
if(NOT EMSCRIPTEN AND NOT YETTY_ANDROID)
    set(YETTY_CORE_SOURCES
        src/yetty/webgpu-context.cpp
        src/yetty/font.cpp
        src/yetty/font-manager.cpp
        src/yetty/rich-text.cpp
        src/yetty/emoji-atlas.cpp
        src/yetty/renderable.cpp
    )

    add_library(yetty_core SHARED ${YETTY_CORE_SOURCES})

    target_include_directories(yetty_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_definitions(yetty_core PRIVATE
        YETTY_WEB=0
        YETTY_ANDROID=0
    )

    # Platform-specific font discovery
    if(WIN32)
        # Windows: Use DirectWrite/GDI for font discovery (no fontconfig)
        target_compile_definitions(yetty_core PRIVATE YETTY_USE_DIRECTWRITE=1)
        target_link_libraries(yetty_core PUBLIC dwrite)
    else()
        # Unix: Use fontconfig for font fallback
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
        target_include_directories(yetty_core PRIVATE ${FONTCONFIG_INCLUDE_DIRS})
        target_compile_definitions(yetty_core PRIVATE YETTY_USE_FONTCONFIG=1)
    endif()

    target_link_libraries(yetty_core PUBLIC
        webgpu
        glfw
        glfw3webgpu
        glm::glm
        stb
        msdfgen::msdfgen-core
        msdfgen::msdfgen-ext
        freetype
        spdlog::spdlog
        lz4_static
        uv_a
        $<$<NOT:$<BOOL:${WIN32}>>:${FONTCONFIG_LIBRARIES}>
    )

    set_target_properties(yetty_core PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        POSITION_INDEPENDENT_CODE ON
    )

    # Build plugins as shared libraries (desktop only)
    add_subdirectory(src/yetty/plugins)
endif()

#-----------------------------------------------------------------------------
# Main target (executable for desktop/web, shared library for Android)
#-----------------------------------------------------------------------------
# Common sources for all platforms
set(YETTY_SOURCES
    src/yetty/grid-renderer.cpp
    src/yetty/grid.cpp
)

# Add core sources for non-desktop platforms (desktop uses yetty_core library)
if(EMSCRIPTEN OR YETTY_ANDROID)
    list(APPEND YETTY_SOURCES
        src/yetty/webgpu-context.cpp
        src/yetty/font.cpp
        src/yetty/font-manager.cpp
        src/yetty/rich-text.cpp
        src/yetty/emoji-atlas.cpp
    )
endif()

# Platform-specific entry point and sources
if(YETTY_ANDROID)
    # Android uses unified main.cpp with android_main() entry point
    list(APPEND YETTY_SOURCES
        src/yetty/main.cpp
        src/yetty/yetty.cpp
        src/yetty/config.cpp
        src/yetty/terminal.cpp
        src/android/plugin-manager-stub.cpp
    )
elseif(NOT EMSCRIPTEN)
    # Desktop uses main.cpp with GLFW
    # Desktop with plugin support
    list(APPEND YETTY_SOURCES
        src/yetty/main.cpp
        src/yetty/yetty.cpp
        src/yetty/input-handler.cpp
        src/yetty/config.cpp
        src/yetty/terminal.cpp
        src/yetty/plugin-manager.cpp
        src/yetty/custom-glyph-plugin.cpp
        src/yetty/shader-manager.cpp
        src/yetty/shader-glyph-renderer.cpp
    )
else()
    # Web uses main.cpp with Emscripten GLFW
    list(APPEND YETTY_SOURCES
        src/yetty/main.cpp
        src/yetty/yetty.cpp
        src/yetty/input-handler.cpp
    )
endif()

# Android builds as shared library, others as executable
if(YETTY_ANDROID)
    add_library(yetty SHARED ${YETTY_SOURCES})
else()
    add_executable(yetty ${YETTY_SOURCES})
endif()

target_include_directories(yetty PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(yetty PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

if(EMSCRIPTEN)
    #-------------------------------------------------------------------------
    # Emscripten (Web) build - uses pre-built atlas, no msdfgen
    #-------------------------------------------------------------------------
    target_compile_definitions(yetty PRIVATE
        YETTY_WEB=1
        YETTY_ANDROID=0
        YETTY_USE_PREBUILT_ATLAS=1
    )

    target_link_options(yetty PRIVATE
        -sUSE_GLFW=3
        -sUSE_WEBGPU
        -sASYNCIFY
        -sALLOW_MEMORY_GROWTH
        -sWASM_BIGINT
        --no-heap-copy
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/src/yetty/shaders/shaders.wgsl@/shaders.wgsl"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
    )

    # Use -O2 to avoid aggressive wasm-opt that causes binaryen version issues
    target_compile_options(yetty PRIVATE -O2)

    # Output as .js (not .html) to avoid html-minifier-terser dependency
    # A standalone index.html will be provided to load yetty.js
    set_target_properties(yetty PROPERTIES SUFFIX ".js")

    target_link_libraries(yetty PRIVATE
        webgpu
        glm::glm
        stb
        yaml-cpp
        vterm
        spdlog::spdlog
    )

elseif(YETTY_ANDROID)
    #-------------------------------------------------------------------------
    # Android build - WebGPU via Vulkan, native_app_glue, BusyBox shell
    #-------------------------------------------------------------------------
    target_compile_definitions(yetty PRIVATE
        YETTY_WEB=0
        YETTY_ANDROID=1
        YETTY_USE_PREBUILT_ATLAS=1
    )

    # Create shared library for Android (not executable)
    set_target_properties(yetty PROPERTIES
        LIBRARY_OUTPUT_NAME "yetty"
    )

    target_link_libraries(yetty PRIVATE
        webgpu
        -Wl,--whole-archive
        native_app_glue
        -Wl,--no-whole-archive
        glm::glm
        stb
        yaml-cpp
        vterm
        spdlog::spdlog
        lz4_static
        uv_a
        android
        log
    )

    # Copy assets to Android assets directory (where Gradle expects them)
    set(ANDROID_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build-android/assets")
    file(MAKE_DIRECTORY ${ANDROID_ASSETS_DIR})
    file(GLOB ASSET_FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
    file(COPY ${ASSET_FILES} DESTINATION ${ANDROID_ASSETS_DIR})
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/yetty/shaders/shaders.wgsl
        ${ANDROID_ASSETS_DIR}/shaders.wgsl
        COPYONLY
    )

    message(STATUS "Building for Android with WebGPU (Vulkan backend)")

else()
    #-------------------------------------------------------------------------
    # Desktop (Native) build - can generate atlas at runtime
    #-------------------------------------------------------------------------
    target_compile_definitions(yetty PRIVATE
        YETTY_WEB=0
        YETTY_ANDROID=0
        YETTY_USE_PREBUILT_ATLAS=0
    )

    # Link against yetty_core (provides font, rich-text, webgpu-context)
    target_link_libraries(yetty PRIVATE
        yetty_core
        glfw
        glfw3webgpu
        stb
        yaml-cpp
        args
        vterm
    )

    # Platform-specific PTY library
    if(WIN32)
        # Windows: Link against ConPTY (part of kernel32)
        target_compile_definitions(yetty PRIVATE YETTY_USE_CONPTY=1)
    else()
        # Unix: Link against util for forkpty
        target_link_libraries(yetty PRIVATE util)
        target_compile_definitions(yetty PRIVATE YETTY_USE_FORKPTY=1)
    endif()

    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/yetty/shaders/shaders.wgsl
        ${CMAKE_CURRENT_BINARY_DIR}/shaders.wgsl
        COPYONLY
    )

    #-------------------------------------------------------------------------
    # Unit tests (desktop only)
    #-------------------------------------------------------------------------
    option(YETTY_BUILD_TESTS "Build unit tests" ON)
    if(YETTY_BUILD_TESTS)
        enable_testing()
        add_subdirectory(test/ut)
    endif()
endif()
