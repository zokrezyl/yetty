# WebAssembly (Emscripten) build target
include(${CMAKE_CURRENT_LIST_DIR}/variables.cmake)

include(${YETTY_ROOT}/build-tools/yetty/platform/shared.cmake)
include(${YETTY_ROOT}/build-tools/yetty/tinyemu.cmake)
include(${YETTY_ROOT}/build-tools/yetty/tinyemu-iframe.cmake)

# Copy runtime assets (fonts, etc.) to build directory
if(YETTY_ENABLE_FEATURE_ASSETS)
    add_subdirectory(${YETTY_ROOT}/assets ${CMAKE_BINARY_DIR}/assets-build)
endif()

# Global definitions for all webasm platforms (applied before add_subdirectory)
add_compile_definitions(YETTY_WEB=1 YETTY_ANDROID=0)

# Set shader directory path for web (used by card libraries)
set(YETTY_SHADERS_DIR "/assets/shaders" CACHE STRING "Shader directory path")

# Platform sources
set(YETTY_PLATFORM_SOURCES
    ${YETTY_ROOT}/src/yetty/ymain/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/window/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/pipe/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/paths/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/coroutine/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu/webasm.c
    ${YETTY_ROOT}/src/yetty/ypty/iframepty.c
    ${YETTY_ROOT}/src/yetty/ytransport/iframe-transport.c
    ${YETTY_ROOT}/src/yetty/yplatform/pty-factory/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/webasm/brotli-glue.c
    ${YETTY_ROOT}/src/yetty/yplatform/extract-assets/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/fs/default.c
    ${YETTY_ROOT}/src/yetty/yncbin/incbin-assets.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/time/default.c
)

# Create executable with core sources + web platform
add_executable(yetty
    ${YETTY_SOURCES}
    ${YETTY_CORE_SOURCES}
    ${YETTY_WEB_SOURCES}
    ${YETTY_PLATFORM_SOURCES}
)

target_include_directories(yetty PRIVATE ${YETTY_INCLUDES} ${YETTY_RENDERER_INCLUDES} ${JPEG_INCLUDE_DIRS})

# Asset delivery: webasm does NOT use incbin. The desktop pipeline
# (yetty_embed_assets) bakes ~200 MB of shaders / fonts / msdf CDBs /
# kernel / rootfs into the binary via incbin, which on emscripten means
# clang has to chew through a 240 MB yetty_data_data.c — the link runs
# multi-GB, hangs the host, and doubles the wasm size needlessly.
# Instead we stage every asset as a standalone file under
# build/yetty-assets/ and let the runtime fetch them at startup
# (build-tools/web/yetty-assets-preload.js, wired below as --pre-js).
# Decompression uses the browser's DecompressionStream('br') so no JS
# brotli library is needed. Kernel/opensbi/rootfs are NOT here — they
# moved to tinyemu.data, owned by the iframe wasm.
include(${YETTY_ROOT}/build-tools/yetty/webasm-stage-assets.cmake)
yetty_stage_webasm_assets()

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
    # Single-threaded build now that TinyEMU lives in its own iframe
    # wasm. Without -pthread the libc syscall shims are direct (no
    # pthread-proxy round-trip, no Atomics.wait), so memory growth no
    # longer pessimises them and we can let the heap expand naturally.
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=256MB
    -sASSERTIONS=2
    --emit-symbol-map
    -lwebsocket.js
    # Asset preload: fetch + brotli-decompress + FS.writeFile in
    # Module.preRun. Manifest + files are produced by
    # yetty_stage_webasm_assets() above and copied next to yetty.wasm at
    # POST_BUILD time. Module.addRunDependency keeps main() blocked
    # until every asset is in MEMFS.
    "--pre-js=${YETTY_ROOT}/build-tools/web/yetty-assets-preload.js"
    # callMain — yetty-assets-preload.js sets Module.noInitialRun and
    # invokes main() manually after asset preload finishes; without
    # callMain in the runtime exports, Module.callMain is undefined.
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8','FS','ENV','HEAPU8','HEAPU32','callMain']"
    # Exported wasm symbols:
    #   _iframe_pty_on_data — JS message listener pushes VM output here.
    #   _yetty_brotli_decode — asset preload shim calls this to
    #     decompress *.br assets in MEMFS before main() runs.
    #   _yetty_iframe_transport_on_{opened,rx,closed} — postMessage
    #     listener (iframe-transport.c) routes session events from
    #     the tinyemu iframe to the right transport instance.
    "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_iframe_pty_on_data','_yetty_brotli_decode','_yetty_ytransport_iframe_transport_on_opened','_yetty_ytransport_iframe_transport_on_rx','_yetty_ytransport_iframe_transport_on_closed']"
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

# brotli decoder is consumed by webasm/brotli-glue.c, exported as
# _yetty_brotli_decode for the asset preload shim. Single-threaded
# webasm prebuilt — see build-tools/yetty/libs/brotli.cmake.
include(${YETTY_ROOT}/build-tools/yetty/libs/brotli.cmake)

target_link_libraries(yetty PRIVATE
    ${YETTY_LIBS}
    tinyemu
    Freetype::Freetype
    brotlidec
    brotlicommon
)

# Force the iframe wasm artifact (tinyemu.{js,wasm,data}) to build whenever
# yetty.{js,wasm} is built — they ship together, the iframe is useless
# without it. The Makefile's `cmake --build … --target yetty` invocation
# walks this dependency.
add_dependencies(yetty tinyemu_vm)

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

# Copy web files. yetty-assets/ already lives under ${CMAKE_BINARY_DIR}
# (yetty_stage_webasm_assets writes there), so it's automatically
# alongside yetty.{js,wasm} and tinyemu.{js,wasm,data} — no extra copy
# step needed. The pre-js shim fetches yetty-assets/manifest.json with
# a relative URL, which works for both serve.py and any static-file CDN.
#
# IMPORTANT: a POST_BUILD on TARGET yetty alone is NOT enough — when
# only the HTML or pre-js changes (no C touched), ninja sees yetty as
# up-to-date and the POST_BUILD never fires. The user then tests with
# stale HTML and wonders why "the JS changes don't take effect". To
# fix this we ALSO declare a phony target with output-file dependencies
# so ninja re-runs the copies whenever any source HTML/JS changes.
set(_WEB_COPY_FILES
    ${CMAKE_BINARY_DIR}/index.html
    ${CMAKE_BINARY_DIR}/tinyemu-iframe.html
    ${CMAKE_BINARY_DIR}/serve.py
    ${CMAKE_BINARY_DIR}/favicon.ico
    ${CMAKE_BINARY_DIR}/apple-touch-icon.jpg
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/index.html
    COMMAND ${CMAKE_COMMAND} -E copy ${YETTY_ROOT}/build-tools/web/index.html ${CMAKE_BINARY_DIR}/index.html
    DEPENDS ${YETTY_ROOT}/build-tools/web/index.html
    COMMENT "Copy web/index.html → build dir"
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/tinyemu-iframe.html
    COMMAND ${CMAKE_COMMAND} -E copy ${YETTY_ROOT}/build-tools/web/tinyemu-iframe.html ${CMAKE_BINARY_DIR}/tinyemu-iframe.html
    DEPENDS ${YETTY_ROOT}/build-tools/web/tinyemu-iframe.html
    COMMENT "Copy web/tinyemu-iframe.html → build dir"
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/serve.py
    COMMAND ${CMAKE_COMMAND} -E copy ${YETTY_ROOT}/build-tools/web/serve.py ${CMAKE_BINARY_DIR}/serve.py
    DEPENDS ${YETTY_ROOT}/build-tools/web/serve.py
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/favicon.ico
    COMMAND ${CMAKE_COMMAND} -E copy ${YETTY_ROOT}/assets/favicon.ico ${CMAKE_BINARY_DIR}/favicon.ico
    DEPENDS ${YETTY_ROOT}/assets/favicon.ico
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/apple-touch-icon.jpg
    COMMAND ${CMAKE_COMMAND} -E copy ${YETTY_ROOT}/assets/apple-touch-icon.jpg ${CMAKE_BINARY_DIR}/apple-touch-icon.jpg
    DEPENDS ${YETTY_ROOT}/assets/apple-touch-icon.jpg
)
add_custom_target(yetty_web_files ALL DEPENDS ${_WEB_COPY_FILES})
add_dependencies(yetty yetty_web_files)

# Generate pre-computed demo script outputs
if(YETTY_ENABLE_FEATURE_DEMO)
    add_custom_target(generate-demo-outputs
        COMMAND ${CMAKE_COMMAND}
            -DYETTY_ROOT=${YETTY_ROOT}
            -DOUTPUT_DIR=${CMAKE_BINARY_DIR}
            -P ${YETTY_ROOT}/build-tools/yetty/generate-demo-outputs.cmake
        COMMENT "Generating demo script outputs..."
    )
    add_dependencies(yetty generate-demo-outputs)
endif()

# Verify all required assets are present
if(YETTY_ENABLE_FEATURE_ASSETS)
    add_custom_command(TARGET yetty POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGET_TYPE=webasm -P ${YETTY_ROOT}/build-tools/yetty/verify-assets.cmake
        COMMENT "Verifying build assets..."
    )
endif()
