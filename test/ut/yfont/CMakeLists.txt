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
