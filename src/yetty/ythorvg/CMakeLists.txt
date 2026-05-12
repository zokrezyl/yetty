# yetty_ythorvg — ThorVG SVG/Lottie -> ypaint SDF bridge.
#
# C++ internally (uses ThorVG's C++ RenderMethod interface) but exposes a
# C API via include/yetty/ythorvg/ythorvg.h so the rest of the C codebase
# can drive it.

add_library(yetty_ythorvg STATIC
    ydraw-render-method.cpp
    ythorvg.cpp
)

target_include_directories(yetty_ythorvg PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# ThorVG internal headers (RenderMethod, PAINT() macro). The 3rdparty
# tarball ships them under include-internal/ preserving thorvg's upstream
# src/<subdir>/ layout — see build-tools/yetty/thorvg.cmake.
target_include_directories(yetty_ythorvg PRIVATE
    ${YETTY_3RDPARTY_thorvg_INTERNAL_INCLUDE_DIR}/renderer
    ${YETTY_3RDPARTY_thorvg_INTERNAL_INCLUDE_DIR}/common
)

# Windows: prevent min/max macros from windows.h conflicting with std::min/max.
target_compile_definitions(yetty_ythorvg PRIVATE
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
)

set_target_properties(yetty_ythorvg PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_link_libraries(yetty_ythorvg PUBLIC
    yetty_ypaint_core
    yetty_ysdf
    thorvg_lib
)
