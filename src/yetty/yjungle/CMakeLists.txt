add_library(yetty_yjungle STATIC
    yjungle.c
)

target_include_directories(yetty_yjungle PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_yjungle PUBLIC
    yetty_ydraw_core
    yetty_ysdf
)

if(NOT WIN32)
    target_link_libraries(yetty_yjungle PUBLIC m)
endif()
