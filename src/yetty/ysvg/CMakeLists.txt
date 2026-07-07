# ysvg — SVG (Tiny 1.2 subset) → ydraw buffer rendering
include(${YETTY_ROOT}/build-tools/yetty/libs/yxml.cmake)

add_library(yetty_ysvg STATIC
    ysvg-parse.c
    ysvg-attrs.c
    ysvg-style.c
    ysvg-css.c
    ysvg-path.c
    ysvg-fill.c
    ysvg-paint.c
    ysvg.c
)

target_include_directories(yetty_ysvg PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ysvg
    PUBLIC
        yetty_ycore
        yetty_ydraw_core
        yetty_ysdf
    PRIVATE
        # <image> elements serialize yimage composite records into the
        # scene (wire-only core lib — no Dawn/GPU dependency).
        yetty_yimage_core
        yxml
)
