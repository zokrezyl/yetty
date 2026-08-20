add_library(yetty_ysdf STATIC
    funcs.gen.c
    aabb.gen.c
    yaml-factory.gen.c
    merge.c
    default-registry.c
)

target_include_directories(yetty_ysdf PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# merge.c calls yetty_ydraw_is_complex() from ydraw-list; without this
# edge the static-archive link order of downstream binaries (ygreeter on
# riscv) only resolves the symbol by luck.
target_link_libraries(yetty_ysdf PUBLIC
    yetty_ydraw_list
    yaml
)
