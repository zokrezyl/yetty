# yetty_ymsdf — polymorphic MSDF generator (cpu | gpu).
# Wraps yetty_ymsdf_gen (cpu / msdfgen) and yetty_ymsdf_wgsl (gpu / compute
# shader) behind a single ops-table interface. Selected by the
# `msdf/generator` config key at yetty startup.

add_library(yetty_ymsdf STATIC
    cpu-generator.c
    gpu-generator.c
    factory.c
)

target_include_directories(yetty_ymsdf PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_ymsdf PUBLIC
    yetty_ycore
    yetty_ymsdf_gen
    yetty_ymsdf_wgsl
)
