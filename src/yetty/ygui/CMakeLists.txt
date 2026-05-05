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
