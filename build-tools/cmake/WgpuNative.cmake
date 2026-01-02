#=============================================================================
# WgpuNative.cmake - Automatic download and setup of wgpu-native
#=============================================================================
#
# This module downloads pre-built wgpu-native binaries from GitHub releases
# and sets up the webgpu target for linking.
#
# Supported platforms:
#   - Linux x86_64/aarch64
#   - macOS x86_64/aarch64
#   - Windows x86_64
#
# For Android, use the separate android/build-wgpu.sh script.
#
#=============================================================================

include(FetchContent)

set(WGPU_VERSION "27.0.4.0" CACHE STRING "wgpu-native version to use")

# Determine platform and download URL
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        set(WGPU_PLATFORM "linux-x86_64")
        set(WGPU_LIB_NAME "libwgpu_native.so")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        set(WGPU_PLATFORM "linux-aarch64")
        set(WGPU_LIB_NAME "libwgpu_native.so")
    else()
        message(FATAL_ERROR "Unsupported Linux architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # macOS architecture-specific binaries (universal no longer available)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
        set(WGPU_PLATFORM "macos-aarch64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        set(WGPU_PLATFORM "macos-x86_64")
    else()
        message(FATAL_ERROR "Unsupported macOS architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    set(WGPU_LIB_NAME "libwgpu_native.dylib")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Determine compiler suffix (msvc or gnu)
    if(MSVC)
        set(WGPU_COMPILER_SUFFIX "-msvc")
    else()
        set(WGPU_COMPILER_SUFFIX "-gnu")
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        set(WGPU_PLATFORM "windows-x86_64${WGPU_COMPILER_SUFFIX}")
        set(WGPU_LIB_NAME "wgpu_native.dll")
        set(WGPU_IMPLIB_NAME "wgpu_native.dll.lib")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        set(WGPU_PLATFORM "windows-aarch64${WGPU_COMPILER_SUFFIX}")
        set(WGPU_LIB_NAME "wgpu_native.dll")
        set(WGPU_IMPLIB_NAME "wgpu_native.dll.lib")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i686|i386|x86")
        set(WGPU_PLATFORM "windows-i686${WGPU_COMPILER_SUFFIX}")
        set(WGPU_LIB_NAME "wgpu_native.dll")
        set(WGPU_IMPLIB_NAME "wgpu_native.dll.lib")
    else()
        message(FATAL_ERROR "Unsupported Windows architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

set(WGPU_URL "https://github.com/gfx-rs/wgpu-native/releases/download/v${WGPU_VERSION}/wgpu-${WGPU_PLATFORM}-release.zip")
set(WGPU_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_deps/wgpu-native")

# Download and extract if not already done
if(NOT EXISTS "${WGPU_DOWNLOAD_DIR}/lib/${WGPU_LIB_NAME}")
    message(STATUS "Downloading wgpu-native v${WGPU_VERSION} for ${WGPU_PLATFORM}...")
    message(STATUS "  URL: ${WGPU_URL}")

    # Download the zip file
    set(WGPU_ZIP "${CMAKE_BINARY_DIR}/wgpu-native.zip")
    file(DOWNLOAD "${WGPU_URL}" "${WGPU_ZIP}"
        STATUS DOWNLOAD_STATUS
        SHOW_PROGRESS
    )
    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    list(GET DOWNLOAD_STATUS 1 STATUS_STRING)

    if(NOT STATUS_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to download wgpu-native: ${STATUS_STRING}\n"
            "URL: ${WGPU_URL}\n"
            "You may need to download manually or check your internet connection.")
    endif()

    # Extract
    message(STATUS "Extracting wgpu-native...")
    file(ARCHIVE_EXTRACT INPUT "${WGPU_ZIP}" DESTINATION "${WGPU_DOWNLOAD_DIR}")

    # Clean up zip
    file(REMOVE "${WGPU_ZIP}")

    message(STATUS "wgpu-native extracted to ${WGPU_DOWNLOAD_DIR}")
endif()

# Set paths
set(WGPU_INCLUDE_DIR "${WGPU_DOWNLOAD_DIR}/include")
set(WGPU_LIB_DIR "${WGPU_DOWNLOAD_DIR}/lib")
set(WGPU_LIB_PATH "${WGPU_LIB_DIR}/${WGPU_LIB_NAME}")

# Verify files exist
if(NOT EXISTS "${WGPU_INCLUDE_DIR}/webgpu/webgpu.h")
    message(FATAL_ERROR "wgpu-native headers not found at ${WGPU_INCLUDE_DIR}")
endif()

if(NOT EXISTS "${WGPU_LIB_PATH}")
    message(FATAL_ERROR "wgpu-native library not found at ${WGPU_LIB_PATH}")
endif()

# Create imported library target
add_library(webgpu SHARED IMPORTED GLOBAL)
set_target_properties(webgpu PROPERTIES
    IMPORTED_LOCATION "${WGPU_LIB_PATH}"
    IMPORTED_NO_SONAME TRUE
    INTERFACE_INCLUDE_DIRECTORIES "${WGPU_INCLUDE_DIR}"
)

# Windows needs import library
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_target_properties(webgpu PROPERTIES
        IMPORTED_IMPLIB "${WGPU_LIB_DIR}/${WGPU_IMPLIB_NAME}"
    )
endif()

target_compile_definitions(webgpu INTERFACE WEBGPU_BACKEND_WGPU)

# Export variables for use elsewhere
set(WGPU_NATIVE_INCLUDE_DIR "${WGPU_INCLUDE_DIR}" CACHE INTERNAL "")
set(WGPU_NATIVE_LIB_DIR "${WGPU_LIB_DIR}" CACHE INTERNAL "")
set(WGPU_NATIVE_LIB_PATH "${WGPU_LIB_PATH}" CACHE INTERNAL "")

# Mark as available for CPM compatibility
set(CPM_PACKAGES "${CPM_PACKAGES};webgpu" CACHE INTERNAL "")
set(webgpu_SOURCE_DIR "${WGPU_INCLUDE_DIR}" CACHE INTERNAL "")
set(webgpu_VERSION "${WGPU_VERSION}" CACHE INTERNAL "")

message(STATUS "wgpu-native v${WGPU_VERSION} ready:")
message(STATUS "  Library: ${WGPU_LIB_PATH}")
message(STATUS "  Headers: ${WGPU_INCLUDE_DIR}")
