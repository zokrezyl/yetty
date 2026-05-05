# ymsdf-wgsl — GPU MSDF CDB generator (compute-shader based, C port of
# yetty-poc/src/yetty/msdf-wgsl). Same output format as ymsdf-gen (CPU)
# so downstream msdf-font readers don't care which one produced the CDB.

include(${YETTY_ROOT}/build-tools/cmake/libs/freetype.cmake)

add_library(yetty_ymsdf_wgsl STATIC
    src/ymsdf-wgsl.c
)

target_include_directories(yetty_ymsdf_wgsl PUBLIC
    ${YETTY_ROOT}/include
    ${FREETYPE_INCLUDE_DIR}
)

target_link_libraries(yetty_ymsdf_wgsl PUBLIC
    yetty_ycore
    yetty_ycdb
    webgpu
    ${FREETYPE_ALL_LIBS}
)

# src/ymsdf-wgsl.c uses <stdatomic.h>. MSVC sets __STDC_NO_ATOMICS__ by
# default and only clears it when both /std:clatest (or /std:c11) and
# /experimental:c11atomics are passed. Same incantation as the main
# yetty target in build-tools/cmake/targets/windows.cmake.
target_compile_options(yetty_ymsdf_wgsl PRIVATE
    $<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/std:clatest>
    $<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/experimental:c11atomics>)

# Stage the compute shader next to consumers' executables so find_shader_path()
# can pick it up via the exe-dir / exe-dir/shaders fallbacks. Final placement
# is handled by the assets pipeline (build-tools/cmake/asset-staging.cmake);
# here we just expose the source path for that pipeline to copy from.
set(YETTY_YMSDF_WGSL_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/msdf_gen.wgsl
    CACHE FILEPATH "Path to the msdf_gen compute shader")
