# ygui — pure-C widget library.
#
# Split into two libraries following the established XXX-core pattern:
#
#   ygui_core
#     Widget tree, layout, rendering, OSC emit, theme — all libuv-free.
#     Linkable into any target including emscripten / cross-builds where
#     libuv isn't a fit.
#
#   ygui
#     ygui_core + the libuv-driven event-loop integration
#     (ygui_engine_uv.c — yetty_ygui_engine_attach / _run / _poll /
#     _get_loop). Used by standalone interactive tools (ygreeter, ytop).
#     Builds only when libuv is available (NOT EMSCRIPTEN).
#
# Public header (ygui.h) stays unified; the libuv-typed forward decl is
# guarded so consumers don't need <uv.h>.

# ygui_core sources: libuv-free.
set(YGUI_CORE_SOURCES
    ygui_engine.c
    ygui_grid.c
    ygui_widgets.c
    ygui_rich.c
    ygui_tabbar.c
    ygui_window.c
    ygui_popup_menu.c
    ygui_render.c
    ygui_theme.c
    ygui_osc.c
    ygui_layout.c
    ygui_css.c
    ygui_notify.c
)

# Static-only YAML convenience layer for the RICH widget — same gating
# as before: brings in yetty_ypaint_yaml (fontconfig + libyaml), which
# the shared FFI variant deliberately omits.
set(YGUI_STATIC_ONLY_SOURCES
    ygui_rich_yaml.c
)

# ---------------------------------------------------------------------
# ygui_core — libuv-free widget runtime.
# ---------------------------------------------------------------------

add_library(ygui_core STATIC ${YGUI_CORE_SOURCES} ${YGUI_STATIC_ONLY_SOURCES})

target_include_directories(ygui_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${YETTY_ROOT}/include
)

target_link_libraries(ygui_core PUBLIC
    yetty_ypaint_core
    yetty_ysdf
    yetty_ycore
    yetty_yface     # OSC envelope + LZ4F + base64 streamer
    yetty_yfont_core  # raster-font-from-file for text measurement
    yetty_ypaint_yaml # rich widget's create_from_yaml -> ypaint_yaml_parse
)

set_target_properties(ygui_core PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# ---------------------------------------------------------------------
# ygui — libuv-coupled full runtime. Only built when libuv is available
# (NOT EMSCRIPTEN). Standalone tools that drive their own event loop
# (ygreeter, ytop) link this; webasm consumers link ygui_core directly.
# ---------------------------------------------------------------------

if(NOT EMSCRIPTEN)
    add_library(ygui STATIC ygui_engine_uv.c)

    target_include_directories(ygui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${YETTY_ROOT}/include
    )

    target_link_libraries(ygui PUBLIC
        ygui_core
        uv_a
    )

    set_target_properties(ygui PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
    )

    install(TARGETS ygui ARCHIVE DESTINATION lib)
endif()

install(FILES ygui.h DESTINATION include)
install(TARGETS ygui_core ARCHIVE DESTINATION lib)

# ---------------------------------------------------------------------
# Shared variant for FFI consumers. Standalone .so/.dll built from the
# same core sources + ygui_engine_uv.c (FFI consumers want the full
# runtime). Not produced for emscripten.
# ---------------------------------------------------------------------

option(YETTY_BUILD_YGUI_SHARED "Build a libygui.so for FFI consumers" ON)

if(YETTY_BUILD_YGUI_SHARED AND NOT EMSCRIPTEN)
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

    add_library(ygui_shared SHARED
        ${YGUI_CORE_SOURCES}
        ygui_engine_uv.c
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
        POSITION_INDEPENDENT_CODE ON
    )

    if(WIN32)
        set_target_properties(ygui_shared PROPERTIES
            ARCHIVE_OUTPUT_NAME ygui_import
        )
    endif()

    install(TARGETS ygui_shared LIBRARY DESTINATION lib)
endif()
