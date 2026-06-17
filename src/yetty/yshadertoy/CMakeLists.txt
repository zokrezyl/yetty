# yshadertoy — standalone Shadertoy-style shader figure.
#
# A yfigure:figure subclass that paints its whole rect with a full-screen
# quad running a user-supplied WGSL mainImage(), injecting the usual
# Shadertoy inputs (iResolution/iTime/iTimeDelta/iFrame/iMouse) as
# uniforms. Decoupled from the terminal text grid — the shader text is an
# argument. Hosts (yetty_yterm) link this to register the
# YETTY_YFIGURE_KIND_YSHADERTOY factory; the ygui yshadertoy widget mints
# figures of that kind over the wire.
#
# the per-class <stem>.gen.c (e.g. figure.gen.c) are produced by `make codegen`
# from figure.c's [[clang::annotate(...)]] and committed to git. Platform
# builds compile what is in the tree.

add_library(yetty_yshadertoy STATIC
    figure.c
    rpc.gen.c
)

target_include_directories(yetty_yshadertoy PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) sit under src/.
target_include_directories(yetty_yshadertoy PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yshadertoy PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_yfigure
    yetty_yframework
    yetty_yrender
    yetty_yevent
    yetty_ywire
    webgpu
)

# figure.c carries `[[clang::annotate(...)]]` on its data struct (codegen
# reads it off the AST). The C2x attribute syntax needs C23 to parse —
# host clang/gcc accept it by default, NDK clang 17 does not. Off-MSVC
# only: MSVC can't express C23 via CMake's C_STANDARD and parses the
# attributes via the global /std:clatest. (Mirrors src/yetty/yfigure.)
if(NOT MSVC)
    set_target_properties(yetty_yshadertoy PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()

#=============================================================================
# yshadertoy prim — the low-level composite-prim sibling of the figure.
# Rides the YDRAW_BIN drawable-list channel (cursor-anchored, scrolls with
# text) instead of the figure-tree compositor wire. Two-tier build, same
# split as yvideo:
#
#   yetty_yshadertoy_core — wire serializer + drawable-list builder.
#                            CPU-only, no Dawn. Linked by client tools (ycat).
#   yetty_yshadertoy_prim — receiving concrete factory (per-instance WGSL
#                            compile + render). Linked by yetty_ydraw.
#=============================================================================

add_library(yetty_yshadertoy_core STATIC
    prim-wire.c
)

target_include_directories(yetty_yshadertoy_core PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yshadertoy_core PUBLIC
    yetty_ycore
    yetty_ydraw_core
)

add_library(yetty_yshadertoy_prim STATIC
    prim.c
)

target_include_directories(yetty_yshadertoy_prim PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yshadertoy_prim PUBLIC
    yetty_yshadertoy_core
    yetty_ydraw_factory
    yetty_yrender
    yetty_yevent
    yetty_yplatform_core
    webgpu
)
