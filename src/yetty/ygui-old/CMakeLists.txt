# ygui — pure-C widget library.
#
# Split into two libraries following the established XXX-core pattern:
#
#   ygui_old_core
#     Widget tree, layout, rendering, OSC emit, theme — all libuv-free.
#     Linkable into any target including emscripten / cross-builds where
#     libuv isn't a fit.
#
#   ygui
#     ygui_old_core + the libuv-driven event-loop integration
#     (ygui_engine_uv.c — yetty_ygui_old_engine_attach / _run / _poll /
#     _get_loop). Used by standalone interactive tools (ygreeter, ytop).
#     Builds only when libuv is available (NOT EMSCRIPTEN).
#
# Public header (ygui.h) stays unified; the libuv-typed forward decl is
# guarded so consumers don't need <uv.h>.

# ygui_old_core sources: libuv-free.
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
    ygui_yimage.c
    ygui_yplot.c
    ygui_yvideo.c
)

# Static-only YAML convenience layer for the RICH widget — same gating
# as before: brings in yetty_ydraw_yaml (fontconfig + libyaml), which
# the shared FFI variant deliberately omits.
set(YGUI_STATIC_ONLY_SOURCES
    ygui_rich_yaml.c
)

# ---------------------------------------------------------------------
# ygui_old_core — libuv-free widget runtime.
# ---------------------------------------------------------------------

add_library(ygui_old_core STATIC ${YGUI_CORE_SOURCES} ${YGUI_STATIC_ONLY_SOURCES})

target_include_directories(ygui_old_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${YETTY_ROOT}/include
)

target_link_libraries(ygui_old_core PUBLIC
    yetty_ydraw_core
    yetty_ysdf
    yetty_ycore
    yetty_yface     # OSC envelope + LZ4F + base64 streamer
    yetty_yfont_core  # raster-font-from-file for text measurement
    yetty_ydraw_yaml # rich widget's create_from_yaml -> ydraw_yaml_parse
    yetty_yplatform_core # ytime monotonic clock for notification TTL
    yetty_yimage_core # ygui_yimage widget — stb_image decode + yimage prim
    yetty_yplot_core  # ygui_yplot widget — yplot prim serializer
    yetty_yvideo_core # ygui_yvideo widget — yvideo prim serializer (+ MP4 helper)
)

set_target_properties(ygui_old_core PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# ---------------------------------------------------------------------
# ygui — libuv-coupled full runtime. Only built when libuv is available
# (NOT EMSCRIPTEN). Standalone tools that drive their own event loop
# (ygreeter, ytop) link this; webasm consumers link ygui_old_core directly.
# ---------------------------------------------------------------------

if(NOT EMSCRIPTEN)
    add_library(ygui_old STATIC ygui_engine_uv.c)

    target_include_directories(ygui_old PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${YETTY_ROOT}/include
    )

    target_link_libraries(ygui_old PUBLIC
        ygui_old_core
        uv_a
    )

    set_target_properties(ygui_old PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
    )

    install(TARGETS ygui_old ARCHIVE DESTINATION lib)
endif()

install(FILES ygui.h DESTINATION include)
install(TARGETS ygui_old_core ARCHIVE DESTINATION lib)

# ---------------------------------------------------------------------
# Per-producer widget libs.
#
# Each widget below wraps one producer module (ypdf, ymarkdown,
# ybrowser, yjungle, yzoo) and exposes it as a ygui widget by
# building a draw_list at construction time and handing it to the
# generic RICH widget. Each lives in its own static library so callers
# only link the producer dependencies they actually use; ygui_old_core stays
# free of pdfio / lexbor / libcurl / etc.
#
# Two of the wrappers (yjungle, yzoo) share a small flatten helper that
# turns the producer's command stream (CMD_ZERO + CMD_GROUPs) into a
# flat paint list compatible with the RICH widget's render path.
# ---------------------------------------------------------------------

# Stage a single asset file under a per-library embed dir and feed it
# through the shared incbin_add_directory pipeline. Same mechanism the
# main yetty target uses for its shaders / fonts / images and ygreeter
# uses for its data assets — so it's portable across every backend
# (gcc/clang via .incbin, MSVC via RCDATA). The asset rides inside the
# .a/.lib; consumers calling yetty_ygui_old_engine_<NAME>_default() get
# bytes without shipping the file alongside the binary.
function(ygui_embed_default_asset LIB_TARGET NAME ASSET_PATH)
    if(NOT EXISTS "${ASSET_PATH}")
        message(FATAL_ERROR
            "ygui_embed_default_asset(${NAME}): asset not found at "
            "${ASSET_PATH}")
    endif()
    set(_EMBED_DIR "${CMAKE_CURRENT_BINARY_DIR}/${LIB_TARGET}-embed-default")
    file(REMOVE_RECURSE "${_EMBED_DIR}")
    file(MAKE_DIRECTORY "${_EMBED_DIR}")
    get_filename_component(_NAME "${ASSET_PATH}" NAME)
    configure_file("${ASSET_PATH}" "${_EMBED_DIR}/${_NAME}" COPYONLY)
    # COMPRESS=FALSE: assets are tiny (≤16 kB) — brotli framing would
    # cost more than it saves and would force every widget lib to link
    # brotlidec for the runtime decode. The generated manifest exposes
    # register_<NAME>_default_assets_c(cb) which we call once at first
    # _default() invocation to cache the pointer.
    incbin_add_directory(${LIB_TARGET} "${NAME}_default" "${_EMBED_DIR}" "*" FALSE)
    # The manifest header lands in CMAKE_CURRENT_BINARY_DIR (per
    # incbin.cmake convention). Add it to the lib's include path so the
    # widget's .c can `#include "<libtarget>_<name>_default_manifest.h"`.
    target_include_directories(${LIB_TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endfunction()

if(YETTY_ENABLE_FEATURE_YMARKDOWN)
    add_library(ygui_old_ymarkdown STATIC ygui_ymarkdown.c)
    target_include_directories(ygui_old_ymarkdown PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${YETTY_ROOT}/include
    )
    target_link_libraries(ygui_old_ymarkdown PUBLIC ygui_old_core yetty_ymarkdown)
    set_target_properties(ygui_old_ymarkdown PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    # Bundle the repo README as the markdown widget's default sample.
    ygui_embed_default_asset(ygui_old_ymarkdown ymarkdown "${YETTY_ROOT}/README.md")
    install(TARGETS ygui_old_ymarkdown ARCHIVE DESTINATION lib)
endif()

if(YETTY_ENABLE_FEATURE_YPDF)
    add_library(ygui_old_ypdf STATIC ygui_ypdf.c)
    target_include_directories(ygui_old_ypdf PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${YETTY_ROOT}/include
    )
    target_link_libraries(ygui_old_ypdf PUBLIC ygui_old_core yetty_ypdf)
    set_target_properties(ygui_old_ypdf PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    ygui_embed_default_asset(ygui_old_ypdf ypdf
        "${YETTY_ROOT}/test/ut/ypdf/test-comprehensive.pdf")
    install(TARGETS ygui_old_ypdf ARCHIVE DESTINATION lib)
endif()

if(YETTY_ENABLE_FEATURE_YBROWSER)
    add_library(ygui_old_ybrowser STATIC ygui_ybrowser.c)
    target_include_directories(ygui_old_ybrowser PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${YETTY_ROOT}/include
    )
    target_link_libraries(ygui_old_ybrowser PUBLIC ygui_old_core yetty_ybrowser)
    set_target_properties(ygui_old_ybrowser PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    ygui_embed_default_asset(ygui_old_ybrowser ybrowser
        "${YETTY_ROOT}/demo/ygui/26_ybrowser/sample.html")
    install(TARGETS ygui_old_ybrowser ARCHIVE DESTINATION lib)
endif()

if(YETTY_ENABLE_FEATURE_YJUNGLE)
    add_library(ygui_old_yjungle STATIC ygui_yjungle.c ygui_flatten.c)
    target_include_directories(ygui_old_yjungle PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${YETTY_ROOT}/include
    )
    target_link_libraries(ygui_old_yjungle PUBLIC ygui_old_core yetty_yjungle)
    set_target_properties(ygui_old_yjungle PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    install(TARGETS ygui_old_yjungle ARCHIVE DESTINATION lib)
endif()

if(YETTY_ENABLE_FEATURE_YZOO)
    add_library(ygui_old_yzoo STATIC ygui_yzoo.c ygui_flatten.c)
    target_include_directories(ygui_old_yzoo PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${YETTY_ROOT}/include
    )
    target_link_libraries(ygui_old_yzoo PUBLIC ygui_old_core yetty_yzoo)
    set_target_properties(ygui_old_yzoo PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    install(TARGETS ygui_old_yzoo ARCHIVE DESTINATION lib)
endif()

install(FILES
    ${YETTY_ROOT}/include/yetty/ygui-old/ygui_ypdf.h
    ${YETTY_ROOT}/include/yetty/ygui-old/ygui_ybrowser.h
    ${YETTY_ROOT}/include/yetty/ygui-old/ygui_ymarkdown.h
    ${YETTY_ROOT}/include/yetty/ygui-old/ygui_yimage.h
    ${YETTY_ROOT}/include/yetty/ygui-old/ygui_yplot.h
    ${YETTY_ROOT}/include/yetty/ygui-old/ygui_yvideo.h
    ${YETTY_ROOT}/include/yetty/ygui-old/ygui_yjungle.h
    ${YETTY_ROOT}/include/yetty/ygui-old/ygui_yzoo.h
    DESTINATION include/yetty/ygui
)

# ---------------------------------------------------------------------
# Shared variant for FFI consumers. Standalone .so/.dll built from the
# same core sources + ygui_engine_uv.c (FFI consumers want the full
# runtime). Not produced for emscripten.
# ---------------------------------------------------------------------

option(YETTY_BUILD_YGUI_OLD_SHARED "Build a libygui_old.so for FFI consumers" ON)

if(YETTY_BUILD_YGUI_OLD_SHARED AND NOT EMSCRIPTEN)
    if(WIN32)
        set(_YGUI_PLATFORM_SOURCES
            ${YETTY_ROOT}/src/yetty/yplatform/fs/windows.c
            ${YETTY_ROOT}/src/yetty/yplatform/term/windows.c
            ${YETTY_ROOT}/src/yetty/yplatform/thread/windows.c
            ${YETTY_ROOT}/src/yetty/yplatform/time/windows.c
        )
    else()
        set(_YGUI_PLATFORM_SOURCES
            ${YETTY_ROOT}/src/yetty/yplatform/fs/default.c
            ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
            ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
            ${YETTY_ROOT}/src/yetty/yplatform/time/default.c
        )
    endif()

    add_library(ygui_old_shared_target SHARED
        ${YGUI_CORE_SOURCES}
        ygui_engine_uv.c
        ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
        ${_YGUI_PLATFORM_SOURCES}
    )

    target_include_directories(ygui_old_shared_target PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${YETTY_ROOT}/include
    )

    target_link_libraries(ygui_old_shared_target PRIVATE
        yetty_ydraw_core
        yetty_ysdf
        yetty_ycore
        yetty_yface
        yetty_yfont_core
        yetty_yimage_core
        yetty_yplot_core
        yetty_yvideo_core
        uv_a
    )

    set_target_properties(ygui_old_shared_target PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        OUTPUT_NAME ygui_old
        POSITION_INDEPENDENT_CODE ON
    )

    if(WIN32)
        set_target_properties(ygui_old_shared_target PROPERTIES
            ARCHIVE_OUTPUT_NAME ygui_old_import
        )
    endif()

    install(TARGETS ygui_old_shared_target LIBRARY DESTINATION lib)
endif()
