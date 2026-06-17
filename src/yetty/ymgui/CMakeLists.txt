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
    rpc.gen.c
)

target_include_directories(yetty_ymgui PUBLIC
    ${YETTY_ROOT}/include
)
target_include_directories(yetty_ymgui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    # Codegen-emitted internal sources (*.gen.c) sit under
    # src/yetty/ymgui/. PRIVATE so the path does not leak to
    # consumers — the public API is the per-class headers under
    # include/yetty/ymgui/.
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ymgui PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_yfigure
    yetty_ydraw_core
    yetty_yrender
    yetty_ywire
    webgpu
)

# figure.c carries `[[clang::annotate(...)]]` on its data struct (yclass
# codegen reads it off the AST). The C2x attribute syntax needs C23 to
# parse — host clang/gcc accept it by default, but NDK clang 17 does not,
# so set it explicitly. Off-MSVC only, mirroring ygui: MSVC can't express
# C23 via CMake's C_STANDARD, and the Windows build's global /std:clatest
# already parses the attributes.
if(NOT MSVC)
    set_target_properties(yetty_ymgui PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()

include(${YETTY_ROOT}/build-tools/yetty/libs/imgui.cmake)

if(TARGET imgui_core)
    add_subdirectory(frontend)
else()
    message(STATUS
        "ymgui: imgui_core target not available — frontend skipped.")
endif()
