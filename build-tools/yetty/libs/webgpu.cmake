# WebGPU - Graphics API abstraction
# Supports: Dawn, Emscripten emdawnwebgpu
if(TARGET webgpu)
    return()
endif()

message(STATUS "WebGPU backend: ${WEBGPU_BACKEND}")

if(YETTY_ANDROID)
    # Android: Use pre-built Dawn WebGPU library from ANDROID_BUILD_DIR
    if(NOT DEFINED ANDROID_BUILD_DIR)
        if(DEFINED ENV{ANDROID_BUILD_DIR})
            set(ANDROID_BUILD_DIR $ENV{ANDROID_BUILD_DIR})
        else()
            message(FATAL_ERROR "ANDROID_BUILD_DIR not set")
        endif()
    endif()

    # ANDROID_ABI is set by the NDK toolchain (arm64-v8a, x86_64, etc.)
    if(NOT DEFINED ANDROID_ABI)
        message(FATAL_ERROR "ANDROID_ABI not set")
    endif()
    message(STATUS "Android ABI: ${ANDROID_ABI}")

    # Only Dawn is supported on Android
    set(WEBGPU_LIB "${ANDROID_BUILD_DIR}/dawn-libs/${ANDROID_ABI}/libwebgpu_dawn.a")
    set(WEBGPU_INCLUDE "${ANDROID_BUILD_DIR}/dawn-include")

    if(NOT EXISTS "${WEBGPU_LIB}")
        message(FATAL_ERROR "WebGPU Dawn library not found at ${WEBGPU_LIB}\n"
            "Run the Android Dawn build first: make _android-dawn-libs")
    endif()

    add_library(webgpu STATIC IMPORTED GLOBAL)
    set_target_properties(webgpu PROPERTIES
        IMPORTED_LOCATION "${WEBGPU_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${WEBGPU_INCLUDE}"
    )
    target_compile_definitions(webgpu INTERFACE WEBGPU_BACKEND_DAWN)

elseif(EMSCRIPTEN)
    # Emscripten: WebGPU via emdawnwebgpu port
    add_library(webgpu INTERFACE)
    target_compile_options(webgpu INTERFACE "SHELL:--use-port=emdawnwebgpu")
    message(STATUS "WebGPU: Using emdawnwebgpu port for Emscripten")

else()
    # Desktop: Only Dawn is supported
    include(${YETTY_ROOT}/build-tools/cmake/Dawn.cmake)
endif()
