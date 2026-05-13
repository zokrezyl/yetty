# ysvg — SVG (Tiny 1.2 subset) → ypaint buffer rendering
include(${YETTY_ROOT}/build-tools/yetty/libs/yxml.cmake)

add_library(yetty_ysvg STATIC
    ysvg-parse.c
    ysvg-attrs.c
    ysvg-style.c
    ysvg-css.c
    ysvg-path.c
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
        yetty_ypaint_core
        yetty_ysdf
    PRIVATE
        yxml
)
