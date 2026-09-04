# yetty_ymsdf — polymorphic MSDF generator (cpu | gpu).
# Wraps yetty_ymsdf_gen (cpu / msdfgen) and yetty_ymsdf_wgsl (gpu / compute
# shader) behind a single ops-table interface. Selected by the
# `msdf/generator` config key at yetty startup.

set(_YMSDF_SOURCES
    gpu-generator.c
    factory.c
    ensure.c
)
if(YETTY_ENABLE_FEATURE_YMSDF_GEN)
    list(APPEND _YMSDF_SOURCES cpu-generator.c)
endif()

add_library(yetty_ymsdf STATIC ${_YMSDF_SOURCES})

target_include_directories(yetty_ymsdf PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_ymsdf PUBLIC
    yetty_ycore
    yetty_ymsdf_wgsl
    yetty_yplatform_core
)
if(YETTY_ENABLE_FEATURE_YMSDF_GEN)
    target_link_libraries(yetty_ymsdf PUBLIC yetty_ymsdf_gen)
else()
    target_compile_definitions(yetty_ymsdf PRIVATE YETTY_YMSDF_NO_CPU=1)
endif()
