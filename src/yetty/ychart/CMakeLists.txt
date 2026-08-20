# ychart — data (CSV / JSON / YAML) → ydraw buffer of MSD/MSDF primitives
# for bar / column / line / area / scatter / pie / donut / radar / treemap /
# sankey charts.
#
# Pure C; depends on ycore (result/types), ydraw-list (buffer + text/font
# drawable-list entries), and ysdf (SDF shape emitters). No platform-specific
# deps — mirrors ydiagram.

add_library(yetty_ychart STATIC
    chart-ir.c
    data-detect.c
    csv-parser.c
    json-parser.c
    yaml-parser.c
    render-common.c
    renderer-cartesian.c
    renderer-polar.c
    renderer-hier.c
    ychart.c
)

target_include_directories(yetty_ychart PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ychart PUBLIC
    yetty_ycore
    yetty_ydraw_list
    yetty_ysdf
)

if(NOT MSVC)
    target_link_libraries(yetty_ychart PUBLIC m)
endif()
