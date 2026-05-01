add_library(yetty_yevent
    dispatch.c
)

target_include_directories(yetty_yevent PUBLIC
    ${YETTY_ROOT}/include
)
