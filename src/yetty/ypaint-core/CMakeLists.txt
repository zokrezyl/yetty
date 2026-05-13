add_library(yetty_ypaint_core STATIC
    buffer.c
    cmds.c
    complex-prim-types.c
    flyweight.c
    font-prim.c
    prim-iter.c
    text-span-prim.c
)

# prim-iter uses yetty_yterm's osc-statemachine header for declarations,
# but does NOT link against yterm — it only needs the header.
target_include_directories(yetty_ypaint_core PRIVATE
    ${YETTY_ROOT}/include
)

target_include_directories(yetty_ypaint_core PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ypaint_core PUBLIC
    yetty_ycore
)
