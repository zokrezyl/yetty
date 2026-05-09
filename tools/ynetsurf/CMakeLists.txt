# ynetsurf — minimal NetSurf browser tool emitting ypaint primitives.
#
# Disabled silently if yetty_ynetsurf wasn't built (which itself
# requires the netsurf-all-3.11 tree to be present and built).

if(NOT TARGET yetty_ynetsurf)
    return()
endif()

add_executable(ynetsurf
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)

target_include_directories(ynetsurf PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_NETSURF_ROOT}/netsurf
    ${YETTY_NETSURF_ROOT}/inst-monkey/include
)

target_link_libraries(ynetsurf PRIVATE
    yetty_ynetsurf
    yetty_ypaint_core
    yetty_ysdf
    yetty_yface
    yetty_ycore
)

target_compile_options(ynetsurf PRIVATE -Wall -Wno-unused-parameter)
