# ymgui — Dear ImGui ↔ yetty bridge.
#
# frontend/  — client-side library the app links against. Ships an
#              ImGui renderer backend (imgui_impl_yetty.*) and the
#              OSC/encode helpers.
# (no backend/ subdir — the yetty-side receiver is the ymgui-layer
#  terminal layer in src/yetty/yterm/. A layer, not a side-car lib.)
#
# Dear ImGui is fetched via CPM (build-tools/yetty/libs/imgui.cmake),
# which exposes the lean imgui_core target the frontend links against.
# No tmp/ vendoring.
include(${YETTY_ROOT}/build-tools/yetty/libs/imgui.cmake)

if(TARGET imgui_core)
    add_subdirectory(frontend)
else()
    message(STATUS
        "ymgui: imgui_core target not available — frontend skipped.")
endif()
