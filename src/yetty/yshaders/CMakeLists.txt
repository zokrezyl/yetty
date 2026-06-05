# Copy all shaders to build directory
set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SHADER_DST_DIR ${CMAKE_BINARY_DIR}/assets/shaders)

# Collect all shader files recursively
file(GLOB_RECURSE SHADER_FILES
    ${SHADER_SRC_DIR}/*.wgsl
)

# Module shaders (embedded via incbin on native, loaded from assets on webasm)
set(MODULE_SHADERS
    ${YETTY_ROOT}/src/yetty/yfont/ms-raster-font.wgsl
    ${YETTY_ROOT}/src/yetty/yfont/raster-font.wgsl
    ${YETTY_ROOT}/src/yetty/yfont/ms-msdf-font.wgsl
    ${YETTY_ROOT}/src/yetty/yfont/msdf-font.wgsl
    ${YETTY_ROOT}/src/yetty/yvterm/text-layer.wgsl
    ${YETTY_ROOT}/src/yetty/yvterm/ydraw-layer.wgsl
    ${YETTY_ROOT}/src/yetty/yvterm/shader-glyph-layer.wgsl
    ${YETTY_ROOT}/src/yetty/ygrid/ygrid.wgsl
    ${YETTY_ROOT}/src/yetty/yrender/blend.wgsl
    # Generated SDF dispatcher + sdf_* functions (gen-sdf-code.py output).
    # ydraw-layer attaches it as a child resource set at runtime; merging
    # happens in the binder so the WGSL needs to be staged alongside the
    # other module shaders. Keep this in sync with the file(COPY) in
    # build-tools/yetty/targets/shared.cmake which seeds embed-data.
    ${YETTY_ROOT}/src/yetty/ysdf/ysdf.gen.wgsl
    # GPU MSDF compute shader — staged so the gpu generator can find it
    # at <shaders_dir>/msdf_gen.wgsl when materialising fonts on the fly.
    ${YETTY_ROOT}/src/yetty/ymsdf-wgsl/shaders/msdf_gen.wgsl
)

# For Android: copy shaders at configure time to ANDROID_ASSETS_DIR
# This ensures shaders are in place BEFORE gradle packages the APK
if(ANDROID_ASSETS_DIR)
    file(MAKE_DIRECTORY ${ANDROID_ASSETS_DIR}/shaders)
    foreach(SHADER_FILE ${SHADER_FILES})
        file(RELATIVE_PATH REL_PATH ${SHADER_SRC_DIR} ${SHADER_FILE})
        get_filename_component(REL_DIR ${REL_PATH} DIRECTORY)
        if(REL_DIR)
            file(MAKE_DIRECTORY ${ANDROID_ASSETS_DIR}/shaders/${REL_DIR})
        endif()
        file(COPY ${SHADER_FILE} DESTINATION ${ANDROID_ASSETS_DIR}/shaders/${REL_DIR})
    endforeach()
    # Copy module shaders
    foreach(SHADER_FILE ${MODULE_SHADERS})
        file(COPY ${SHADER_FILE} DESTINATION ${ANDROID_ASSETS_DIR}/shaders)
    endforeach()
    # Also copy gpu-screen.wgsl to root for backward compat
    file(COPY ${SHADER_SRC_DIR}/gpu-screen.wgsl DESTINATION ${ANDROID_ASSETS_DIR})
    message(STATUS "Copied shaders to Android assets: ${ANDROID_ASSETS_DIR}/shaders")
endif()

# Create custom target to copy shaders (for non-Android builds and incremental builds)
add_custom_target(copy-shaders ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_DST_DIR}
    COMMENT "Copying shaders to build directory"
)

foreach(SHADER_FILE ${SHADER_FILES})
    # Get relative path from shader source dir
    file(RELATIVE_PATH REL_PATH ${SHADER_SRC_DIR} ${SHADER_FILE})
    get_filename_component(REL_DIR ${REL_PATH} DIRECTORY)

    add_custom_command(TARGET copy-shaders POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_DST_DIR}/${REL_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHADER_FILE} ${SHADER_DST_DIR}/${REL_PATH}
    )
endforeach()

# Also copy the main gpu-screen.wgsl to root for backward compat
add_custom_command(TARGET copy-shaders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHADER_SRC_DIR}/gpu-screen.wgsl ${CMAKE_BINARY_DIR}/assets/gpu-screen.wgsl
)

# Copy module shaders to assets/shaders (needed for webasm)
foreach(SHADER_FILE ${MODULE_SHADERS})
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)
    add_custom_command(TARGET copy-shaders POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHADER_FILE} ${SHADER_DST_DIR}/${SHADER_NAME}
    )
endforeach()

# Copy per-glyph shaders to assets/shaders/glyph-shaders/. The shader-glyph
# layer scans this dir at runtime, reads each .wgsl, and splices them into
# the layer shader. Drop a 0xNNNN-name.wgsl file here to add a glyph.
file(GLOB GLYPH_SHADER_FILES "${YETTY_ROOT}/src/yetty/yfont/glyph-shaders/*.wgsl")
add_custom_command(TARGET copy-shaders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_DST_DIR}/glyph-shaders
)
foreach(GLYPH_FILE ${GLYPH_SHADER_FILES})
    get_filename_component(GLYPH_NAME ${GLYPH_FILE} NAME)
    add_custom_command(TARGET copy-shaders POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GLYPH_FILE} ${SHADER_DST_DIR}/glyph-shaders/${GLYPH_NAME}
    )
endforeach()
if(ANDROID_ASSETS_DIR)
    file(MAKE_DIRECTORY ${ANDROID_ASSETS_DIR}/shaders/glyph-shaders)
    foreach(GLYPH_FILE ${GLYPH_SHADER_FILES})
        file(COPY ${GLYPH_FILE} DESTINATION ${ANDROID_ASSETS_DIR}/shaders/glyph-shaders)
    endforeach()
endif()
