# ygui — pure-C widget library with libuv event loop
# Output: libygui.a

set(YGUI_SOURCES
    ygui_engine.c
    ygui_grid.c
    ygui_widgets.c
    ygui_render.c
    ygui_theme.c
    ygui_osc.c
    ygui_layout.c
    ygui_css.c
)

add_library(ygui STATIC ${YGUI_SOURCES})

target_include_directories(ygui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${YETTY_ROOT}/include
)

target_link_libraries(ygui PUBLIC
    yetty_ypaint_core
    yetty_ysdf
    yetty_ycore
    yetty_yface     # OSC envelope + LZ4F + base64 streamer
    yetty_yfont     # raster-font-from-file for text measurement
    yetty_yrender   # transitive: raster-font's shader_code_set
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
    # ytrace and yplatform/shared are just single-file pieces compiled into
    # each parent target, not standalone CMake libraries — pull them in by
    # path so the .so is fully self-contained for FFI consumers.
    add_library(ygui_shared SHARED
        ${YGUI_SOURCES}
        ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
        ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
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
        yetty_yfont
        yetty_yrender
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

    install(TARGETS ygui_shared LIBRARY DESTINATION lib)
endif()
