# yfont unit tests
#
# Complex-script shaping (HarfBuzz) contract test. Registered only when the
# shaper is built in (YETTY_ENABLE_LIB_HARFBUZZ); the test self-skips at runtime
# when the bundled Noto fonts are not staged under the build tree. Headless
# (metrics-only font, no GPU/display).

yetty_add_c_test(
    NAME    yfont_shaping
    SOURCES shaping-test.c
    LIBS    yetty_yfont_core yetty_ycore yetty_yrender yetty_yplatform_core
    SHIMS   trace
    LABELS  unit
    TIMEOUT 30)
target_compile_definitions(yfont_shaping-test PRIVATE
    YFONT_TEST_FONT_DIR=\"${CMAKE_BINARY_DIR}/embed-data/fonts\")

# ms-raster fallback-chain contract (#89): the yscene grid's fallback face must
# resolve CJK through the FreeType chain and composite clusters. Headless;
# self-skips when the staged fonts/shader are absent.
if(APPLE)
    set(YFONT_FALLBACK_PATHS_SOURCE ${YETTY_ROOT}/src/yetty/yplatform/paths/macos.c)
else()
    set(YFONT_FALLBACK_PATHS_SOURCE ${YETTY_ROOT}/src/yetty/yplatform/paths/linux.c)
endif()
yetty_add_c_test(
    NAME    yfont_fallback
    SOURCES fallback-test.c
            ${YETTY_ROOT}/src/yetty/yconfig/config.c
            ${YETTY_ROOT}/src/yetty/yplatform/paths/create.c
            ${YFONT_FALLBACK_PATHS_SOURCE}
            ${YETTY_ROOT}/src/yetty/yplatform/fs/default.c
            ${YETTY_ROOT}/src/yetty/yplatform/getopt.c
    LIBS    yetty_yfont_core yaml yetty_ycore yetty_yrender yetty_yplatform_core
    SHIMS   trace
    LABELS  unit
    TIMEOUT 30)
target_compile_definitions(yfont_fallback-test PRIVATE
    YFONT_TEST_FONT_DIR=\"${CMAKE_BINARY_DIR}/embed-data/fonts\"
    YFONT_TEST_SHADER_DIR=\"${CMAKE_BINARY_DIR}/embed-data/shaders\")

# End-to-end shaped-glyph rasterization: shape -> gid atlas -> composite pixels.
# Set YFONT_SHAPING_DUMP=<path.ppm> at runtime to emit the canvas for eyeballing.
yetty_add_c_test(
    NAME    yfont_shaping_render
    SOURCES shaping-render-test.c
    LIBS    yetty_yfont_core yetty_ycore yetty_yrender yetty_yplatform_core
    SHIMS   trace
    LABELS  unit
    TIMEOUT 30)
target_compile_definitions(yfont_shaping_render-test PRIVATE
    YFONT_TEST_FONT_DIR=\"${CMAKE_BINARY_DIR}/embed-data/fonts\"
    YFONT_RASTER_SHADER=\"${YETTY_ROOT}/src/yetty/yfont/raster-font.wgsl\")

# Programming-ligature detection table (font-free, deterministic).
yetty_add_c_test(
    NAME    yfont_ligature
    SOURCES ligature-test.c
    LIBS    yetty_yfont_core yetty_ycore yetty_yrender yetty_yplatform_core
    SHIMS   trace
    LABELS  unit
    TIMEOUT 30)
