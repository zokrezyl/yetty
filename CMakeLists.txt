cmake_minimum_required(VERSION 3.20)
project(yetty-poc VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include CPM.cmake
include(cmake/CPM.cmake)

# WebGPU via eliemichel's distribution (wraps wgpu-native)
CPMAddPackage(
    NAME webgpu
    GITHUB_REPOSITORY eliemichel/WebGPU-distribution
    GIT_TAG wgpu-v0.19.4.1
    OPTIONS
        "WEBGPU_BACKEND WGPU"
)

# GLFW for window management
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

# glfw3webgpu - adapter between GLFW and WebGPU
CPMAddPackage(
    NAME glfw3webgpu
    GITHUB_REPOSITORY eliemichel/glfw3webgpu
    GIT_TAG v1.2.0
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

# msdfgen for SDF generation
CPMAddPackage(
    NAME msdfgen
    GITHUB_REPOSITORY Chlumsky/msdfgen
    GIT_TAG v1.12
    OPTIONS
        "MSDFGEN_BUILD_STANDALONE OFF"
        "MSDFGEN_USE_SKIA OFF"
        "MSDFGEN_USE_VCPKG OFF"
        "MSDFGEN_DISABLE_SVG ON"
        "MSDFGEN_INSTALL OFF"
)

# msdf-atlas-gen for atlas generation
CPMAddPackage(
    NAME msdf-atlas-gen
    GITHUB_REPOSITORY Chlumsky/msdf-atlas-gen
    GIT_TAG v1.3
    OPTIONS
        "MSDF_ATLAS_BUILD_STANDALONE OFF"
        "MSDF_ATLAS_NO_ARTERY_FONT ON"
        "MSDF_ATLAS_MSDFGEN_EXTERNAL ON"
        "MSDF_ATLAS_USE_VCPKG OFF"
        "MSDF_ATLAS_INSTALL OFF"
)

# Main executable
add_executable(yetty
    src/main.cpp
    src/renderer/WebGPUContext.cpp
    src/renderer/TextRenderer.cpp
    src/terminal/Grid.cpp
    src/terminal/Font.cpp
)

target_include_directories(yetty PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Pass source directory path to code for loading shaders
target_compile_definitions(yetty PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(yetty PRIVATE
    webgpu
    glfw
    glfw3webgpu
    glm::glm
    stb
    msdfgen::msdfgen
    msdfgen::msdfgen-ext
    msdf-atlas-gen::msdf-atlas-gen
)

# Copy assets to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Copy shaders to build directory
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/shaders.wgsl
    ${CMAKE_CURRENT_BINARY_DIR}/shaders.wgsl
    COPYONLY
)
