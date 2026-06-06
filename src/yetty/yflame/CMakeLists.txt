# yflame — folded stacks → ydraw buffer of flame-graph boxes + MSDF labels.
#
# Pure C; depends on ycore (result/types), ydraw-core (drawable list + text),
# ysdf (box emitters), and yface (OSC envelope emit). GPU-less — it produces
# generic ydraw primitives the canvas already renders, like ydiagram.

add_library(yetty_yflame STATIC
    yflame.c
)

target_include_directories(yetty_yflame PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_yflame PUBLIC
    yetty_ycore
    yetty_ydraw_core
    yetty_ysdf
    yetty_yface
)
