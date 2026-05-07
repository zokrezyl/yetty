add_library(yetty_ypaint_core STATIC
    buffer.c
    cmds.c
    complex-prim-types.c
    flyweight.c
    font-prim.c
    text-span-prim.c
)

target_include_directories(yetty_ypaint_core PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ypaint_core PUBLIC
    yetty_ycore
)
