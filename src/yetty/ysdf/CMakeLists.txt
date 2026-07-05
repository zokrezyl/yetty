add_library(yetty_ysdf STATIC
    funcs.gen.c
    aabb.gen.c
    yaml-factory.gen.c
    merge.c
)

target_include_directories(yetty_ysdf PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ysdf PUBLIC yaml)
