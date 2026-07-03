# ymarkdown unit tests.

yetty_add_c_test(
    NAME    ymarkdown_smoke
    SOURCES ymarkdown-smoke-test.c
    LIBS    yetty_ymarkdown yetty_ydraw_core yetty_ysdf yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  unit
    TIMEOUT 30)
