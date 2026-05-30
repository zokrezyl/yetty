add_library(yetty_yfigure STATIC
    container.c
    figure.c
    registry.c
    methods.gen.c
    rpc.gen.c
)

target_include_directories(yetty_yfigure PUBLIC
    ${YETTY_ROOT}/include
)
# Internal codegen headers (methods.gen.h) sit under src/yetty/yfigure/.
# PRIVATE so the path does not leak to consumers — they go through
# the public include/yetty/yfigure/methods.h.
target_include_directories(yetty_yfigure PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yfigure PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_ywire
    # <yetty/yetty/yetty.h> (included transitively by registry.h and
    # container.c) uses WGPU* types in struct declarations. Pull in the
    # webgpu target so the include path resolves. The real dawn lib
    # only enters the link line when YETTY_ENABLE_LIB_WEBGPU=ON;
    # otherwise this is a headers-only stub from yrdawn.
    webgpu
)

# container.c / figure.c carry `[[clang::annotate(...)]]` on their data
# structs (codegen reads them off the AST). The C2x attribute syntax
# needs C23 to parse — host clang/gcc accept it by default, NDK clang 17
# does not. Off-MSVC only: MSVC can't express C23 via CMake's
# C_STANDARD and parses the attributes via the global /std:clatest.
# (Mirrors src/yetty/ygui/CMakeLists.txt.)
if(NOT MSVC)
    set_target_properties(yetty_yfigure PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
