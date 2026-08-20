# ygreeter — first-contact tool. IN-TERMINAL ONLY on desktop:
# CLIENT mode ships OSC envelopes over stdout to the host yetty
# (libuv-driven, no GPU, no window). Outside a hosting yetty the binary
# prints an error and exits — the desktop build carries NO WebGPU/Dawn/GLFW.
# main.c still contains the YETTY_YGREETER_HAS_STANDALONE-guarded standalone
# host — it is compiled ONLY by the Android/web packagings, never here.

# Builds on Linux and macOS (shared POSIX forkpty / termios path). Windows
# only had the standalone GPU mode — client mode's termios/ioctl/SIGWINCH/
# uv-stdin-poll glue isn't ported — so it is not built there.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT APPLE)
    return()
endif()

# Sources shared by both modes — libuv pieces, pty plumbing, config,
# trace, notification, asset extraction. None of these pull in webgpu
# or glfw, so they build fine on the riscv cross.
set(YGREETER_SOURCES
    main.c
    embedded-assets.c
    # Shared Shadertoy gallery (Shadertoy tab). Plain string data, no GPU
    # deps — compiled in directly so the client/cross build (which omits
    # the webgpu-bound yetty_yshadertoy lib) still has the table.
    ${YETTY_ROOT}/src/yetty/yshadertoy/demo-shaders.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/coroutine/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/yworkpool/default.c
    ${YETTY_ROOT}/src/yetty/ypty/memory-pty.c
    ${YETTY_ROOT}/src/yetty/yconfig/config.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/ynotify/ynotify.c
)
# POSIX pty + pipe backends (client mode runs the wire over stdin/stdout;
# forkpty.c is self-contained, no external refs).
list(APPEND YGREETER_SOURCES
    ${YETTY_ROOT}/src/yetty/ypty/forkpty.c
    ${YETTY_ROOT}/src/yetty/yplatform/pipe/default.c
)
# ygreeter ships its OWN incbin manifest (logos + the demo video) via
# embedded-assets.c.

add_executable(ygreeter ${YGREETER_SOURCES})

target_include_directories(ygreeter BEFORE PRIVATE
    ${YETTY_ROOT}/src
    ${YETTY_ROOT}/include
    # incbin_add_directory emits ${TARGET}_${PREFIX}_manifest.h here;
    # embedded-assets.c includes ygreeter_data_manifest.h directly.
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_definitions(ygreeter PRIVATE
    YETTY_WEB=0
    YETTY_ANDROID=0
    YETTY_USE_FORKPTY=1
    YETTY_YGUI_HAS_UV
)
# Window chrome (ychrome) in the CLIENT/wire flavor — GPU-free: it emits the
# backdrop + caption as figure records the hosting yetty renders. Without
# this, the greeter drew its widgets with no opaque backdrop and the host's
# console text showed through underneath.
target_compile_definitions(ygreeter PRIVATE YETTY_YGREETER_HAS_CHROME)

find_package(Threads REQUIRED)

# Client-mode link line — what's needed to build the OSC-emitting
# binary that runs inside a hosting yetty. Pulls in ygui (which
# transitively brings yfigure, the *_core helpers, ydraw-core,
# ysdf, etc.), the wire SM, libuv glue, coroutines, telnet, and
# the system libs. None of these reach for wgpu functions, so they
# all link on the riscv64 cross (with the headers-only webgpu stub
# defined in build-tools/yetty/platform/shared.cmake).
target_link_libraries(ygreeter PRIVATE
    yetty_ygui
    yetty_ywire
    yetty_yplatform_core
    yetty_yco
    yetty_telnet
    # Window chrome — the WIRE-ONLY flavor (backdrop + caption emitted as
    # figure records, no yscene/GPU local sink even on WebGPU-enabled trees).
    # The window_chrome gesture symbols it references resolve via
    # yetty_yplatform_core above (NULL object in client mode → the stubs
    # early-return).
    yetty_ychrome_wire
    # ycircuit — GPU-free schematic renderer (SDF prims + MSDF text → a ydraw
    # drawable list). The Circuit scene hosts it in a generic ydraw_embed
    # widget, so it links on every target (client guest included), same as
    # ychrome above.
    yetty_ycircuit
    # ymusic — GPU-free score engraver (same yclass → ydraw drawable list
    # pattern as ycircuit). The Music scene hosts it in an ydraw_embed widget.
    yetty_ymusic
    uv_a
    ${BROTLIDEC_LIBRARIES}
    Threads::Threads
)
# Platform-specific link bits:
#   macOS    — util for openpty/forkpty; rt symbols live in libc/libSystem
#              so no -lrt (same pattern as tinyemu.cmake).
#   Linux    — rt for clock/shm/timer (glibc splits those out), util for
#              openpty/forkpty/login_tty.
if(APPLE)
    target_link_libraries(ygreeter PRIVATE util)
else()
    target_link_libraries(ygreeter PRIVATE rt util)
endif()

if(NOT MSVC)
    target_compile_options(ygreeter PRIVATE -Wall -Wextra)
endif()

# Self-contained ygreeter: embed the assets that drive the Images and
# Video tabs (logo-*.jpeg + yetty-unchained-2.mp4) into the binary.
# Mirrors yetty_embed_assets at a much smaller scale — a single "data/"
# section, extracted on first run into <data_dir>/ via
# ygreeter_embedded_assets_extract (called early in standalone main).
# The YETTY_ENABLE_LIB_INCBIN guard lets the dev build skip embedding;
# embedded-assets.c becomes a no-op then and the Images tab falls back
# to whatever logo-N.jpeg the user has under <data_dir>/.
if(YETTY_ENABLE_LIB_INCBIN)
    yetty_compute_build_version()
    target_compile_definitions(ygreeter PRIVATE
        YETTY_BUILD_VERSION="${YETTY_BUILD_VERSION_STR}")

    # Brotli stays linked unconditionally: embedded-assets.c uses it when the
    # data manifest is present, and other deps (freetype/woff2, curl) may pull
    # it in too. Harmless when unused — the linker drops it.
    include(${YETTY_ROOT}/build-tools/yetty/libs/brotli.cmake)
    target_link_libraries(ygreeter PRIVATE brotlidec brotlicommon)

    # Desktop ygreeter is thin: the yinstall installer carries its logos /
    # intro video / samples and deploys them to <data_dir>/, so a baked-in
    # copy would only bloat the binary and the installer that embeds it.
    # YETTY_EMBED_ASSETS_IN_BINARIES is forced ON for the riscv VM rootfs
    # (no installer there — the tool must self-extract its assets at boot).
    # When OFF, embedded-assets.c compiles to a no-op and ygreeter reads the
    # logos straight from <data_dir>/.
    if(YETTY_EMBED_ASSETS_IN_BINARIES)
        include(${YETTY_ROOT}/build-tools/yetty/incbin.cmake)

        set(_YGREETER_EMBED_DIR "${CMAKE_CURRENT_BINARY_DIR}/embed-data")
        file(REMOVE_RECURSE "${_YGREETER_EMBED_DIR}")
        file(MAKE_DIRECTORY "${_YGREETER_EMBED_DIR}")

        foreach(_F
                "assets/logo-1.jpeg"
                "assets/logo-2.jpeg"
                "assets/logo-3.jpeg"
                "assets/logo-4.jpeg"
                "assets/yetty-unchained-2.mp4"
                "test/ut/ypdf/pdf-sample.pdf"
                "README.md")
            if(EXISTS "${YETTY_ROOT}/${_F}")
                get_filename_component(_NAME "${_F}" NAME)
                configure_file("${YETTY_ROOT}/${_F}"
                    "${_YGREETER_EMBED_DIR}/${_NAME}" COPYONLY)
            endif()
        endforeach()

        incbin_add_directory(ygreeter "data" "${_YGREETER_EMBED_DIR}" "*" TRUE)
    endif()
endif()
