# ymgui — Dear ImGui ↔ yetty bridge.
#
# figure.c + pipeline.c — yetty_ymgui_figure: yfigure_figure subclass
#   that renders one Dear-ImGui frame as a compositor figure. This is
#   the new path. The old ymgui-layer in src/yetty/yterm/ stays alive
#   in parallel until the figure path is validated.
#
# frontend/  — client-side library the app links against. Ships an
#              ImGui renderer backend (imgui_impl_yetty.*) and the
#              OSC/encode helpers.
add_library(yetty_ymgui STATIC
    figure.c
    pipeline.c
)

target_include_directories(yetty_ymgui PUBLIC
    ${YETTY_ROOT}/include
)
target_include_directories(yetty_ymgui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(yetty_ymgui PUBLIC
    yetty_ycore
    yetty_yfigure
    yetty_ydraw_core
    yetty_yrender
    yetty_ywire
    webgpu
)

include(${YETTY_ROOT}/build-tools/yetty/libs/imgui.cmake)

if(TARGET imgui_core)
    add_subdirectory(frontend)
else()
    message(STATUS
        "ymgui: imgui_core target not available — frontend skipped.")
endif()
