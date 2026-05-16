add_library(yetty_ydraw_core STATIC
    buffer.c
    cmds.c
    complex-prim-types.c
    flyweight.c
    font-prim.c
    drawable-iterator.c
    text-span-prim.c
)

# drawable-iterator uses yetty_yterm's wire-statemachine header for declarations,
# but does NOT link against yterm — it only needs the header.
target_include_directories(yetty_ydraw_core PRIVATE
    ${YETTY_ROOT}/include
)

target_include_directories(yetty_ydraw_core PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ydraw_core PUBLIC
    yetty_ycore
)
