# ychart parser + render golden test — headless, deterministic.
yetty_add_c_test(
    NAME    ychart_parse
    SOURCES chart-test.c
    LIBS    yetty_ychart yetty_ydraw_list yetty_ysdf yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  golden
    TIMEOUT 30)
