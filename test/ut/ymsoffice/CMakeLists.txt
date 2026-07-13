# ymsoffice golden tests (model extraction + primitive-family structure +
# byte-exact determinism). Fixtures are committed; regenerate with
# make-fixtures.py.

yetty_add_c_test(
    NAME    ymsoffice_golden
    SOURCES ymsoffice-test.c
    LIBS    yetty_ymsoffice yetty_ydraw_core yetty_ysdf yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  golden
    TIMEOUT 30)
target_compile_definitions(ymsoffice_golden-test PRIVATE
    YMSOFFICE_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\")
