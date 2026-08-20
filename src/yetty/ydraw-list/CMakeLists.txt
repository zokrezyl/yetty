add_library(yetty_ydraw_list STATIC
    drawable-list.c
    cmds.c
    complex.c
    drawable-list-registry.c
    font-resource.c
    drawable-iterator.c
    text-drawable-list.c
)

# drawable-iterator uses yetty_yterm's wire-statemachine header for declarations,
# but does NOT link against yterm — it only needs the header.
target_include_directories(yetty_ydraw_list PRIVATE
    ${YETTY_ROOT}/include
)

target_include_directories(yetty_ydraw_list PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ydraw_list PUBLIC
    yetty_ycore
)
