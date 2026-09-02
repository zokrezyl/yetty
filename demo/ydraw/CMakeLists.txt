# ydraw demos — C clients of the v2 drawable interface (ydrawlist2 /
# ysdf2 / ycomplex2 / api_yplot). GPU-free: they emit DCS envelopes on
# stdout for the enclosing yetty to render.

add_executable(demo-ydraw-stress-bomb
    stress-bomb.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(demo-ydraw-stress-bomb PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(demo-ydraw-stress-bomb PRIVATE
    yetty_ysdf2
    yetty_api_ysdf2
    yetty_ycomplex2
    yetty_api_ycomplex2
    yetty_api_yplot
    yetty_ydrawlist2
    yetty_api_ydrawlist2
    yetty_ydraw_list
    yetty_yclass
    yetty_ycore
    yetty_yplatform_core
    Threads::Threads
)

target_compile_features(demo-ydraw-stress-bomb PRIVATE c_std_11)

if(NOT WIN32)
    target_link_libraries(demo-ydraw-stress-bomb PRIVATE m)
endif()
