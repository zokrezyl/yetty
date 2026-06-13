add_library(yetty_ygrid STATIC
    grid.c
)

target_include_directories(yetty_ygrid PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) sit under src/yetty/ygrid/.
# PRIVATE so the path does not leak to consumers — the public API is the per-class
# headers under include/yetty/ygrid/.
target_include_directories(yetty_ygrid PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_ygrid PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_yfigure
    yetty_ydraw_core
    yetty_yrender
    yetty_yfont_core      # font cache: materialise wire-shipped figure fonts
    yetty_yplatform_core  # cache dir + mkdir/file_exists for the font cache
    webgpu
)

# grid.c carries `[[clang::annotate(...)]]` on its data struct (codegen
# reads it off the AST). The C2x attribute syntax needs C23 to parse —
# host clang/gcc accept it by default, NDK clang 17 does not. Off-MSVC
# only: MSVC can't express C23 via CMake's C_STANDARD and parses the
# attributes via the global /std:clatest. (Mirrors ygui/CMakeLists.txt.)
if(NOT MSVC)
    set_target_properties(yetty_ygrid PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
