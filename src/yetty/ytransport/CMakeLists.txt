# yetty core (C)
add_library(yetty_ycore STATIC
    tcp-transport.c
)

target_include_directories(yetty_ycore PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

