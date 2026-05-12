# ygui — pure-C widget library with libuv event loop
# Output: libygui.a

# Core sources — both static and shared variants compile these. No YAML
# parser dep here: keeps libygui.so light enough for FFI consumers.
set(YGUI_SOURCES
    ygui_engine.c
    ygui_grid.c
    ygui_widgets.c
    ygui_rich.c
    ygui_tabbar.c
    ygui_render.c
    ygui_theme.c
    ygui_osc.c
    ygui_layout.c
    ygui_css.c
)

# Static-only sources — the YAML convenience layer for the RICH widget.
# Brought in only by libygui.a so the shared variant can avoid linking
# yetty_ypaint_yaml (which transitively requires fontconfig + libyaml,
# and libfontconfig.a on Linux isn't built with -fPIC).
set(YGUI_STATIC_ONLY_SOURCES
    ygui_rich_yaml.c
)

add_library(ygui STATIC ${YGUI_SOURCES} ${YGUI_STATIC_ONLY_SOURCES})

target_include_directories(ygui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${YETTY_ROOT}/include
)

target_link_libraries(ygui PUBLIC
    yetty_ypaint_core
    yetty_ysdf
    yetty_ycore
    yetty_yface     # OSC envelope + LZ4F + base64 streamer
    yetty_yfont_core  # raster-font-from-file for text measurement
    yetty_ypaint_yaml # rich widget's create_from_yaml -> ypaint_yaml_parse
)

if(NOT EMSCRIPTEN)
    target_link_libraries(ygui PUBLIC uv_a)
endif()

set_target_properties(ygui PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

install(FILES ygui.h DESTINATION include)
install(TARGETS ygui ARCHIVE DESTINATION lib)

# Shared variant for FFI consumers (Python ctypes / cffi, Rust libloading,
# etc.). Built only when YETTY_BUILD_YGUI_SHARED=ON. The yetty binary itself
# never links against this — it always uses the static `ygui` target — so
# turning the option off has zero effect on a normal yetty build.
option(YETTY_BUILD_YGUI_SHARED "Build a libygui.so for FFI consumers" ON)

if(YETTY_BUILD_YGUI_SHARED AND NOT EMSCRIPTEN)
    # term.c / thread.c are platform-specific singletons compiled directly
    # into each parent target rather than living in a standalone CMake
    # library. Pick the right pair so the .so/.dll is self-contained for
    # FFI consumers.
    if(WIN32)
        set(_YGUI_PLATFORM_SOURCES
            ${YETTY_ROOT}/src/yetty/yplatform/term/windows.c
            ${YETTY_ROOT}/src/yetty/yplatform/thread/windows.c
        )
    else()
        set(_YGUI_PLATFORM_SOURCES
            ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
            ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
        )
    endif()

    # Shared variant: no YAML-using sources, no yetty_ypaint_yaml dep.
    # FFI consumers that want YAML can call yetty_ypaint_yaml_parse() in
    # their own toolchain and hand the resulting buffer to
    # yetty_ygui_widget_rich_set_buffer().
    add_library(ygui_shared SHARED
        ${YGUI_SOURCES}
        ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
        ${_YGUI_PLATFORM_SOURCES}
    )

    target_include_directories(ygui_shared PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${YETTY_ROOT}/include
    )

    target_link_libraries(ygui_shared PRIVATE
        yetty_ypaint_core
        yetty_ysdf
        yetty_ycore
        yetty_yface
        yetty_yfont_core
        uv_a
    )

    set_target_properties(ygui_shared PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        OUTPUT_NAME ygui
        # Force PIC on the static deps; -fPIC propagates from the SHARED
        # consumer onto the static archive sources via CMake's POSITION_
        # INDEPENDENT_CODE handling.
        POSITION_INDEPENDENT_CODE ON
    )

    if(WIN32)
        # MSVC names a SHARED library's import library <OUTPUT_NAME>.lib,
        # which collides with the static `ygui` target's archive ygui.lib.
        # Give the DLL's import lib a distinct name; the DLL itself stays
        # ygui.dll for FFI consumers.
        set_target_properties(ygui_shared PROPERTIES
            ARCHIVE_OUTPUT_NAME ygui_import
        )
    endif()

    install(TARGETS ygui_shared LIBRARY DESTINATION lib)
endif()
