# ypdf unit tests
#
# Runs as standalone C programs using ctest. Enable with
# -DYETTY_ENABLE_FEATURE_TESTS=ON.

enable_testing()

# Shared shim: pulls in the ytrace helpers + platform mutex/timestamp
# symbols that yfont/yrender and ypdf reference internally.
set(YPDF_TEST_SHIM_SOURCES
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

# Smoke test: render the bundled test-comprehensive.pdf.
add_executable(ypdf-render-test
    ypdf-render-test.c
    ${YPDF_TEST_SHIM_SOURCES}
)
target_include_directories(ypdf-render-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)
target_link_libraries(ypdf-render-test PRIVATE
    yetty_ypdf yetty_ypaint yetty_ypaint_core yetty_yfont_core yetty_ycore
    yetty_yrender pdfio_lib yetty_yplatform_core)
target_compile_definitions(ypdf-render-test PRIVATE
    YPDF_TEST_PDF=\"${CMAKE_CURRENT_SOURCE_DIR}/test-comprehensive.pdf\"
)
add_test(NAME ypdf_render COMMAND ypdf-render-test)

# Position test: ypdf rendering of a real-world PDF (pdf-sample.pdf).
# Verifies that text spans land at the positions a reference PDF
# renderer (mutool stext) would compute. Catches the "all spans at same
# coordinate" bug that surfaces when measure_text returns errors and
# text_matrix never advances.
add_executable(ypdf-positions-test
    ypdf-positions-test.c
    ${YPDF_TEST_SHIM_SOURCES}
)
target_include_directories(ypdf-positions-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)
target_link_libraries(ypdf-positions-test PRIVATE
    yetty_ypdf yetty_ypaint yetty_ypaint_core yetty_yfont_core yetty_ycore
    yetty_yrender pdfio_lib yetty_yplatform_core)
target_compile_definitions(ypdf-positions-test PRIVATE
    YPDF_TEST_PDF=\"${CMAKE_CURRENT_SOURCE_DIR}/pdf-sample.pdf\"
)
add_test(NAME ypdf_positions COMMAND ypdf-positions-test)

# Font metrics test.
add_executable(ypdf-font-test
    ypdf-font-test.c
    ${YPDF_TEST_SHIM_SOURCES}
)
target_include_directories(ypdf-font-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)
target_link_libraries(ypdf-font-test PRIVATE
    yetty_yfont_core yetty_ycore yetty_yrender yetty_yplatform_core)
target_compile_definitions(ypdf-font-test PRIVATE
    YPDF_TEST_TTF=\"${YETTY_ROOT}/assets/fonts/DejaVuSansMNerdFontMono-Regular.ttf\"
)
add_test(NAME ypdf_font COMMAND ypdf-font-test)
