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
    ${YETTY_ROOT}/src/yetty/yinit/webasm.c
    ${YETTY_ROOT}/src/yetty/yinit/webasm-run.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/window/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/pipe/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/coroutine/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu/webasm.c
    ${YETTY_ROOT}/src/yetty/ypty/iframepty.c
    ${YETTY_ROOT}/src/yetty/ypty/memory-pty.c
    ${YETTY_ROOT}/src/yetty/ypty/websocket-pty.c
    ${YETTY_ROOT}/src/yetty/ytransport/iframe-transport.c
    ${YETTY_ROOT}/src/yetty/ytransport/websocket-transport.c
    ${YETTY_ROOT}/src/yetty/ytransport/lwip-transport.c
    ${YETTY_ROOT}/src/yetty/yssh/ssh-websocket-pty.c
    ${YETTY_ROOT}/src/yetty/ynet/netstack.c
    ${YETTY_ROOT}/src/yetty/ynet/lwip-port.c
    ${YETTY_ROOT}/src/yetty/yplatform/pty-factory/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/webasm/brotli-glue.c
    ${YETTY_ROOT}/src/yetty/yplatform/extract-assets/default.c
    ${YETTY_ROOT}/src/yetty/yncbin/incbin-assets.c
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

# The pre-js is baked INTO yetty.js at link time — without this, editing
# yetty-assets-preload.js leaves ninja thinking yetty.js is up to date
# and the change silently never ships.
set_target_properties(yetty PROPERTIES
    LINK_DEPENDS ${YETTY_ROOT}/build-tools/web/yetty-assets-preload.js)

# brotli decoder is consumed by webasm/brotli-glue.c, exported as
# _yetty_brotli_decode for the asset preload shim. Single-threaded
# webasm prebuilt — see build-tools/yetty/libs/brotli.cmake.
include(${YETTY_ROOT}/build-tools/yetty/libs/brotli.cmake)

# libssh2 (mbedTLS backend) — consumed by yssh/ssh-websocket-pty.c for
# the --ssh-over-websocket session mode. From-source emscripten build.
include(${YETTY_ROOT}/build-tools/yetty/libs/libssh2-webasm.cmake)

# lwIP — userspace TCP/IP stack for the in-browser netstack (ynet). Gives
# real TCP connectivity over an L2 relay WebSocket, no VM. FetchContent
# source build with yetty's toolchain; PUBLIC include dirs propagate to
# the ynet sources compiled into the yetty target.
include(${YETTY_ROOT}/build-tools/yetty/libs/lwip-webasm.cmake)

target_link_libraries(yetty PRIVATE
    ${YETTY_LIBS}
    tinyemu
    Freetype::Freetype
    brotlidec
    brotlicommon
    libssh2_webasm
    lwip_webasm
    yetty_yplatform_core
)

# Force the iframe wasm artifact (tinyemu.{js,wasm,data}) to build whenever
# yetty.{js,wasm} is built — they ship together, the iframe is useless
# without it. The Makefile's `cmake --build … --target yetty` invocation
# walks this dependency.
add_dependencies(yetty tinyemu_vm)

# ============================================================================
# ygreeter.wasm — the ygui feature-showcase app, standalone in the browser.
#
# Reuses ygreeter's existing standalone mode wholesale: main() →
# run_standalone_mode → yetty_yinit_run(..., standalone_worker, ...). The
# only thing that was missing for the web was the webasm yetty_yinit_run
# (yinit/webasm-run.c), now shared. ygreeter renders the ygui figure tree
# into its own WebGPU canvas with no terminal, no shell, no VM.
#
# Assets: reuses yetty's --pre-js asset preload (fonts / shaders /
# msdf-fonts staged into /data); ygreeter's own incbin path is compiled
# out (embedded-assets.c is a no-op without HAS_DATA_MANIFEST). The GPU
# device is handed in from JS (ygreeter.html), same as yetty.
# ============================================================================
add_executable(ygreeter
    ${YETTY_ROOT}/tools/ygreeter/main.c
    ${YETTY_ROOT}/tools/ygreeter/embedded-assets.c
    ${YETTY_ROOT}/src/yetty/yshadertoy/demo-shaders.c
    # Core sources ygreeter compiles directly (not provided as libs) —
    # mirrors tools/ygreeter/CMakeLists.txt's YGREETER_SOURCES.
    ${YETTY_ROOT}/src/yetty/yframework/yframework.c
    ${YETTY_ROOT}/src/yetty/yconfig/config.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/ynotify/ynotify.c
    ${YETTY_ROOT}/src/yetty/yinit/webasm-run.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/window/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/pipe/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/coroutine/webasm.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu/webasm.c
    ${YETTY_ROOT}/src/yetty/ypty/memory-pty.c
    ${YETTY_ROOT}/src/yetty/yplatform/webasm/brotli-glue.c
)

target_include_directories(ygreeter BEFORE PRIVATE
    ${YETTY_ROOT}/src
    ${YETTY_ROOT}/include
    ${YETTY_RENDERER_INCLUDES}
)

target_compile_definitions(ygreeter PRIVATE
    ${YETTY_DEFINITIONS}
    YETTY_WEB=1
    YETTY_ANDROID=0
    YETTY_USE_PREBUILT_ATLAS=1
    YETTY_YGREETER_HAS_STANDALONE
    YETTY_YGREETER_HAS_CHROME
    YTRACE_ENABLED=1
    YTRACE_NO_CONTROL_SOCKET=1
    YTRACE_USE_SPDLOG=1
    YETTY_ASSETS_DIR="/assets"
    YETTY_SHADERS_DIR="/assets/shaders"
)

target_compile_options(ygreeter PRIVATE --use-port=emdawnwebgpu -fexceptions)

target_link_options(ygreeter PRIVATE
    -sUSE_GLFW=3
    --use-port=emdawnwebgpu
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=65536
    -sSTACK_SIZE=1048576
    -sWASM_BIGINT
    -sFILESYSTEM=1
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=256MB
    -sASSERTIONS=2
    --emit-symbol-map
    -fexceptions
    "--pre-js=${YETTY_ROOT}/build-tools/web/yetty-assets-preload.js"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8','FS','ENV','HEAPU8','HEAPU32','callMain']"
    "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_yetty_brotli_decode']"
)
set_target_properties(ygreeter PROPERTIES SUFFIX ".js" LINKER_LANGUAGE CXX)
set_target_properties(ygreeter PROPERTIES
    LINK_DEPENDS ${YETTY_ROOT}/build-tools/web/yetty-assets-preload.js)

# Standalone ygreeter's lib set (mirrors tools/ygreeter/CMakeLists.txt's
# webgpu link line). move_resize / window_manager are desktop-only — on
# webasm the window_manager symbols resolve via stubs in
# yetty_yplatform_core (window_manager is NULL, the stubs early-return).
target_link_libraries(ygreeter PRIVATE
    yetty_ygui
    yetty_ywire
    yetty_ychrome
    yetty_ycircuit
    yetty_yfigure
    yetty_ygrid
    yetty_yshadertoy
    yetty_yfont_core
    yetty_ydraw_factory
    yetty_yimage
    yetty_yplot
    ${YETTY_LIBS}
    Freetype::Freetype
    brotlidec
    brotlicommon
    yetty_yplatform_core
)

# Copy ygreeter's loader page next to ygreeter.{js,wasm}. It reuses the
# same yetty-assets/ preload directory (already staged alongside, see
# yetty_stage_webasm_assets) for fonts/shaders/msdf-fonts.
add_custom_command(TARGET ygreeter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${YETTY_ROOT}/build-tools/web/ygreeter.html
            ${CMAKE_BINARY_DIR}/ygreeter.html
    COMMENT "Copy web/ygreeter.html → build dir")
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY
    CMAKE_CONFIGURE_DEPENDS "${YETTY_ROOT}/build-tools/web/ygreeter.html")

# Build ygreeter whenever yetty is built (the Makefile only names the
# `yetty` target), so `make build-webasm-*` produces ygreeter.{js,wasm}
# + ygreeter.html for the picker's showcase option.
add_dependencies(yetty ygreeter)

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
    ${CMAKE_BINARY_DIR}/tinyemu-iframe.html
    ${CMAKE_BINARY_DIR}/serve.py
    ${CMAKE_BINARY_DIR}/favicon.ico
    ${CMAKE_BINARY_DIR}/apple-touch-icon.jpg
)

# index.html is generated by configure_file() so the latest yetty-X.Y.Z
# tag (resolved at configure time) can be baked into the topbar links.
execute_process(
    COMMAND git -C "${YETTY_ROOT}" describe --tags --abbrev=0
            --match "yetty-[0-9]*.[0-9]*.[0-9]*" HEAD
    OUTPUT_VARIABLE YETTY_RELEASE_TAG OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _YETTY_TAG_RC
    ERROR_QUIET)
if(NOT _YETTY_TAG_RC EQUAL 0 OR NOT YETTY_RELEASE_TAG)
    message(WARNING "webasm: no yetty-X.Y.Z tag reachable from HEAD — "
                    "topbar release link will point at a placeholder. "
                    "Run `git fetch --tags`.")
    set(YETTY_RELEASE_TAG "yetty-0.0.0")
endif()
configure_file(
    ${YETTY_ROOT}/build-tools/web/index.html
    ${CMAKE_BINARY_DIR}/index.html
    @ONLY)
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY
    CMAKE_CONFIGURE_DEPENDS
    "${YETTY_ROOT}/build-tools/web/index.html")
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
