add_library(yetty_ygrid STATIC
    grid.c
    methods.gen.c
    rpc.gen.c
)

target_include_directories(yetty_ygrid PUBLIC
    ${YETTY_ROOT}/include
)
# Internal codegen headers (methods.gen.h) sit under src/yetty/ygrid/.
# PRIVATE so the path does not leak to consumers — they go through
# the public include/yetty/ygrid/methods.h.
target_include_directories(yetty_ygrid PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_ygrid PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_yfigure
    yetty_ydraw_core
    yetty_yrender
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
