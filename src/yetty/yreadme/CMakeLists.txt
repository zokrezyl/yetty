# yreadme — file-and-buffer-fronted README rendering on top of ymarkdown.
add_library(yetty_yreadme STATIC
    yreadme.c
)

target_include_directories(yetty_yreadme PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_yreadme PUBLIC
    yetty_ycore
    yetty_ydraw_core
    yetty_ymarkdown
)
