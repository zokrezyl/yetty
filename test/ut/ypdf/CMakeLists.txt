# ypdf unit tests
#
# Standalone C programs registered with ctest via yetty_add_c_test(). Enable
# the suite with -DYETTY_ENABLE_FEATURE_TESTS=ON. Deterministic and headless
# (no GPU/display), so they carry only a layer label.

# Smoke test: render the bundled test-comprehensive.pdf.
yetty_add_c_test(
    NAME    ypdf_render
    SOURCES ypdf-render-test.c
    LIBS    yetty_ypdf yetty_ysdf yetty_ydraw_core yetty_yfont_core yetty_ycore
            yetty_yrender pdfio_lib yetty_yplatform_core
    SHIMS   trace
    LABELS  golden
    TIMEOUT 60)
target_compile_definitions(ypdf_render-test PRIVATE
    YPDF_TEST_PDF=\"${CMAKE_CURRENT_SOURCE_DIR}/test-comprehensive.pdf\")

# Position test: ypdf rendering of a real-world PDF (pdf-sample.pdf).
# Verifies that text spans land at the positions a reference PDF renderer
# (mutool stext) would compute. Catches the "all spans at same coordinate" bug
# that surfaces when measure_text returns errors and text_matrix never advances.
# This PDF resolves a system font through Fontconfig, whose process-global
# config (FcInitLoadConfigAndFonts / FcConfigInit, reached from
# resolve_pdf_font_via_fontconfig) is kept for the process lifetime and never
# FcFini()'d. Rather than exclude the whole test from ASAN, that third-party
# leak family is suppressed narrowly via test/lsan.supp (issue #414), so the
# test still runs under ASAN. (ypdf_render uses embedded fonts and stays clean.)
yetty_add_c_test(
    NAME    ypdf_positions
    SOURCES ypdf-positions-test.c
    LIBS    yetty_ypdf yetty_ysdf yetty_ydraw_core yetty_yfont_core yetty_ycore
            yetty_yrender pdfio_lib yetty_yplatform_core
    SHIMS   trace
    LABELS  golden
    TIMEOUT 60)
target_compile_definitions(ypdf_positions-test PRIVATE
    YPDF_TEST_PDF=\"${CMAKE_CURRENT_SOURCE_DIR}/pdf-sample.pdf\")

# Font metrics test.
yetty_add_c_test(
    NAME    ypdf_font
    SOURCES ypdf-font-test.c
    LIBS    yetty_yfont_core yetty_ycore yetty_yrender yetty_yplatform_core
    SHIMS   trace
    LABELS  unit
    TIMEOUT 30)
target_compile_definitions(ypdf_font-test PRIVATE
    YPDF_TEST_TTF=\"${YETTY_ROOT}/assets/fonts/DejaVuSansMNerdFontMono-Regular.ttf\")
