# yhello — ygreeter's UI running standalone directly against yfigures.
# It opens its own WebGPU window and wires ygui to a local yfigure_container
# through yetty_ygui_framework_set_container_obj: no terminal, no PTY, no
# OSC/DCS, no wire state machine.

if(NOT YETTY_ENABLE_LIB_WEBGPU)
    return()
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT WIN32 AND NOT APPLE)
    return()
endif()

set(YHELLO_SOURCES
    main.c
    embedded-assets.c
    ${YETTY_ROOT}/src/yetty/yshadertoy/demo-shaders.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/yworkpool/default.c
    ${YETTY_ROOT}/src/yetty/yconfig/config.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/ynotify/ynotify.c
    # yhello is a yapp:app subclass (class yhello:app in main.c); its dual-mode
    # main() drives the glfw_platform bootstrap directly (no shared ymain entry).
    ${YETTY_APP_BOOTSTRAP_SOURCES}
    ${YETTY_ROOT}/src/yetty/yframework/yframework.c
    ${YETTY_ROOT}/src/yetty/yplatform/os-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu/default.c
)

if(WIN32)
    list(APPEND YHELLO_SOURCES ${YETTY_ROOT}/src/yetty/yplatform/pipe/windows.c)
else()
    list(APPEND YHELLO_SOURCES ${YETTY_ROOT}/src/yetty/yplatform/pipe/default.c)
endif()

add_executable(yhello ${YHELLO_SOURCES})

target_include_directories(yhello BEFORE PRIVATE
    ${YETTY_ROOT}/src
    ${YETTY_ROOT}/include
    # incbin_add_directory emits ${TARGET}_${PREFIX}_manifest.h here;
    # embedded-assets.c includes yhello_data_manifest.h directly.
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_definitions(yhello PRIVATE
    YETTY_WEB=0
    YETTY_ANDROID=0
)
if(WIN32)
    target_compile_definitions(yhello PRIVATE
        YETTY_USE_DIRECTWRITE=1
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

target_compile_definitions(yhello PRIVATE
    YETTY_YHELLO_HAS_CHROME
    YETTY_YHELLO_HAS_STANDALONE
)

find_package(Threads REQUIRED)
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
endif()

target_link_libraries(yhello PRIVATE
    yetty_ygui
    yetty_yplatform_core
    yetty_yco
    yetty_ychrome
    yetty_ycircuit
    yetty_ymusic
    ${BROTLIDEC_LIBRARIES}
    Threads::Threads
)
if(WIN32)
    target_link_libraries(yhello PRIVATE dwrite)
elseif(NOT APPLE)
    target_link_libraries(yhello PRIVATE rt)
endif()

target_link_libraries(yhello PRIVATE
    yetty_yfigure
    yetty_ygrid
    yetty_yshadertoy
    yetty_yfont_core
    yetty_ydraw_factory
    yetty_yimage
    yetty_yplot
    ${YETTY_LIBS}
    yetty_yplatform_move_resize
    yetty_yplatform_window_chrome
    ${FONTCONFIG_LIBRARIES}
)
if(NOT WIN32)
    target_include_directories(yhello PRIVATE ${FONTCONFIG_INCLUDE_DIRS})
endif()

# Dawn (libwebgpu_dawn) is C++ — needs C++ runtime even though our
# sources are C. Only relevant when webgpu is in the link line; harmless
# to keep otherwise (CXX linker driver still works for pure C).
set_target_properties(yhello PROPERTIES LINKER_LANGUAGE CXX)

if(NOT MSVC)
    target_compile_options(yhello PRIVATE -Wall -Wextra)
endif()

# Standalone mode opens a Dawn/D3D12 window — copy the DX shader-compiler
# DLLs next to yhello.exe so it runs from its own build dir.
if(WIN32 AND YETTY_ENABLE_LIB_WEBGPU)
    include(${YETTY_ROOT}/build-tools/yetty/dawn-runtime-dlls.cmake)
    yetty_copy_dawn_runtime_dlls(yhello)
endif()

# Self-contained yhello: embed the assets that drive the Images and
# Video tabs (logo-*.jpeg + yetty-unchained-2.mp4) into the binary.
# Mirrors yetty_embed_assets at a much smaller scale — a single "data/"
# section, extracted on first run into <data_dir>/ via
# yhello_embedded_assets_extract (called early in standalone main).
# The YETTY_ENABLE_LIB_INCBIN guard lets the dev build skip embedding;
# embedded-assets.c becomes a no-op then and the Images tab falls back
# to whatever logo-N.jpeg the user has under <data_dir>/.
if(YETTY_ENABLE_LIB_INCBIN)
    yetty_compute_build_version()
    target_compile_definitions(yhello PRIVATE
        YETTY_BUILD_VERSION="${YETTY_BUILD_VERSION_STR}")

    # Brotli stays linked unconditionally: embedded-assets.c uses it when the
    # data manifest is present, and other deps (freetype/woff2, curl) may pull
    # it in too. Harmless when unused — the linker drops it.
    include(${YETTY_ROOT}/build-tools/yetty/libs/brotli.cmake)
    target_link_libraries(yhello PRIVATE brotlidec brotlicommon)

    # Desktop yhello is thin: the yinstall installer carries its logos /
    # intro video / samples and deploys them to <data_dir>/, so a baked-in
    # copy would only bloat the binary and the installer that embeds it.
    # YETTY_EMBED_ASSETS_IN_BINARIES is forced ON for the riscv VM rootfs
    # (no installer there — the tool must self-extract its assets at boot).
    # When OFF, embedded-assets.c compiles to a no-op and yhello reads the
    # logos straight from <data_dir>/.
    if(YETTY_EMBED_ASSETS_IN_BINARIES)
        include(${YETTY_ROOT}/build-tools/yetty/incbin.cmake)

        set(_YHELLO_EMBED_DIR "${CMAKE_CURRENT_BINARY_DIR}/embed-data")
        file(REMOVE_RECURSE "${_YHELLO_EMBED_DIR}")
        file(MAKE_DIRECTORY "${_YHELLO_EMBED_DIR}")

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
                    "${_YHELLO_EMBED_DIR}/${_NAME}" COPYONLY)
            endif()
        endforeach()

        incbin_add_directory(yhello "data" "${_YHELLO_EMBED_DIR}" "*" TRUE)
    endif()
endif()
