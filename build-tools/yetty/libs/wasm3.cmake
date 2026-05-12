# wasm3 - WebAssembly interpreter
if(TARGET wasm3_lib)
    return()
endif()

CPMAddPackage(
    NAME wasm3
    GITHUB_REPOSITORY wasm3/wasm3
    GIT_TAG v0.5.0
    DOWNLOAD_ONLY YES
)

if(wasm3_ADDED)
    # Collect wasm3 source files
    file(GLOB WASM3_SOURCES
        ${wasm3_SOURCE_DIR}/source/*.c
    )

    # Create wasm3 library
    add_library(wasm3_lib STATIC ${WASM3_SOURCES})

    target_include_directories(wasm3_lib PUBLIC
        ${wasm3_SOURCE_DIR}/source
    )

    # Optional: Configuration defines
    # d_m3HasFloat - enable floating point (default: on)
    # d_m3LogOutput - enable debug output
    target_compile_definitions(wasm3_lib PRIVATE
        d_m3HasFloat=1
    )

    set_target_properties(wasm3_lib PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
    )

    # Suppress warnings in third-party code
    if(MSVC)
        target_compile_options(wasm3_lib PRIVATE /w)
    else()
        target_compile_options(wasm3_lib PRIVATE -w)
    endif()

    message(STATUS "wasm3: WebAssembly interpreter v0.5.0")
endif()
