# yfigure — the figure base class + the compositor container. Migrated to
# the role-split codegen layout; build ownership follows the generated-
# source ownership:
#
#   yetty_api_yfigure       src/yetty/gen/api/yfigure/{figure,container}.c
#                           — the object API (typed stubs). What a pure
#                           wire producer needs; no strong references to
#                           the implementation, accessors, skels or
#                           registration. Headers:
#                           <yetty/api/yfigure/{figure,container}.h>.
#
#   yetty_yfigure           container.c figure.c registry.c — the
#                           implementation (compositor). The hand-written
#                           sources #include their generated impl glue
#                           (src/yetty/gen/impl/yfigure/*.c: accessors,
#                           shims, skels, factories, registration hooks).
#                           Links the API lib (skels re-enter the stubs).
#                           A wire client needs only yetty_api_yfigure plus
#                           the yclass connect runtime (yetty_yclass) and,
#                           to navigate to the container, yetty_api_yterminal.
#
# yfigure has NO compat header: its subclassing modules (ygrid, yrdawn,
# yshadertoy, ymgui, yvterm) are all migrated and reach the parent through
# src/yetty/gen/impl/yfigure/figure.h. Consumers of the object API include
# include/yetty/api/yfigure/{figure,container}.h.

add_library(yetty_api_yfigure STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/yfigure/figure.c
    ${YETTY_ROOT}/src/yetty/gen/api/yfigure/container.c
)

target_include_directories(yetty_api_yfigure PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_api_yfigure PUBLIC
    yetty_ycore
    yetty_yclass
)

add_library(yetty_yfigure STATIC
    container.c
    figure.c
    registry.c
)

target_include_directories(yetty_yfigure PUBLIC
    ${YETTY_ROOT}/include
)
# The hand-written sources #include the generated impl glue via
# "yetty/gen/impl/yfigure/...".
target_include_directories(yetty_yfigure PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yfigure PUBLIC
    yetty_api_yfigure
    yetty_ycore
    yetty_yclass
    yetty_ywire
    # container.c consumes the YCOMPOSITOR_BIN yface envelope wire format
    # (the emit side lives with the producers). yface only depends on
    # ycore + ywire, so no cycle.
    yetty_yface
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
