# ydiagram unit tests.

yetty_add_c_test(
    NAME    ydiagram_smoke
    SOURCES ydiagram-smoke-test.c
    LIBS    yetty_ydiagram yetty_ydraw_core yetty_ysdf yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  unit
    TIMEOUT 30)
