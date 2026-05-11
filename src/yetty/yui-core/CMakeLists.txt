add_library(yetty_yui_core STATIC
    view.c
)

target_include_directories(yetty_yui_core
    PUBLIC
        ${YETTY_ROOT}/include
        ${YETTY_ROOT}/src
)
