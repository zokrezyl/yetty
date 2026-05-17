# ymarkdown - Markdown → ydraw buffer rendering
add_library(yetty_ymarkdown STATIC
    ymarkdown.c
)

target_include_directories(yetty_ymarkdown PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ymarkdown PUBLIC
    yetty_ycore
    yetty_ydraw_core
    yetty_ysdf
)
