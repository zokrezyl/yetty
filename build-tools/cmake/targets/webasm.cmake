# WebAssembly (Emscripten) build target

# Disable desktop-only libraries
set(YETTY_ENABLE_LIB_GLFW OFF CACHE BOOL "" FORCE)
# libco is desktop-only. Webasm doesn't need a coroutine library: the
# wgpu _await wrappers (src/yetty/yplatform/webasm/ywebgpu.c) suspend the
# C stack via Asyncify (emscripten_sleep) instead of switching stacks,
# and the coroutine API (src/yetty/yplatform/webasm/ycoroutine.c) is a
# degenerate stub for source compatibility.
set(YETTY_ENABLE_LIB_LIBCO OFF CACHE BOOL "" FORCE)
# qemu is not built for webasm — the webasm yetty build uses in-process
# TinyEMU (compiled to wasm) instead of a prebuilt QEMU binary.
set(YETTY_ENABLE_LIB_QEMU OFF CACHE BOOL "" FORCE)
# libjpeg-turbo + YVNC re-enabled now that the webasm-mt 3rdparty variant
# (built with -pthread → atomics + bulk-memory) is fetched on EMSCRIPTEN,
# making the prebuilt .a link-compatible with --shared-memory yetty.wasm.

# yetty.wasm is linked with -pthread (--shared-memory). Every .o that
# enters the link must be built with -pthread too, so that wasm-ld sees
# the atomics + bulk-memory features it requires. Apply globally so all
# yetty static libs (yetty_yui, yetty_ymsdf_gen, libvterm, …) inherit it.
add_compile_options(-pthread)
add_link_options(-pthread)

# emcc auto-defines both `EMSCRIPTEN` and `__EMSCRIPTEN__`. The bare
# `EMSCRIPTEN` macro switches several upstream tinyemu sources/headers
# into a stripped-down (single-CPU, no fopen, fs_wget-only) variant we
# don't want. Yetty's webasm uses MEMFS + all CPU variants, so undefine
# the legacy macro globally and rely on `__EMSCRIPTEN__` (the canonical,
# modern spelling) where actually needed.
add_compile_options(-UEMSCRIPTEN)

# Drop the C++ prebuilt libs on webasm. Each one drags in libc++ (so
# std::shared_ptr / std::atomic etc.), and the upstream prebuilt tarballs
# were not produced with the same emscripten/clang version we link with —
# the resulting wasm has caller/callee type-signature mismatches that
# wasm-emscripten-finalize rejects with "popping from empty stack".
# Yetty's webasm config is single-threaded by design, so refcount
# atomics from these libs are pure cost with no benefit.
set(YETTY_ENABLE_LIB_THORVG     OFF CACHE BOOL "" FORCE)  # vector graphics renderer (C++)
set(YETTY_ENABLE_LIB_LIBSSH2    OFF CACHE BOOL "" FORCE)  # transitively pulls openssl libcrypto
set(YETTY_ENABLE_LIB_OPENH264   OFF CACHE BOOL "" FORCE)  # H.264 decoder (C++)
# pdfio + tree-sitter + imgui prebuilts for webasm-mt are not published
# yet (404 on the *-webasm-mt-*.tar.gz release assets). Disable here
# until the tarballs are built and uploaded; ypdf + ycat consume pdfio
# (and ycat consumes tree-sitter), and ymgui pulls imgui — so those
# features go too. ymgui-layer.c in yterm is pure C and doesn't link
# imgui directly, so yterm builds fine without YMGUI.
set(YETTY_ENABLE_LIB_PDFIO       OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_LIB_TREESITTER  OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_YPDF    OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_YCAT    OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_YMGUI   OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YCAT       OFF CACHE BOOL "" FORCE)
# msdfgen + tinyxml2 stay ON: yetty_ypaint hard-depends on
# yetty_ymsdf_gen, which in turn pulls msdfgen-core/msdfgen-ext +
# tinyxml2. Unwinding that needs source surgery in ypaint.
set(YETTY_ENABLE_LIB_MSDFGEN       ON  CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_YMSDF_GEN ON  CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_YTHORVG   OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_SSH       OFF CACHE BOOL "" FORCE)

include(${YETTY_ROOT}/build-tools/cmake/targets/shared.cmake)
include(${YETTY_ROOT}/build-tools/cmake/tinyemu.cmake)

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
    ${YETTY_ROOT}/src/yetty/yplatform/webasm/window.c
    ${YETTY_ROOT}/src/yetty/yplatform/webasm/event-loop.c
    ${YETTY_ROOT}/src/yetty/yplatform/webasm/pipe.c
    ${YETTY_ROOT}/src/yetty/yplatform/webasm/platform-paths.c
    ${YETTY_ROOT}/src/yetty/yplatform/webasm/ycoroutine.c
    ${YETTY_ROOT}/src/yetty/yplatform/webasm/ywebgpu.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/tinyemu-pty.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/extract-assets.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/fs.c
    ${YETTY_ROOT}/src/yetty/yncbin/incbin-assets.c
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

target_include_directories(yetty PRIVATE ${YETTY_INCLUDES} ${YETTY_RENDERER_INCLUDES} ${JPEG_INCLUDE_DIRS})

# Embed resources directly into yetty.wasm (same as desktop platforms).
# Logo + DefaultConfig are referenced by name; yetty_embed_assets adds
# the bulk fonts / shaders / msdf-fonts / yemu (each pre-brotli'd; the
# extract-assets startup path decompresses to MEMFS).
# Same extraction code path as desktop.
if(YETTY_ENABLE_LIB_INCBIN)
    incbin_add_resources(yetty
        Logo "${YETTY_ROOT}/docs/logo-2.jpeg"
        DefaultConfig "${YETTY_ROOT}/assets/default-config.yaml"
    )
    
    # yetty_embed_assets() will embed shaders, fonts, config, and yemu/ directory
    # (kernel, opensbi, rootfs) using the same pipeline as desktop.
    yetty_embed_assets(yetty)
endif()

if(YETTY_ENABLE_FEATURE_ASSETS)
    add_dependencies(yetty copy-assets)
endif()

if(YETTY_ENABLE_FEATURE_YSHADERS)
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
    CONFIG_SLIRP=1
)

# (removed dead `${ytrace_SOURCE_DIR}/include ${spdlog_SOURCE_DIR}/include`
# — neither external lib is used by yetty's C source; the C ytrace impl
# is in src/yetty/ytrace/ytrace.c.)

target_link_options(yetty PRIVATE
    -sUSE_GLFW=3
    --use-port=emdawnwebgpu
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=65536
    -sSTACK_SIZE=1048576
    -sWASM_BIGINT
    -sFILESYSTEM=1
    # `-pthread + -sALLOW_MEMORY_GROWTH=1` makes every libc syscall shim
    # (pipe read/write, select, …) run on the slow JS-side path — emcc
    # warns about the combo. The VM thread hammers these per cycle batch,
    # which compounds into seconds of input/output latency. Fixed-size
    # SharedArrayBuffer lets the JIT specialise the shims and the JS
    # heap stays put. We already reserve 2 GiB; host has 16 GiB free.
    -sALLOW_MEMORY_GROWTH=0
    -sINITIAL_MEMORY=2048MB
    -sMAXIMUM_MEMORY=2048MB
    -sASSERTIONS=2
    --emit-symbol-map
    -lwebsocket.js
    -pthread
    -sPTHREAD_POOL_SIZE=4
    # No --preload-file=assets — fonts/shaders/CDB/yemu are baked into
    # yetty.wasm via yetty_embed_assets() (incbin, brotli-compressed) and
    # decompressed into MEMFS at startup by yetty_yplatform_extract_assets.
    # Same flow as desktop targets.
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8','FS','ENV','HEAPU8']"
    # "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_yetty_write','_yetty_key','_yetty_special_key','_yetty_read_input','_yetty_sync','_yetty_set_scale','_yetty_resize','_yetty_get_cols','_yetty_get_rows','_webpty_on_data']"
    "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']"
)

if(YETTY_ENABLE_FEATURE_DEMO)
    target_link_options(yetty PRIVATE
        "--preload-file=${CMAKE_BINARY_DIR}/demo@/demo"
        "--preload-file=${CMAKE_BINARY_DIR}/src@/src"
    )
endif()

target_compile_options(yetty PRIVATE --use-port=emdawnwebgpu -fexceptions -pthread)
target_link_options(yetty PRIVATE -fexceptions)
set_target_properties(yetty PROPERTIES SUFFIX ".js")

target_link_libraries(yetty PRIVATE
    ${YETTY_LIBS}
    tinyemu
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

# msdf-fonts CDBs and the yemu kernel/opensbi/img bundle are no longer
# staged into the preload — they're embedded into yetty.wasm via
# yetty_embed_assets() (see incbin path) and yetty_yplatform_extract_assets()
# decompresses them into MEMFS at startup. Same flow as desktop.
add_custom_command(TARGET yetty PRE_LINK
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/assets"
    COMMENT "(no 3rdparty staging — assets are embedded via incbin and extracted at startup)"
)

# Copy web files
add_custom_command(TARGET yetty POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${YETTY_ROOT}/build-tools/web/index.html ${CMAKE_BINARY_DIR}/index.html
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${YETTY_ROOT}/build-tools/web/serve.py ${CMAKE_BINARY_DIR}/serve.py
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${YETTY_ROOT}/assets/favicon.ico ${CMAKE_BINARY_DIR}/favicon.ico
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${YETTY_ROOT}/assets/apple-touch-icon.jpg ${CMAKE_BINARY_DIR}/apple-touch-icon.jpg
)

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
