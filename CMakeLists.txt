#=============================================================================
# yetty - WebGPU Terminal Emulator
#=============================================================================
cmake_minimum_required(VERSION 3.20)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version")
cmake_policy(SET CMP0074 NEW)

project(yetty VERSION 0.1.0 LANGUAGES C CXX)

# Set YETTY_ROOT to this directory (works for both direct and subdirectory builds)
set(YETTY_ROOT ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Yetty root directory")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

# Build options
option(YETTY_ANDROID "Build for Android" OFF)
option(YETTY_IOS "Build for iOS" OFF)
option(YETTY_TVOS "Build for tvOS" OFF)
set(WEBGPU_BACKEND "dawn" CACHE STRING "WebGPU backend (wgpu or dawn)")
set(TOYBOX_PATH "" CACHE PATH "Path to Toybox binary")

# Central build variables (libraries and features — defaults only, no platform logic)
include(${YETTY_ROOT}/build-tools/yetty/variables.cmake)

# Include CPM
include(${YETTY_ROOT}/build-tools/yetty/CPM.cmake)

# Detect platform and include appropriate target.
# Each platform cmake.cmake applies its own variable overrides then builds.
# tvOS check comes BEFORE iOS — tvOS sets YETTY_IOS=1 internally to share
# the iOS code paths in src/yetty/yplatform/ios/, but the build target
# selection itself must pick platform/tvos/cmake.cmake.
if(YETTY_ANDROID)
    include(${YETTY_ROOT}/build-tools/yetty/platform/android/cmake.cmake)
elseif(YETTY_TVOS OR CMAKE_SYSTEM_NAME STREQUAL "tvOS")
    include(${YETTY_ROOT}/build-tools/yetty/platform/tvos/cmake.cmake)
elseif(YETTY_IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    include(${YETTY_ROOT}/build-tools/yetty/platform/ios/cmake.cmake)
elseif(EMSCRIPTEN)
    include(${YETTY_ROOT}/build-tools/yetty/platform/webasm/cmake.cmake)
elseif(WIN32)
    include(${YETTY_ROOT}/build-tools/yetty/platform/windows/cmake.cmake)
elseif(APPLE)
    include(${YETTY_ROOT}/build-tools/yetty/platform/macos/cmake.cmake)
else()
    # Linux and other Unix
    include(${YETTY_ROOT}/build-tools/yetty/platform/linux/cmake.cmake)
endif()

# Tools
add_subdirectory(${YETTY_ROOT}/tools ${CMAKE_BINARY_DIR}/tools)

# Custom QA / static-analysis tools (Clang LibTooling-based)
if(YETTY_ENABLE_TOOL_QA)
    add_subdirectory(${YETTY_ROOT}/qa-tools/lib/naming-convention
                     ${CMAKE_BINARY_DIR}/qa-tools/lib/naming-convention)
    add_subdirectory(${YETTY_ROOT}/qa-tools/analysis/result-checker
                     ${CMAKE_BINARY_DIR}/qa-tools/analysis/result-checker)
    add_subdirectory(${YETTY_ROOT}/qa-tools/analysis/naming-convention
                     ${CMAKE_BINARY_DIR}/qa-tools/analysis/naming-convention)
    add_subdirectory(${YETTY_ROOT}/qa-tools/refactoring/naming-convention
                     ${CMAKE_BINARY_DIR}/qa-tools/refactoring/naming-convention)
endif()

# Demos
if(YETTY_ENABLE_FEATURE_DEMO)
    add_subdirectory(${YETTY_ROOT}/demo ${CMAKE_BINARY_DIR}/demo)
endif()
