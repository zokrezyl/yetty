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
project(yetty-poc VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build target options
option(YETTY_ANDROID "Build for Android" OFF)
option(YETTY_YMERY_ENABLED "Enable ymery ImGui plugin" ON)

# BusyBox path for Android shell
set(BUSYBOX_PATH "" CACHE PATH "Path to BusyBox install directory for Android")

# Include CPM.cmake
include(cmake/CPM.cmake)

#-----------------------------------------------------------------------------
# Dependencies
#-----------------------------------------------------------------------------

# WebGPU setup - use wgpu-native v27.0.4.0 for all platforms
if(YETTY_ANDROID)
    # Android: Use pre-built wgpu-native from build-android/wgpu-libs
    set(WGPU_LIB "${CMAKE_CURRENT_SOURCE_DIR}/build-android/wgpu-libs/arm64-v8a/libwgpu_native.so")
    set(WGPU_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/build-android/wgpu-include")
    set(WGPU_IS_SHARED TRUE)

    if(NOT EXISTS "${WGPU_LIB}")
        message(FATAL_ERROR
            "wgpu-native Android library not found at ${WGPU_LIB}\n"
            "Run 'make android-wgpu' or 'android/build-wgpu.sh' first.")
    endif()
elseif(NOT EMSCRIPTEN)
    # Desktop: Use pre-built wgpu-native from external/wgpu-native
    set(WGPU_LIB "${CMAKE_CURRENT_SOURCE_DIR}/external/wgpu-native/lib/libwgpu_native.so")
    set(WGPU_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/external/wgpu-native/include")
    set(WGPU_IS_SHARED TRUE)

    if(NOT EXISTS "${WGPU_LIB}")
        message(FATAL_ERROR
            "wgpu-native library not found at ${WGPU_LIB}\n"
            "Run 'scripts/build-wgpu-desktop.sh' first.")
    endif()
endif()

# Create webgpu target for native builds (Android and Desktop)
if(YETTY_ANDROID OR NOT EMSCRIPTEN)
    if(NOT EXISTS "${WGPU_INCLUDE}/webgpu/webgpu.h")
        message(FATAL_ERROR
            "wgpu-native headers not found at ${WGPU_INCLUDE}\n"
            "Run the appropriate build script first.")
    endif()

    # Create imported library target
    # v27+: all headers are in webgpu/ directory
    add_library(webgpu SHARED IMPORTED GLOBAL)
    set_target_properties(webgpu PROPERTIES
        IMPORTED_LOCATION "${WGPU_LIB}"
        IMPORTED_NO_SONAME TRUE
        INTERFACE_INCLUDE_DIRECTORIES "${WGPU_INCLUDE}"
    )
    target_compile_definitions(webgpu INTERFACE WEBGPU_BACKEND_WGPU)

    # Mark webgpu as already added so CPM/ymery won't try to fetch it again
    # CPM checks if package is in CPM_PACKAGES list
    set(CPM_PACKAGES "${CPM_PACKAGES};webgpu" CACHE INTERNAL "")
    set(webgpu_SOURCE_DIR "${WGPU_INCLUDE}" CACHE INTERNAL "")
    set(webgpu_BINARY_DIR "${CMAKE_BINARY_DIR}/webgpu" CACHE INTERNAL "")
    set(webgpu_VERSION "27.0.4.0" CACHE INTERNAL "")
    set(CPM_PACKAGE_webgpu_SOURCE_DIR "${WGPU_INCLUDE}" CACHE INTERNAL "")

    message(STATUS "Using wgpu-native v27.0.4.0:")
    message(STATUS "  Library: ${WGPU_LIB}")
    message(STATUS "  Headers: ${WGPU_INCLUDE}")
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

# spdlog for logging (desktop and Android, not web)
if(NOT EMSCRIPTEN)
    CPMAddPackage(
        NAME spdlog
        GITHUB_REPOSITORY gabime/spdlog
        GIT_TAG v1.12.0
    )
endif()

#-----------------------------------------------------------------------------
# Desktop-only dependencies (font atlas generation, GLFW windowing)
#-----------------------------------------------------------------------------
if(NOT EMSCRIPTEN AND NOT YETTY_ANDROID)
    # FreeType for font loading
    CPMAddPackage(
        NAME freetype
        GITHUB_REPOSITORY freetype/freetype
        GIT_TAG VER-2-13-2
        OPTIONS
            "FT_DISABLE_ZLIB ON"
            "FT_DISABLE_BZIP2 ON"
            "FT_DISABLE_PNG ON"
            "FT_DISABLE_HARFBUZZ ON"
            "FT_DISABLE_BROTLI ON"
    )
    # Create alias for msdfgen's find_package(Freetype)
    if(NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()

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

    # GLFW and glfw3webgpu adapter (desktop only, not Android)
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

    # glfw3webgpu needs to know which native platform GLFW is using
    # to properly expose the native window handles
    target_compile_definitions(glfw3webgpu PRIVATE _GLFW_X11)
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
# libvterm (native and Android - not web)
#-----------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
    set(LIBVTERM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/libvterm-0.3.3)
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
# Main target (executable for desktop/web, shared library for Android)
#-----------------------------------------------------------------------------
# Common sources for all platforms
set(YETTY_SOURCES
    src/yetty/renderer/webgpu-context.cpp
    src/yetty/renderer/text-renderer.cpp
    src/yetty/terminal/grid.cpp
    src/yetty/terminal/font.cpp
)

# Platform-specific entry point and sources
if(YETTY_ANDROID)
    # Android uses android_main.cpp instead of main.cpp
    list(APPEND YETTY_SOURCES
        src/android/android_main.cpp
        src/android/plugin-manager-stub.cpp
        src/yetty/terminal/terminal.cpp
    )
elseif(NOT EMSCRIPTEN)
    # Desktop uses main.cpp with GLFW
    list(APPEND YETTY_SOURCES
        src/main.cpp
        src/yetty/terminal/terminal.cpp
        src/yetty/plugin-manager.cpp
        src/yetty/custom-glyph-plugin.cpp
        src/yetty/plugins/shader-toy/shader-toy.cpp
        src/yetty/plugins/shader-glyph/shader-glyph.cpp
        src/yetty/plugins/image/image.cpp
    )
    if(YETTY_YMERY_ENABLED)
        list(APPEND YETTY_SOURCES
            src/yetty/plugins/ymery/ymery.cpp
        )
    endif()
else()
    # Web uses main.cpp with Emscripten GLFW
    list(APPEND YETTY_SOURCES
        src/main.cpp
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
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/src/yetty/renderer/shaders.wgsl@/shaders.wgsl"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
        "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    )

    set_target_properties(yetty PROPERTIES SUFFIX ".html")

    target_link_libraries(yetty PRIVATE
        webgpu
        glm::glm
        stb
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
        android
        log
    )

    # Copy assets to Android assets directory (where Gradle expects them)
    set(ANDROID_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build-android/assets")
    file(MAKE_DIRECTORY ${ANDROID_ASSETS_DIR})
    file(GLOB ASSET_FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
    file(COPY ${ASSET_FILES} DESTINATION ${ANDROID_ASSETS_DIR})
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/yetty/renderer/shaders.wgsl
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

    # Find fontconfig for font fallback
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG REQUIRED fontconfig)

    target_include_directories(yetty PRIVATE ${FONTCONFIG_INCLUDE_DIRS})

    target_link_libraries(yetty PRIVATE
        webgpu
        glfw
        glfw3webgpu
        glm::glm
        stb
        yaml-cpp
        msdfgen::msdfgen-core
        msdfgen::msdfgen-ext
        freetype
        vterm
        spdlog::spdlog
        util  # for forkpty
        ${FONTCONFIG_LIBRARIES}
    )

    #-------------------------------------------------------------------------
    # Optional: ymery plugin support
    #-------------------------------------------------------------------------
    if(YETTY_YMERY_ENABLED)
        # Configure ymery to use WebGPU backend (must be set before CPMAddPackage)
        set(YMERY_USE_WEBGPU ON CACHE BOOL "Use WebGPU backend for ymery" FORCE)

        # Fetch ymery-cpp from GitHub
        CPMAddPackage(
            NAME ymery
            GITHUB_REPOSITORY zokrezyl/ymery-cpp
            GIT_TAG main
            OPTIONS
                "YMERY_USE_WEBGPU ON"
                "YMERY_BUILD_DEMO OFF"
                "YMERY_BUILD_TESTS OFF"
        )

        target_compile_definitions(yetty PRIVATE YETTY_YMERY_ENABLED=1)
        target_include_directories(yetty PRIVATE ${ymery_SOURCE_DIR}/src)
        target_link_libraries(yetty PRIVATE ymery)

        message(STATUS "ymery plugin enabled (from GitHub)")
    endif()

    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/yetty/renderer/shaders.wgsl
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
