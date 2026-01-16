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
#     Prerequisites: Android NDK, Toybox (auto-downloaded)
#     make android    # downloads Toybox + builds yetty shared library
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

# Toybox - BSD-licensed shell utilities (used on all platforms)
# Binaries available at https://landley.net/toybox/bin/
set(TOYBOX_PATH "" CACHE PATH "Path to Toybox binary")

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

# Taywee/args for command line parsing (desktop only, not web)
if(NOT YETTY_ANDROID AND NOT EMSCRIPTEN)
    CPMAddPackage(
        NAME args
        GITHUB_REPOSITORY Taywee/args
        GIT_TAG 6.4.6
    )
endif()

# spdlog - add FIRST so ytrace detects it
CPMAddPackage(
    NAME spdlog
    GITHUB_REPOSITORY gabime/spdlog
    GIT_TAG v1.16.0
    OPTIONS "CMAKE_POSITION_INDEPENDENT_CODE ON"
)

# ytrace for logging with runtime control (will use existing spdlog)
CPMAddPackage(
    NAME ytrace
    GITHUB_REPOSITORY zokrezyl/ytrace
    GIT_TAG main
    OPTIONS
        "YTRACE_BUILD_TOOLS ON"
        "YTRACE_BUILD_EXAMPLES OFF"
        "YTRACE_ENABLE_YLOG ON"
        "YTRACE_ENABLE_YTRACE ON"
        "YTRACE_ENABLE_YDEBUG ON"
        "YTRACE_ENABLE_YINFO ON"
        "YTRACE_ENABLE_YWARN ON"
        "YTRACE_ENABLE_YFUNC ON"
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
# MuPDF for PDF rendering (desktop only)
#-----------------------------------------------------------------------------
if(NOT EMSCRIPTEN AND NOT YETTY_ANDROID)
    CPMAddPackage(
        NAME mupdf
        URL https://mupdf.com/downloads/archive/mupdf-1.25.4-source.tar.gz
        DOWNLOAD_ONLY YES
    )
    if(mupdf_ADDED)
        include(${CMAKE_CURRENT_SOURCE_DIR}/build-tools/cmake/mupdf/CMakeLists.txt)
    endif()
endif()

#-----------------------------------------------------------------------------
# ymery-cpp for ymery plugin (ImGui/ImPlot scientific visualization)
#-----------------------------------------------------------------------------
if(NOT EMSCRIPTEN AND NOT YETTY_ANDROID)
    # ymery-cpp plugins go to plugins/ymery subdirectory
    set(YMERY_PLUGIN_DIR "${CMAKE_BINARY_DIR}/plugins/ymery" CACHE PATH "")
    CPMAddPackage(
        NAME ymery-cpp
        GITHUB_REPOSITORY zokrezyl/ymery-cpp
        GIT_TAG main
        OPTIONS
            "YMERY_BUILD_TESTS OFF"
            "YMERY_BUILD_GUI_TESTS OFF"
            "YMERY_USE_WEBGPU ON"
    )
    # Fix: ymery-cpp's filesystem_common includes ytrace headers but doesn't link ytrace::ytrace
    # This adds the missing YTRACE_USE_SPDLOG define
    if(TARGET filesystem_common)
        target_compile_definitions(filesystem_common PRIVATE YTRACE_USE_SPDLOG=1)
    endif()
endif()

#-----------------------------------------------------------------------------
# FFmpeg for video plugin (Unix only - requires ./configure + make)
#-----------------------------------------------------------------------------
if(UNIX AND NOT EMSCRIPTEN AND NOT YETTY_ANDROID)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/build-tools/cmake/ffmpeg ffmpeg_build)
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

    # Toybox configuration (BSD-licensed shell utilities)
    if(TOYBOX_PATH)
        message(STATUS "Toybox path: ${TOYBOX_PATH}")
        add_compile_definitions(YETTY_TOYBOX_PATH="${TOYBOX_PATH}")
    else()
        message(STATUS "TOYBOX_PATH not set - will look for toybox at runtime")
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
    # Python for built-in plugin (Unix only - requires ./configure + make)
    # Must be defined before yetty so python_embedded target exists
    if(UNIX)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/build-tools/cmake/python python_build)
    endif()

    set(YETTY_CORE_SOURCES
        src/yetty/webgpu-context.cpp
        src/yetty/font.cpp
        src/yetty/font-manager.cpp
        src/yetty/rich-text.cpp
        src/yetty/emoji-atlas.cpp
        src/yetty/ydraw.cpp
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
        yaml-cpp
        msdfgen::msdfgen-core
        msdfgen::msdfgen-ext
        freetype
        ytrace::ytrace
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
        src/yetty/ydraw.cpp
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
        src/yetty/osc-command.cpp
        src/yetty/widget-factory.cpp
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
        src/yetty/local-terminal-backend.cpp
        src/yetty/remote-terminal-backend.cpp
        src/yetty/remote-terminal.cpp
        src/yetty/shared-grid.cpp
        src/yetty/widget-factory.cpp
        src/yetty/osc-command.cpp
        src/yetty/custom-glyph-plugin.cpp
        src/yetty/shader-manager.cpp
        src/yetty/shader-glyph-renderer.cpp
    )
    # Python plugin built into main executable (Unix only)
    if(UNIX)
        list(APPEND YETTY_SOURCES
            src/yetty/plugins/python/python.cpp
            src/yetty/plugins/python/yetty_wgpu.cpp
        )
    endif()
else()
    # Web uses main.cpp with Emscripten GLFW
    list(APPEND YETTY_SOURCES
        src/yetty/main.cpp
        src/yetty/yetty.cpp
        src/yetty/config.cpp
        src/yetty/input-handler.cpp
        src/yetty/web-terminal.cpp
        src/yetty/web-display.cpp
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

# Python includes for built-in Python plugin (Unix only)
if(UNIX AND NOT EMSCRIPTEN)
    target_include_directories(yetty PRIVATE ${CMAKE_BINARY_DIR}/python/install/include/python3.13)
    target_compile_definitions(yetty PRIVATE CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}")
endif()

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
        # Export web terminal functions for JavaScript
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8']"
        "-sEXPORTED_FUNCTIONS=['_main','_web_terminal_init','_web_terminal_write','_web_terminal_print','_web_terminal_get_cell_char','_web_terminal_get_cursor_row','_web_terminal_get_cursor_col','_web_terminal_get_rows','_web_terminal_get_cols','_web_terminal_key_input','_web_terminal_read_output','_web_terminal_cleanup']"
    )

    # Use -O1 to avoid wasm-opt which has binaryen version mismatch issues
    target_compile_options(yetty PRIVATE -O1)
    target_link_options(yetty PRIVATE -O1)

    # Output as .js (not .html) to avoid html-minifier-terser dependency
    # A standalone index.html will be provided to load yetty.js
    set_target_properties(yetty PROPERTIES SUFFIX ".js")

    target_link_libraries(yetty PRIVATE
        webgpu
        glm::glm
        stb
        yaml-cpp
        vterm
        ytrace::ytrace
    )

elseif(YETTY_ANDROID)
    #-------------------------------------------------------------------------
    # Android build - WebGPU via Vulkan, native_app_glue, Toybox shell
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
        ytrace::ytrace
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

    # Export symbols so plugins can find them (e.g., WidgetFactory::getShaderManager)
    set_target_properties(yetty PROPERTIES ENABLE_EXPORTS TRUE)

    # Link against yetty_core (provides font, rich-text, webgpu-context, glfw, glfw3webgpu)
    target_link_libraries(yetty PRIVATE
        yetty_core
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
        # Unix: Link against util for forkpty (Linux only, macOS has it in libc)
        if(NOT APPLE)
            target_link_libraries(yetty PRIVATE util)
        endif()
        target_compile_definitions(yetty PRIVATE YETTY_USE_FORKPTY=1)

        # Link Python into main executable so symbols are globally visible to numpy extensions
        # Use --whole-archive (Linux) or -force_load (macOS) to export all Python symbols
        if(TARGET python_embedded)
            add_dependencies(yetty python_embedded)
            if(APPLE)
                # On macOS, also need libintl and iconv for Python's locale module
                if(EXISTS "/usr/local/lib/libintl.a")
                    set(INTL_LIBRARY "/usr/local/lib/libintl.a")
                elseif(EXISTS "/opt/homebrew/lib/libintl.a")
                    set(INTL_LIBRARY "/opt/homebrew/lib/libintl.a")
                elseif(EXISTS "/opt/local/lib/libintl.a")
                    set(INTL_LIBRARY "/opt/local/lib/libintl.a")
                endif()

                target_link_libraries(yetty PRIVATE
                    -Wl,-force_load,$<TARGET_FILE:python_embedded>
                    ${INTL_LIBRARY}
                    iconv
                    m pthread
                )
            else()
                target_link_libraries(yetty PRIVATE
                    -Wl,--whole-archive
                    python_embedded
                    -Wl,--no-whole-archive
                    m dl pthread
                )
            endif()

            # Link the Python widget (statically linked, instantiated via plugin system)
            target_link_libraries(yetty PRIVATE python_widget)
        endif()
    endif()

    # Copy assets at build time (not just configure time)
    add_custom_command(TARGET yetty POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            ${CMAKE_CURRENT_BINARY_DIR}/assets
        COMMENT "Copying assets to build directory"
    )
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

    #-------------------------------------------------------------------------
    # Plugin tester (desktop only)
    #-------------------------------------------------------------------------
    add_executable(yetty-plugin-tester
        src/yetty/tester/main.cpp
        src/yetty/webgpu-context.cpp
    )
    target_include_directories(yetty-plugin-tester PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_definitions(yetty-plugin-tester PRIVATE
        CMAKE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        CMAKE_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    )
    target_link_libraries(yetty-plugin-tester PRIVATE
        yetty_core
        webgpu
        glm::glm
        ytrace::ytrace
        args
        ${CMAKE_DL_LIBS}
    )
    # Link Python into plugin tester so Python plugin symbols are available
    if(TARGET python_embedded)
        add_dependencies(yetty-plugin-tester python_embedded)
        if(APPLE)
            # On macOS, also need libintl and iconv for Python's locale module
            if(EXISTS "/usr/local/lib/libintl.a")
                set(INTL_LIBRARY "/usr/local/lib/libintl.a")
            elseif(EXISTS "/opt/homebrew/lib/libintl.a")
                set(INTL_LIBRARY "/opt/homebrew/lib/libintl.a")
            elseif(EXISTS "/opt/local/lib/libintl.a")
                set(INTL_LIBRARY "/opt/local/lib/libintl.a")
            endif()

            target_link_libraries(yetty-plugin-tester PRIVATE
                -Wl,-force_load,$<TARGET_FILE:python_embedded>
                ${INTL_LIBRARY}
                iconv
                m pthread
            )
        else()
            target_link_options(yetty-plugin-tester PRIVATE -rdynamic)
            target_link_libraries(yetty-plugin-tester PRIVATE
                -Wl,--whole-archive
                python_embedded
                -Wl,--no-whole-archive
                m dl pthread
            )
        endif()
    endif()

    #-------------------------------------------------------------------------
    # yetty-server - Terminal multiplexer server (desktop only)
    #-------------------------------------------------------------------------
    add_executable(yetty-server
        src/yetty/server/main.cpp
        src/yetty/local-terminal-backend.cpp
        src/yetty/shared-grid.cpp
        src/yetty/grid.cpp
    )
    target_include_directories(yetty-server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_definitions(yetty-server PRIVATE
        CMAKE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        YETTY_SERVER_BUILD=1
    )
    target_link_libraries(yetty-server PRIVATE
        vterm
        uv_a
        ytrace::ytrace
        ${CMAKE_DL_LIBS}
    )
    # Linux: Link against util for forkpty, rt for shm_open
    # macOS has these in libc, no separate libraries needed
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(yetty-server PRIVATE util rt)
    endif()

    # Tools
    add_subdirectory(tools)
endif()


# Python subdirectory moved earlier in file (before yetty target)

