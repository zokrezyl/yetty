# ysvg parser + render golden test — headless, deterministic.
yetty_add_c_test(
    NAME    ysvg_parse
    SOURCES svg-test.c
    LIBS    yetty_ysvg yetty_ydraw_core yetty_ysdf yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  golden
    TIMEOUT 30)
