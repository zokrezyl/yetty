# ypaint layer - rich content rendering
# Note: core moved to ypaint-core, SDF/yaml moved to ysdf

add_library(yetty_ypaint STATIC
    flyweight.c
    ypaint-canvas.c
)

target_link_libraries(yetty_ypaint PUBLIC
    yetty_ypaint_core
    yetty_ypaint_factory
    yetty_ysdf
    yetty_yimage
    yetty_yplot
    $<$<BOOL:${YETTY_ENABLE_FEATURE_YMESH}>:yetty_ymesh>
    $<$<BOOL:${YETTY_ENABLE_FEATURE_YMSDF_GEN}>:yetty_ymsdf_gen>
    $<$<BOOL:${YETTY_ENABLE_FEATURE_YMSDF_WGSL}>:yetty_ymsdf>
)

if(YETTY_ENABLE_FEATURE_YMESH)
    target_compile_definitions(yetty_ypaint PUBLIC YETTY_HAS_YMESH=1)
else()
    target_compile_definitions(yetty_ypaint PUBLIC YETTY_HAS_YMESH=0)
endif()

# YETTY_HAS_YMSDF_GEN gates ypaint-canvas.c's on-demand MSDF CDB generation
# (uses yetty_ymsdf_gen_cpu_generate from yetty_ymsdf_gen). When the feature
# is off (e.g. Windows where the upstream msdfgen tarball ships /MT static-CRT
# and would clash with yetty's /MD), the CDB must be pre-built and present.
if(YETTY_ENABLE_FEATURE_YMSDF_GEN)
    target_compile_definitions(yetty_ypaint PUBLIC YETTY_HAS_YMSDF_GEN=1)
else()
    target_compile_definitions(yetty_ypaint PUBLIC YETTY_HAS_YMSDF_GEN=0)
endif()
# Note: yetty_ypaint references yplatform_mkdir_p / yplatform_file_exists
# (per-platform shared sources). Final executables already include
# YETTY_YPLATFORM_THREAD_SOURCES, so these symbols resolve at link time.
