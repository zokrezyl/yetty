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
    # Emscripten: WebGPU via emdawnwebgpu port, pinned to the same Dawn release
    # as every other platform. emscripten's --use-port takes either a built-in
    # port name (locked to whatever version the installed emscripten vendors) or
    # a path to a Python port module — there is no version-string input. So we
    # download google/dawn's per-release port script matching DAWN_VERSION at
    # configure time and hand emcc that. DAWN_VERSION (dawn-version.cmake) is the
    # single knob; nothing about the port is committed to the tree.
    include(${YETTY_ROOT}/build-tools/yetty/dawn-version.cmake)

    set(_emdawn_port_dir "${CMAKE_BINARY_DIR}/emdawnwebgpu")
    # Keep the basename stable — emcc derives the port name from the filename,
    # and the same name must be used everywhere it's passed (compile here + link
    # on yetty/ygreeter) or emcc reports a duplicate port.
    set(_emdawn_port "${_emdawn_port_dir}/emdawnwebgpu.remoteport.py")
    if(NOT EXISTS "${_emdawn_port}")
        set(_emdawn_url
            "https://github.com/google/dawn/releases/download/v${DAWN_VERSION}/emdawnwebgpu-v${DAWN_VERSION}.remoteport.py")
        message(STATUS "WebGPU: fetching emdawnwebgpu port v${DAWN_VERSION}")
        message(STATUS "  URL: ${_emdawn_url}")
        file(DOWNLOAD "${_emdawn_url}" "${_emdawn_port}"
            TLS_VERIFY ON
            STATUS _emdawn_dl_status)
        list(GET _emdawn_dl_status 0 _emdawn_dl_code)
        if(NOT _emdawn_dl_code EQUAL 0)
            list(GET _emdawn_dl_status 1 _emdawn_dl_msg)
            file(REMOVE "${_emdawn_port}")
            message(FATAL_ERROR
                "Failed to download emdawnwebgpu port v${DAWN_VERSION}: ${_emdawn_dl_msg}\n"
                "  ${_emdawn_url}")
        endif()
    endif()
    # Expose the resolved path to the webasm platform link steps (yetty/ygreeter).
    set(YETTY_EMDAWNWEBGPU_PORT "${_emdawn_port}" CACHE INTERNAL "emdawnwebgpu port path")

    add_library(webgpu INTERFACE)
    target_compile_options(webgpu INTERFACE "SHELL:--use-port=${_emdawn_port}")
    message(STATUS "WebGPU: Using emdawnwebgpu port v${DAWN_VERSION} for Emscripten")

else()
    # Desktop: Only Dawn is supported
    include(${YETTY_ROOT}/build-tools/yetty/Dawn.cmake)
endif()
