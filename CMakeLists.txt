#=============================================================================
# yetty-poc - WebGPU Terminal Emulator
#=============================================================================
#
# BUILD TARGETS:
#
#   Native (Linux/macOS/Windows):
#     mkdir build && cd build
#     cmake ..
#     make -j$(nproc)
#
#   Emscripten (WebAssembly):
#     Prerequisites:
#       1. Emscripten SDK 3.x (system: /usr/bin/emcc)
#       2. Build native first, then generate atlas:
#          cd build && ./yetty --generate-atlas
#
#     Build:
#       mkdir web-build && cd web-build
#       emcmake cmake ..
#       emmake make -j$(nproc)
#
#     Output: yetty.html, yetty.js, yetty.wasm, yetty.data
#     Serve:  cd web-build && python -m http.server 8080
#             Open: http://localhost:8080/yetty.html
#
#=============================================================================

cmake_minimum_required(VERSION 3.20)
project(yetty-poc VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optional: Enable ymery plugin (requires ymery-cpp library)
option(YETTY_YMERY_ENABLED "Enable ymery ImGui plugin" ON)

# Include CPM.cmake
include(cmake/CPM.cmake)

#-----------------------------------------------------------------------------
# Dependencies
#-----------------------------------------------------------------------------

# WebGPU - automatically handles Emscripten via eliemichel's distribution
CPMAddPackage(
    NAME webgpu
    GITHUB_REPOSITORY eliemichel/WebGPU-distribution
    GIT_TAG wgpu-v0.19.4.1
    OPTIONS
        "WEBGPU_BACKEND WGPU"
)

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

#-----------------------------------------------------------------------------
# Native-only dependencies (font atlas generation)
#-----------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
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

    # GLFW and glfw3webgpu adapter
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
        GIT_TAG v1.2.0
    )
endif()

#-----------------------------------------------------------------------------
# libvterm (native only)
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
# Main executable
#-----------------------------------------------------------------------------
add_executable(yetty
    src/main.cpp
    src/yetty/renderer/webgpu-context.cpp
    src/yetty/renderer/text-renderer.cpp
    src/yetty/terminal/grid.cpp
    src/yetty/terminal/font.cpp
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/yetty/terminal/terminal.cpp>
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/yetty/plugin-manager.cpp>
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/yetty/custom-glyph-plugin.cpp>
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/yetty/plugins/shader-toy.cpp>
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/yetty/plugins/shader-glyph/shader-glyph.cpp>
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/yetty/plugins/image.cpp>
    $<$<AND:$<NOT:$<BOOL:${EMSCRIPTEN}>>,$<BOOL:${YETTY_YMERY_ENABLED}>>:src/yetty/plugins/ymery.cpp>
)

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
else()
    #-------------------------------------------------------------------------
    # Native build - can generate atlas at runtime
    #-------------------------------------------------------------------------
    target_compile_definitions(yetty PRIVATE
        YETTY_WEB=0
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
        target_link_libraries(yetty PRIVATE ymery spdlog::spdlog)

        message(STATUS "ymery plugin enabled (from GitHub)")
    endif()

    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/yetty/renderer/shaders.wgsl
        ${CMAKE_CURRENT_BINARY_DIR}/shaders.wgsl
        COPYONLY
    )
endif()
