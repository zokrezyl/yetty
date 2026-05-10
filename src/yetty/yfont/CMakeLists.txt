# yetty font subsystem (C)

#-----------------------------------------------------------------------------
# Shader-glyph name lookup — tiny no-deps library so thin clients (yecho,
# tools) can resolve "@spinner" -> U+E000 without dragging FreeType/webgpu.
# The table is generated at configure time by scanning glyph-shaders/0xNN-*.wgsl.
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
# Full font subsystem (raster, msdf, ms-msdf) — pulls FreeType + webgpu.
#-----------------------------------------------------------------------------
add_library(yetty_yfont STATIC
    ms-raster-font.c
    ms-msdf-font.c
    msdf-font.c
    raster-font.c
    font-cache.c
)

target_include_directories(yetty_yfont PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
    ${FREETYPE_INCLUDE_DIR}
)

target_link_libraries(yetty_yfont PUBLIC
    ${FREETYPE_ALL_LIBS} webgpu yetty_ycdb yetty_yfont_shader_glyph)

# Embed shaders via incbin (or stubs on Emscripten)
incbin_add_resources(yetty_yfont
    ms_raster_font_shader "${CMAKE_CURRENT_SOURCE_DIR}/ms-raster-font.wgsl"
    raster_font_shader "${CMAKE_CURRENT_SOURCE_DIR}/raster-font.wgsl"
    ms_msdf_font_shader "${CMAKE_CURRENT_SOURCE_DIR}/ms-msdf-font.wgsl"
    msdf_font_shader "${CMAKE_CURRENT_SOURCE_DIR}/msdf-font.wgsl"
)
