#=============================================================================
# Dawn.cmake - Automatic download and setup of Dawn WebGPU
#=============================================================================
#
# This module downloads pre-built Dawn binaries from GitHub releases
# and sets up the webgpu target for linking.
#
# Supported platforms and sources:
#   - Linux x86_64  → dawn-exotic (Wayland surface support enabled)
#   - Linux aarch64 → dawn-exotic (Wayland surface support enabled)
#   - macOS x86_64/aarch64 → google/dawn (macos-15-intel, macos-latest)
#   - Windows x86_64       → dawn-exotic (OpenGL/GLES backends enabled)
#   - iOS arm64            → google/dawn (dawn-apple xcframework)
#   - tvOS                 → dawn-exotic (google/dawn has no tvOS slices)
#
# For Android, use the separate build-tools/android/build-dawn.sh script.
#
# Release URLs:
#   google/dawn:   https://github.com/google/dawn/releases
#   dawn-exotic:   https://github.com/zokrezyl/dawn-exotic/releases
#
# Linux x86_64 was moved off Google's ubuntu-latest prebuilt because that
# build ships without SurfaceSourceWaylandSurface — rendering on a Wayland
# session aborts with "Unsupported sType". dawn-exotic builds both Linux
# archs with the Wayland backend enabled.
#
# Note: Dawn provides STATIC libraries (.a/.lib), not shared libraries.
#
#=============================================================================

include(FetchContent)

# Dawn release version (date-based versioning). Single source of truth for all
# platforms lives in dawn-version.cmake:
#   - dawn-exotic ships linux (x86_64/aarch64) + Windows + tvOS slices → Linux
#     desktop, Windows, tvOS.
#   - google/dawn ships macOS + dawn-apple (iOS) at this version → macOS, iOS.
# Every platform is on this single version.
include(${CMAKE_CURRENT_LIST_DIR}/dawn-version.cmake)

# iOS uses XCFramework from upstream Google dawn-apple package.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(DAWN_URL "https://github.com/google/dawn/releases/download/v${DAWN_VERSION}/dawn-apple-${DAWN_COMMIT}.xcframework.tar.gz")
    set(DAWN_HEADERS_URL "https://github.com/google/dawn/releases/download/v${DAWN_VERSION}/dawn-headers-${DAWN_COMMIT}.tar.gz")

    if(CMAKE_OSX_SYSROOT MATCHES "simulator" OR CMAKE_OSX_SYSROOT MATCHES "Simulator")
        set(DAWN_IOS_PLATFORM "simulator")
        set(DAWN_LIB_SUBDIR "ios-arm64_x86_64-simulator")
    else()
        set(DAWN_IOS_PLATFORM "device")
        set(DAWN_LIB_SUBDIR "ios-arm64")
    endif()

    message(STATUS "Downloading Dawn v${DAWN_VERSION} XCFramework for iOS ${DAWN_IOS_PLATFORM}...")
    message(STATUS "  URL: ${DAWN_URL}")

    FetchContent_Declare(dawn_apple   URL "${DAWN_URL}"          DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_Declare(dawn_headers URL "${DAWN_HEADERS_URL}"  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(dawn_apple dawn_headers)

    set(DAWN_XCFRAMEWORK "${dawn_apple_SOURCE_DIR}")
    set(DAWN_LIB_PATH "${DAWN_XCFRAMEWORK}/${DAWN_LIB_SUBDIR}/libwebgpu_dawn.a")
    if(EXISTS "${dawn_headers_SOURCE_DIR}/include")
        set(DAWN_INCLUDE_DIR "${dawn_headers_SOURCE_DIR}/include")
    else()
        set(DAWN_INCLUDE_DIR "${dawn_headers_SOURCE_DIR}")
    endif()

    if(NOT EXISTS "${DAWN_LIB_PATH}")
        message(FATAL_ERROR "Dawn iOS ${DAWN_IOS_PLATFORM} library not found at ${DAWN_LIB_PATH}")
    endif()

    add_library(webgpu STATIC IMPORTED GLOBAL)
    find_library(METAL_LIBRARY Metal REQUIRED)
    find_library(QUARTZCORE_LIBRARY QuartzCore REQUIRED)
    find_library(IOSURFACE_LIBRARY IOSurface REQUIRED)
    find_library(FOUNDATION_LIBRARY Foundation REQUIRED)
    set_target_properties(webgpu PROPERTIES
        IMPORTED_LOCATION "${DAWN_LIB_PATH}"
        INTERFACE_INCLUDE_DIRECTORIES "${DAWN_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${METAL_LIBRARY};${QUARTZCORE_LIBRARY};${IOSURFACE_LIBRARY};${FOUNDATION_LIBRARY}"
    )
    target_compile_definitions(webgpu INTERFACE WEBGPU_BACKEND_DAWN)

    message(STATUS "Dawn v${DAWN_VERSION} ready (iOS ${DAWN_IOS_PLATFORM}):")
    message(STATUS "  Library: ${DAWN_LIB_PATH}")
    message(STATUS "  Headers: ${DAWN_INCLUDE_DIR}")

# tvOS uses dawn-exotic build (Google's dawn-apple has no tvOS slices).
# The tarball ships an XCFramework with proper TVOS / TVOSSIMULATOR slices
# plus its own bundled headers — no separate headers fetch.
elseif(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
    set(DAWN_TVOS_URL "https://github.com/zokrezyl/dawn-exotic/releases/download/v${DAWN_VERSION}/dawn-tvos-release-${DAWN_VERSION}.tar.gz")

    if(CMAKE_OSX_SYSROOT MATCHES "simulator" OR CMAKE_OSX_SYSROOT MATCHES "Simulator")
        set(DAWN_TVOS_PLATFORM "simulator")
        set(DAWN_LIB_SUBDIR "tvos-arm64_x86_64-simulator")
    else()
        set(DAWN_TVOS_PLATFORM "device")
        set(DAWN_LIB_SUBDIR "tvos-arm64")
    endif()

    message(STATUS "Downloading Dawn v${DAWN_VERSION} XCFramework for tvOS ${DAWN_TVOS_PLATFORM}...")
    message(STATUS "  URL: ${DAWN_TVOS_URL}")

    FetchContent_Declare(dawn_tvos URL "${DAWN_TVOS_URL}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(dawn_tvos)

    set(DAWN_XCFRAMEWORK "${dawn_tvos_SOURCE_DIR}/lib/webgpu_dawn.xcframework")
    set(DAWN_LIB_PATH    "${DAWN_XCFRAMEWORK}/${DAWN_LIB_SUBDIR}/libwebgpu_dawn.a")
    set(DAWN_INCLUDE_DIR "${dawn_tvos_SOURCE_DIR}/include")

    if(NOT EXISTS "${DAWN_LIB_PATH}")
        message(FATAL_ERROR "Dawn tvOS ${DAWN_TVOS_PLATFORM} library not found at ${DAWN_LIB_PATH}\n"
            "XCFramework contents: ${DAWN_XCFRAMEWORK}")
    endif()

    add_library(webgpu STATIC IMPORTED GLOBAL)
    find_library(METAL_LIBRARY Metal REQUIRED)
    find_library(QUARTZCORE_LIBRARY QuartzCore REQUIRED)
    find_library(IOSURFACE_LIBRARY IOSurface REQUIRED)
    find_library(FOUNDATION_LIBRARY Foundation REQUIRED)
    set_target_properties(webgpu PROPERTIES
        IMPORTED_LOCATION "${DAWN_LIB_PATH}"
        INTERFACE_INCLUDE_DIRECTORIES "${DAWN_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${METAL_LIBRARY};${QUARTZCORE_LIBRARY};${IOSURFACE_LIBRARY};${FOUNDATION_LIBRARY}"
    )
    target_compile_definitions(webgpu INTERFACE WEBGPU_BACKEND_DAWN)

    message(STATUS "Dawn v${DAWN_VERSION} ready (tvOS ${DAWN_TVOS_PLATFORM}):")
    message(STATUS "  Library: ${DAWN_LIB_PATH}")
    message(STATUS "  Headers: ${DAWN_INCLUDE_DIR}")

    message(STATUS "Dawn v${DAWN_VERSION} ready (iOS ${DAWN_IOS_PLATFORM}):")
    message(STATUS "  XCFramework: ${DAWN_XCFRAMEWORK}")
    message(STATUS "  Library: ${DAWN_LIB_PATH}")
    message(STATUS "  Headers: ${DAWN_INCLUDE_DIR}")

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux desktop (x86_64 and aarch64): prebuilt install bundle from
    # the in-house dawn-exotic build. Google's official Linux release
    # (used for macOS / Windows below) is built without Wayland surface
    # support — yetty's render-target-texture path needs
    # SurfaceSourceWaylandSurface to present directly to a Wayland
    # surface and skip the X11-tile readback+diff. dawn-exotic enables
    # the Wayland backend so we can opt into native Wayland via
    # YETTY_GLFW_PLATFORM=wayland.
    #
    # Set DAWN_LOCAL_DIR to point at a local Dawn build tree to override.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(_dawn_arch "aarch64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        set(_dawn_arch "x86_64")
    else()
        message(FATAL_ERROR "Unsupported Linux architecture for Dawn: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    set(DAWN_LOCAL_DIR "" CACHE PATH "Optional local Dawn build tree (Linux)")
    set(DAWN_LOCAL_BUILD_TYPE "Release"
        CACHE STRING "Build type of the local Dawn tree (Debug or Release)")

    if(DAWN_LOCAL_DIR)
        set(DAWN_LIB_PATH "${DAWN_LOCAL_DIR}/out/${DAWN_LOCAL_BUILD_TYPE}/src/dawn/native/libwebgpu_dawn.a")
        set(DAWN_PRIMARY_INCLUDE_DIR "${DAWN_LOCAL_DIR}/include")
        set(DAWN_INCLUDE_DIRS
            "${DAWN_LOCAL_DIR}/include"
            "${DAWN_LOCAL_DIR}/out/${DAWN_LOCAL_BUILD_TYPE}/gen/include"
        )
        set(_dawn_linux_source "local ${DAWN_LOCAL_BUILD_TYPE}")

        if(NOT EXISTS "${DAWN_LIB_PATH}")
            message(FATAL_ERROR "Local Dawn library not found: ${DAWN_LIB_PATH}")
        endif()
        if(NOT EXISTS "${DAWN_LOCAL_DIR}/include/webgpu/webgpu.h")
            message(FATAL_ERROR "Missing webgpu.h in ${DAWN_LOCAL_DIR}/include/webgpu/")
        endif()
    else()
        # Always Release for the prebuilt (Debug Dawn is huge).
        set(DAWN_LINUX_URL
            "https://github.com/zokrezyl/dawn-exotic/releases/download/v${DAWN_VERSION}/dawn-linux-${_dawn_arch}-release-${DAWN_VERSION}.tar.gz")

        message(STATUS "Downloading Dawn ${_dawn_arch} install bundle from dawn-exotic v${DAWN_VERSION}")
        message(STATUS "  URL: ${DAWN_LINUX_URL}")
        FetchContent_Declare(
            dawn_linux
            URL "${DAWN_LINUX_URL}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(dawn_linux)

        set(DAWN_LIB_PATH "${dawn_linux_SOURCE_DIR}/lib/libwebgpu_dawn.a")
        set(DAWN_PRIMARY_INCLUDE_DIR "${dawn_linux_SOURCE_DIR}/include")
        set(DAWN_INCLUDE_DIRS "${DAWN_PRIMARY_INCLUDE_DIR}")

        if(NOT EXISTS "${DAWN_LIB_PATH}")
            message(FATAL_ERROR "Dawn ${_dawn_arch} library not found in extracted tarball: ${DAWN_LIB_PATH}")
        endif()
        if(NOT EXISTS "${DAWN_PRIMARY_INCLUDE_DIR}/webgpu/webgpu.h")
            message(FATAL_ERROR "webgpu.h not found at ${DAWN_PRIMARY_INCLUDE_DIR}/webgpu/")
        endif()
        set(_dawn_linux_source "prebuilt v${DAWN_VERSION}")
    endif()

    include(${YETTY_ROOT}/build-tools/yetty/x11-static.cmake)
    add_library(webgpu STATIC IMPORTED GLOBAL)
    set_target_properties(webgpu PROPERTIES
        IMPORTED_LOCATION "${DAWN_LIB_PATH}"
        INTERFACE_INCLUDE_DIRECTORIES "${DAWN_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${YETTY_X11_STATIC_LIBS};${CMAKE_DL_LIBS};pthread"
    )
    target_compile_definitions(webgpu INTERFACE WEBGPU_BACKEND_DAWN)

    message(STATUS "Dawn (linux/${_dawn_arch}, ${_dawn_linux_source}) ready:")
    message(STATUS "  Library: ${DAWN_LIB_PATH}")
    message(STATUS "  Headers: ${DAWN_INCLUDE_DIRS}")

    set(DAWN_NATIVE_INCLUDE_DIR "${DAWN_PRIMARY_INCLUDE_DIR}" CACHE INTERNAL "")
    set(DAWN_NATIVE_LIB_PATH "${DAWN_LIB_PATH}" CACHE INTERNAL "")
    set(CPM_PACKAGES "${CPM_PACKAGES};webgpu" CACHE INTERNAL "")
    set(webgpu_SOURCE_DIR "${DAWN_PRIMARY_INCLUDE_DIR}" CACHE INTERNAL "")
    set(webgpu_VERSION "${DAWN_VERSION}" CACHE INTERNAL "")
    return()

else()
    # Remaining desktop platforms: macOS, Windows (Linux is handled above
    # via dawn-exotic and returns before reaching here). macOS uses google/dawn
    # and Windows uses dawn-exotic, both at the current DAWN_VERSION
    # (v20260714.215939).
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
            set(DAWN_PLATFORM "macos-latest")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
            set(DAWN_PLATFORM "macos-15-intel")
        else()
            message(FATAL_ERROR "Unsupported macOS architecture for Dawn: ${CMAKE_SYSTEM_PROCESSOR}")
        endif()
        set(DAWN_LIB_DIR_NAME "lib")
        set(DAWN_LIB_NAME "libwebgpu_dawn.a")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
            set(DAWN_PLATFORM "windows-latest")
        else()
            message(FATAL_ERROR "Unsupported Windows architecture for Dawn: ${CMAKE_SYSTEM_PROCESSOR}")
        endif()
        set(DAWN_LIB_DIR_NAME "lib")
        set(DAWN_LIB_NAME "webgpu_dawn.lib")
    else()
        message(FATAL_ERROR "Unsupported platform for Dawn: ${CMAKE_SYSTEM_NAME}")
    endif()

    # Build type for Dawn (use Release for both Debug and Release builds - Dawn debug is huge)
    set(DAWN_BUILD_TYPE "Release")

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        # Windows uses the in-house dawn-exotic build: Google's windows-latest
        # prebuilt ships without the OpenGL/GLES backends, so on virtualized
        # GPUs (VMware SVGA 3D: hardware GL, no usable D3D12/Vulkan) Dawn falls
        # back to the WARP software rasterizer. dawn-exotic enables desktop GL
        # and GLES on top of the stock D3D11/D3D12/Vulkan set.
        set(DAWN_URL "https://github.com/zokrezyl/dawn-exotic/releases/download/v${DAWN_VERSION}/dawn-windows-x86_64-release-${DAWN_VERSION}.tar.gz")
    else()
        set(DAWN_URL "https://github.com/google/dawn/releases/download/v${DAWN_VERSION}/Dawn-${DAWN_COMMIT}-${DAWN_PLATFORM}-${DAWN_BUILD_TYPE}.tar.gz")
    endif()

    # Use FetchContent for reliable downloads (handles GitHub 302 redirects properly)
    message(STATUS "Downloading Dawn v${DAWN_VERSION} for ${DAWN_PLATFORM}...")
    message(STATUS "  URL: ${DAWN_URL}")

    FetchContent_Declare(
        dawn_prebuilt
        URL "${DAWN_URL}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(dawn_prebuilt)

    # Set paths - FetchContent extracts to _deps/dawn_prebuilt-src/
    # The tarball contains: Dawn-{commit}-{platform}-{buildtype}/lib[64]/... and include/
    set(DAWN_INCLUDE_DIR "${dawn_prebuilt_SOURCE_DIR}/include")
    set(DAWN_LIB_DIR "${dawn_prebuilt_SOURCE_DIR}/${DAWN_LIB_DIR_NAME}")
    set(DAWN_LIB_PATH "${DAWN_LIB_DIR}/${DAWN_LIB_NAME}")

    # Verify files exist
    if(NOT EXISTS "${DAWN_INCLUDE_DIR}/webgpu/webgpu.h")
        message(FATAL_ERROR "Dawn headers not found at ${DAWN_INCLUDE_DIR}\n"
            "Expected: ${DAWN_INCLUDE_DIR}/webgpu/webgpu.h")
    endif()

    if(NOT EXISTS "${DAWN_LIB_PATH}")
        message(FATAL_ERROR "Dawn library not found at ${DAWN_LIB_PATH}")
    endif()

    # Create imported STATIC library target
    add_library(webgpu STATIC IMPORTED GLOBAL)

    # Dawn requires additional system libraries - set all properties together
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        include(${YETTY_ROOT}/build-tools/yetty/x11-static.cmake)
        set_target_properties(webgpu PROPERTIES
            IMPORTED_LOCATION "${DAWN_LIB_PATH}"
            INTERFACE_INCLUDE_DIRECTORIES "${DAWN_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${YETTY_X11_STATIC_LIBS};${CMAKE_DL_LIBS};pthread"
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        find_library(COCOA_LIBRARY Cocoa REQUIRED)
        find_library(METAL_LIBRARY Metal REQUIRED)
        find_library(QUARTZCORE_LIBRARY QuartzCore REQUIRED)
        find_library(IOKIT_LIBRARY IOKit REQUIRED)
        find_library(IOSURFACE_LIBRARY IOSurface REQUIRED)
        set_target_properties(webgpu PROPERTIES
            IMPORTED_LOCATION "${DAWN_LIB_PATH}"
            INTERFACE_INCLUDE_DIRECTORIES "${DAWN_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${COCOA_LIBRARY};${METAL_LIBRARY};${QUARTZCORE_LIBRARY};${IOKIT_LIBRARY};${IOSURFACE_LIBRARY}"
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        # Dawn on Windows needs D3D12, DXGI, and various Windows libraries
        # CompareObjectHandles is in kernel32 but needs Windows 10 SDK targeting
        set_target_properties(webgpu PROPERTIES
            IMPORTED_LOCATION "${DAWN_LIB_PATH}"
            INTERFACE_INCLUDE_DIRECTORIES "${DAWN_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "d3d12;dxgi;dxguid;d3dcompiler;user32;gdi32;ole32;shell32;kernel32;onecore"
        )
    else()
        set_target_properties(webgpu PROPERTIES
            IMPORTED_LOCATION "${DAWN_LIB_PATH}"
            INTERFACE_INCLUDE_DIRECTORIES "${DAWN_INCLUDE_DIR}"
        )
    endif()

    target_compile_definitions(webgpu INTERFACE WEBGPU_BACKEND_DAWN)

    message(STATUS "Dawn v${DAWN_VERSION} ready:")
    message(STATUS "  Library: ${DAWN_LIB_PATH}")
    message(STATUS "  Headers: ${DAWN_INCLUDE_DIR}")
endif()

# Export variables for use elsewhere
set(DAWN_NATIVE_INCLUDE_DIR "${DAWN_INCLUDE_DIR}" CACHE INTERNAL "")
set(DAWN_NATIVE_LIB_PATH "${DAWN_LIB_PATH}" CACHE INTERNAL "")

# Mark as available for CPM compatibility
set(CPM_PACKAGES "${CPM_PACKAGES};webgpu" CACHE INTERNAL "")
set(webgpu_SOURCE_DIR "${DAWN_INCLUDE_DIR}" CACHE INTERNAL "")
set(webgpu_VERSION "${DAWN_VERSION}" CACHE INTERNAL "")
