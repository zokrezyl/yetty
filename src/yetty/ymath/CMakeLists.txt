# ymath — TeX-subset math rendering to ydraw drawables. GPU-less, same
# producer shape as ychart/ydiagram.
add_library(yetty_ymath STATIC
    math.c
)

target_include_directories(yetty_ymath PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ymath PUBLIC
    yetty_ycore
    yetty_ydraw_list
    yetty_yface
    yetty_ysdf
)

if(NOT WIN32)
    target_link_libraries(yetty_ymath PUBLIC m)
endif()
