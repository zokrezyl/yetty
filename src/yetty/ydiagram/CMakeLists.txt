# ydiagram — Mermaid (and later DOT) → ydraw buffer of MSD/MSDF primitives.
#
# Pure C; depends on ycore (result/types), ydraw-list (buffer + text/font
# drawable-list entries), and ysdf (SDF shape emitters). No platform-specific deps.

add_library(yetty_ydiagram STATIC
    graph-ir.c
    mermaid-parser.c
    diagram-detect.c
    state-parser.c
    class-parser.c
    er-parser.c
    sequence.c
    sugiyama-layout.c
    renderer.c
    ydiagram.c
)

target_include_directories(yetty_ydiagram PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ydiagram PUBLIC
    yetty_ycore
    yetty_ydraw_list
    yetty_ysdf
)
