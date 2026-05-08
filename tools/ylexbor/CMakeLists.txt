# ylexbor-demo — minimal CLI to render HTML through ylexbor + emit OSC.

# if(NOT TARGET yetty_ylexbor)
#    return()
# endif()

add_executable(ylexbor
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/thread.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/term.c
)

target_include_directories(ylexbor PRIVATE
    ${YETTY_ROOT}/include
)

target_link_libraries(ylexbor PRIVATE
    yetty_ylexbor
    yetty_ypaint_core
    yetty_ysdf
    yetty_yface
    yetty_ycore
    CURL::libcurl   # http(s):// fetching for URL args
)

target_compile_options(ylexbor PRIVATE -Wall -Wno-unused-parameter)
