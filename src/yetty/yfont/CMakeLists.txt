# yetty font subsystem (C)

#-----------------------------------------------------------------------------
# Shader-glyph name lookup — tiny no-deps library so thin clients (yecho,
# tools) can resolve "@spinner" -> U+100000 without dragging FreeType/webgpu.
# The table is generated at configure time by scanning glyph-shaders/0xNNNN-*.wgsl.
#-----------------------------------------------------------------------------
set(SHADER_GLYPH_TABLE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/shader-glyph-table.gen.h")

find_package(Python3 COMPONENTS Interpreter REQUIRED)
execute_process(
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/gen-shader-glyph-table.py
        ${CMAKE_CURRENT_SOURCE_DIR}/glyph-shaders
        ${SHADER_GLYPH_TABLE_HEADER}
    RESULT_VARIABLE GEN_RESULT
    OUTPUT_VARIABLE GEN_OUTPUT
    ERROR_VARIABLE GEN_ERROR
)
if(NOT GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "shader-glyph table generation failed: ${GEN_ERROR}")
else()
    message(STATUS "${GEN_OUTPUT}")
endif()

add_library(yetty_yfont_shader_glyph STATIC
    shader-glyph.c
)
target_include_directories(yetty_yfont_shader_glyph PUBLIC
    ${YETTY_ROOT}/include
)
target_include_directories(yetty_yfont_shader_glyph PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}  # for shader-glyph-table.gen.h
)

#-----------------------------------------------------------------------------
# yetty_yfont_core — full font subsystem (raster, msdf, ms-msdf) without a
# WebGPU dep. The .c files describe atlas pixels + texture format metadata
# (uint32_t mirrors of WGPUTextureFormat / WGPUFilterMode — see
# yetty/yrender/texture-format.h) so clients (ycat, ypdf, ygui, riscv64
# cross-builds) get FreeType-driven metrics + glyph rasterization without
# pulling Dawn. Server-side WGPU uploads happen later in yrender via
# yetty_yrender_gpu_resource_set, which is itself GPU-less.
#-----------------------------------------------------------------------------
add_library(yetty_yfont_core STATIC
    ms-raster-font.c
    ms-msdf-font.c
    msdf-font.c
    raster-font.c
    # Codepoint -> shaping-script classification for complex-script run
    # detection. HarfBuzz-free — always compiled.
    shaping-script.c
    font-cache.c
)

target_include_directories(yetty_yfont_core PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
    ${FREETYPE_INCLUDE_DIR}
)

target_link_libraries(yetty_yfont_core PUBLIC
    ${FREETYPE_ALL_LIBS} yetty_ycdb yetty_yfont_shader_glyph yetty_yrender_core
    yetty_yplatform_core # raster-font glob faces: cross-platform dir iteration
)

# Complex-script shaping (raster-font.c). When enabled, link the standalone
# HarfBuzz shaper and define the gate macro PUBLIC so consumers that route
# runs through the shaper (yvterm) compile the same paths. Off by default —
# raster-font's shape_run op is then NULL and callers fall back to the
# per-codepoint path.
if(YETTY_ENABLE_LIB_HARFBUZZ)
    target_link_libraries(yetty_yfont_core PUBLIC harfbuzz::harfbuzz)
    target_compile_definitions(yetty_yfont_core PUBLIC YETTY_ENABLE_LIB_HARFBUZZ=1)
endif()

# Embed shaders via incbin (or stubs on Emscripten).
# These WGSL shaders are part of the gpu_resource_set returned by each font
# type's get_gpu_resource_set ops. The bytes are inert on client / riscv64
# builds (the RS is queried only by the server-side renderer), but kept
# alongside the font code to preserve the existing layout — see comment on
# yetty_yfont_core above.
incbin_add_resources(yetty_yfont_core
    ms_raster_font_shader "${CMAKE_CURRENT_SOURCE_DIR}/ms-raster-font.wgsl"
    raster_font_shader "${CMAKE_CURRENT_SOURCE_DIR}/raster-font.wgsl"
    ms_msdf_font_shader "${CMAKE_CURRENT_SOURCE_DIR}/ms-msdf-font.wgsl"
    msdf_font_shader "${CMAKE_CURRENT_SOURCE_DIR}/msdf-font.wgsl"
)
