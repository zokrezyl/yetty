add_library(yetty_yzoo STATIC
    yzoo.c
)

target_include_directories(yetty_yzoo PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_yzoo PUBLIC
    yetty_ydraw_list
    yetty_ysdf
)

if(NOT WIN32)
    target_link_libraries(yetty_yzoo PUBLIC m)
endif()
