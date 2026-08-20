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

# Log-level gating: compile-time strip of the ytrace()/ydebug() macros.
# The yinfo build passes -DYTRACE_ENABLE_YTRACE=0 -DYTRACE_ENABLE_YDEBUG=0 to
# drop the two verbose levels while keeping yinfo/ywarn/yerror compiled in.
# These translate to the header's own switches (YTRACE_C_ENABLE_TRACE /
# YTRACE_C_ENABLE_DEBUG in include/yetty/ytrace/ytrace.h). Define them for
# every target (add_compile_definitions applies to this directory and every
# subdirectory / include()d platform target added below).
option(YTRACE_ENABLE_YTRACE "Compile in the ytrace() trace-level log macro" ON)
option(YTRACE_ENABLE_YDEBUG "Compile in the ydebug() debug-level log macro" ON)
if(NOT YTRACE_ENABLE_YTRACE)
    add_compile_definitions(YTRACE_C_ENABLE_TRACE=0)
endif()
if(NOT YTRACE_ENABLE_YDEBUG)
    add_compile_definitions(YTRACE_C_ENABLE_DEBUG=0)
endif()

# Include CPM — clear stale CPM_DIRECTORY if CPM.cmake moved to a new path
# (CPM silently returns early when CPM_DIRECTORY != CMAKE_CURRENT_LIST_DIR)
get_filename_component(_CPM_EXPECTED_DIR "${YETTY_ROOT}/build-tools/yetty" REALPATH)
if(DEFINED CPM_DIRECTORY AND NOT CPM_DIRECTORY STREQUAL _CPM_EXPECTED_DIR)
    unset(CPM_DIRECTORY CACHE)
endif()
unset(_CPM_EXPECTED_DIR)
include(${YETTY_ROOT}/build-tools/yetty/CPM.cmake)

# yclass codegen: defines yetty_yclass_module() (every yclass module's own
# CMakeLists registers itself with it) and schedules the deferred finalizer
# that turns the registrations into the on-demand `yclass-codegen` ninja
# target (`make codegen` builds it; needs this build's compile_commands.json,
# so only exporting builds get the target — the desktop builds do). Included
# BEFORE the platform tree so the registration function exists when the
# module CMakeLists run.
include(${YETTY_ROOT}/build-tools/yetty/yclass-codegen.cmake)

# ymux (#699) is a POSIX-only subsystem — the daemon uses unix-domain sockets,
# poll(2) and forkpty(3), none of which exist on the Windows or WebASM targets
# (and it is not run on the mobile/macOS targets). Gate its module, tool, and
# tests to desktop Linux so the non-POSIX builds compile. Must be set BEFORE the
# platform include below: that include pulls in src/yetty (via shared.cmake's
# add_subdirectory(src/yetty)), where the gate is read. The test dirs already
# guard on `TARGET yetty_ymux`, so they skip automatically when it is absent.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN AND NOT ANDROID AND NOT YETTY_ANDROID)
    set(YETTY_YMUX_SUPPORTED TRUE)
else()
    set(YETTY_YMUX_SUPPORTED FALSE)
endif()

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
    add_subdirectory(${YETTY_ROOT}/qa-tools/analysis/out-param
                     ${CMAKE_BINARY_DIR}/qa-tools/analysis/out-param)
    add_subdirectory(${YETTY_ROOT}/qa-tools/analysis/naming-convention
                     ${CMAKE_BINARY_DIR}/qa-tools/analysis/naming-convention)
    add_subdirectory(${YETTY_ROOT}/qa-tools/refactoring/naming-convention
                     ${CMAKE_BINARY_DIR}/qa-tools/refactoring/naming-convention)
endif()

# Demos
if(YETTY_ENABLE_FEATURE_DEMO)
    add_subdirectory(${YETTY_ROOT}/demo ${CMAKE_BINARY_DIR}/demo)
endif()

# yinstall — the installer. Added last, after the yetty binary, every tool
# and every demo, so each executable it embeds already exists as a target
# (the bin/ component is built from `if(TARGET ...)` over those names).
if(YETTY_ENABLE_TOOL_YINSTALL)
    add_subdirectory(${YETTY_ROOT}/tools/yinstall ${CMAKE_BINARY_DIR}/tools/yinstall)
endif()
