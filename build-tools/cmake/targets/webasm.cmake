# WebAssembly (Emscripten) build target

# Disable desktop-only libraries
set(YETTY_ENABLE_LIB_LIBUV OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_LIB_GLFW OFF CACHE BOOL "" FORCE)
# libco is desktop-only — webasm uses emscripten_fiber_t (Asyncify) instead.
set(YETTY_ENABLE_LIB_LIBCO OFF CACHE BOOL "" FORCE)
# qemu is not built for webasm — the webasm yetty build uses in-process
# TinyEMU (compiled to wasm) instead of a prebuilt QEMU binary.
set(YETTY_ENABLE_LIB_QEMU OFF CACHE BOOL "" FORCE)

include(${YETTY_ROOT}/build-tools/cmake/targets/shared.cmake)

# Copy runtime assets (fonts, etc.) to build directory
if(YETTY_ENABLE_FEATURE_ASSETS)
    add_subdirectory(${YETTY_ROOT}/assets ${CMAKE_BINARY_DIR}/assets-build)
endif()

# Global definitions for all webasm targets (applied before add_subdirectory)
add_compile_definitions(YETTY_WEB=1 YETTY_ANDROID=0)

# Set shader directory path for web (used by card libraries)
set(YETTY_SHADERS_DIR "/assets/shaders" CACHE STRING "Shader directory path")

# Platform sources
set(YETTY_PLATFORM_SOURCES
    ${YETTY_ROOT}/src/yetty/yplatform/webasm/main.c
    ${YETTY_ROOT}/src/yetty/yplatform/webasm/surface.c
    ${YETTY_ROOT}/src/yetty/yplatform/webasm/webasm-pty.c
    ${YETTY_ROOT}/src/yetty/yplatform/webasm/window.c
    ${YETTY_ROOT}/src/yetty/yplatform/webasm/event-loop.c
    ${YETTY_ROOT}/src/yetty/yplatform/webasm/pipe.c
    ${YETTY_ROOT}/src/yetty/yplatform/webasm/platform-paths.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/thread.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/term.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/time.c
)

# Create executable with core sources + web platform
add_executable(yetty
    ${YETTY_SOURCES}
    ${YETTY_CORE_SOURCES}
    ${YETTY_WEB_SOURCES}
    ${YETTY_PLATFORM_SOURCES}
)

# JSLinux integration (downloads and copies files)
if(YETTY_ENABLE_FEATURE_JSLINUX)
    add_subdirectory(${YETTY_ROOT}/build-tools/jslinux ${CMAKE_BINARY_DIR}/jslinux-build)
endif()

target_include_directories(yetty PRIVATE ${YETTY_INCLUDES} ${YETTY_RENDERER_INCLUDES} ${JPEG_INCLUDE_DIRS})

# Embed resources (stubs on web, but needed for symbol resolution)
# Note: webasm uses preload files, not incbin, so we only need stubs for symbols
if(YETTY_ENABLE_LIB_INCBIN)
    incbin_add_resources(yetty
        Logo "${YETTY_ROOT}/docs/logo-2.jpeg"
        DefaultConfig "${YETTY_ROOT}/assets/default-config.yaml"
    )
endif()

if(YETTY_ENABLE_FEATURE_ASSETS)
    add_dependencies(yetty copy-assets)
endif()

if(YETTY_ENABLE_FEATURE_SHADERS)
    add_dependencies(yetty copy-shaders)
endif()

target_compile_definitions(yetty PRIVATE
    ${YETTY_DEFINITIONS}
    YETTY_WEB=1
    YETTY_ANDROID=0
    YETTY_USE_PREBUILT_ATLAS=1
    YTRACE_ENABLED=1
    YTRACE_NO_CONTROL_SOCKET=1
    YTRACE_USE_SPDLOG=1
    YETTY_ASSETS_DIR="/assets"
    YETTY_SHADERS_DIR="/assets/shaders"
    YETTY_HAS_VNC=1
)

# (removed dead `${ytrace_SOURCE_DIR}/include ${spdlog_SOURCE_DIR}/include`
# — neither external lib is used by yetty's C source; the C ytrace impl
# is in src/yetty/ytrace.c.)

target_link_options(yetty PRIVATE
    -sUSE_GLFW=3
    --use-port=emdawnwebgpu
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=65536
    -sSTACK_SIZE=1048576
    -sWASM_BIGINT
    -sFILESYSTEM=1
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=2048MB
    -sASSERTIONS=2
    -lwebsocket.js
    "--preload-file=${CMAKE_BINARY_DIR}/assets@/assets"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8','FS','ENV','HEAPU8']"
    # "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_yetty_write','_yetty_key','_yetty_special_key','_yetty_read_input','_yetty_sync','_yetty_set_scale','_yetty_resize','_yetty_get_cols','_yetty_get_rows','_webpty_on_data']"
    "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_webpty_pipe_source_notify']"
)

if(YETTY_ENABLE_FEATURE_DEMO)
    target_link_options(yetty PRIVATE
        "--preload-file=${CMAKE_BINARY_DIR}/demo@/demo"
        "--preload-file=${CMAKE_BINARY_DIR}/src@/src"
    )
endif()

target_compile_options(yetty PRIVATE --use-port=emdawnwebgpu -fexceptions)
target_link_options(yetty PRIVATE -fexceptions)
set_target_properties(yetty PROPERTIES SUFFIX ".js")

target_link_libraries(yetty PRIVATE
    ${YETTY_LIBS}
    Freetype::Freetype
)

# Copy demo and source tree to build directory for preloading
if(YETTY_ENABLE_FEATURE_DEMO)
    add_custom_command(TARGET yetty PRE_LINK
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${YETTY_ROOT}/demo ${CMAKE_BINARY_DIR}/demo
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${YETTY_ROOT}/src ${CMAKE_BINARY_DIR}/src
        COMMENT "Copying demo and source tree to build directory"
    )
endif()

# Stage fetched 3rdparty assets into ${CMAKE_BINARY_DIR}/assets/<subdir>/
# pre-link, so emcc's --preload-file=${CMAKE_BINARY_DIR}/assets@/assets
# (above) packs them into the wasm virtual filesystem under /assets.
# Three fetch dirs (linux + opensbi + alpine-disk) all contribute to
# /assets/yemu, so a multi-preload would collide — single staging dir is
# the only path. Stamp files (.fetched-<ver>) are filtered out so emcc
# doesn't pack them.
add_custom_command(TARGET yetty PRE_LINK
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/assets/msdf-fonts"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/assets/yemu"
    # cdb (.cdb.br files)
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${YETTY_3RDPARTY_cdb_DIR}" "${CMAKE_BINARY_DIR}/assets/msdf-fonts"
    # linux (kernel + alpine-rootfs/ tree)
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${YETTY_3RDPARTY_linux_DIR}" "${CMAKE_BINARY_DIR}/assets/yemu"
    # opensbi (firmware bins)
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${YETTY_3RDPARTY_opensbi_DIR}" "${CMAKE_BINARY_DIR}/assets/yemu"
    # alpine-disk (raw ext4)
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${YETTY_3RDPARTY_alpine-disk_DIR}" "${CMAKE_BINARY_DIR}/assets/yemu"
    # 3rdparty-fetch leaves stamp + .br files; strip the fetch-internal stamps
    # (the .br files stay — runtime decompresses on read).
    COMMAND ${CMAKE_COMMAND} -E rm -f
        "${CMAKE_BINARY_DIR}/assets/msdf-fonts/.fetched-*"
        "${CMAKE_BINARY_DIR}/assets/yemu/.fetched-*"
    COMMENT "Staging 3rdparty fetched assets into ${CMAKE_BINARY_DIR}/assets/ for emcc preload"
)

# Copy web files
add_custom_command(TARGET yetty POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${YETTY_ROOT}/build-tools/web/index.html ${CMAKE_BINARY_DIR}/index.html
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${YETTY_ROOT}/build-tools/web/serve.py ${CMAKE_BINARY_DIR}/serve.py
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${YETTY_ROOT}/assets/favicon.ico ${CMAKE_BINARY_DIR}/favicon.ico
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${YETTY_ROOT}/assets/apple-touch-icon.jpg ${CMAKE_BINARY_DIR}/apple-touch-icon.jpg
)

# Copy JSLinux files to build output
if(YETTY_ENABLE_FEATURE_JSLINUX)
    add_custom_command(TARGET yetty POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/jslinux
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/jslinux-build/jslinux ${CMAKE_BINARY_DIR}/jslinux
        COMMENT "Copying JSLinux files..."
    )
endif()

# Generate pre-computed demo script outputs
if(YETTY_ENABLE_FEATURE_DEMO)
    add_custom_target(generate-demo-outputs
        COMMAND ${CMAKE_COMMAND}
            -DYETTY_ROOT=${YETTY_ROOT}
            -DOUTPUT_DIR=${CMAKE_BINARY_DIR}
            -P ${YETTY_ROOT}/build-tools/cmake/generate-demo-outputs.cmake
        COMMENT "Generating demo script outputs..."
    )
    add_dependencies(yetty generate-demo-outputs)
endif()

# Verify all required assets are present
if(YETTY_ENABLE_FEATURE_ASSETS)
    add_custom_command(TARGET yetty POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGET_TYPE=webasm -P ${YETTY_ROOT}/build-tools/cmake/verify-assets.cmake
        COMMENT "Verifying build assets..."
    )
endif()
