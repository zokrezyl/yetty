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
    src/yetty/renderer/WebGPUContext.cpp
    src/yetty/renderer/TextRenderer.cpp
    src/yetty/terminal/Grid.cpp
    src/yetty/terminal/Font.cpp
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/yetty/terminal/Terminal.cpp>
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/yetty/PluginManager.cpp>
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/yetty/plugins/ShaderToy.cpp>
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/yetty/plugins/Image.cpp>
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

    target_link_libraries(yetty PRIVATE
        webgpu
        glfw
        glfw3webgpu
        glm::glm
        stb
        msdfgen::msdfgen-core
        msdfgen::msdfgen-ext
        freetype
        vterm
        util  # for forkpty
    )

    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/yetty/renderer/shaders.wgsl
        ${CMAKE_CURRENT_BINARY_DIR}/shaders.wgsl
        COPYONLY
    )
endif()
