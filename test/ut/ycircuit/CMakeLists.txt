# ycircuit parser + render golden test — headless, deterministic.
yetty_add_c_test(
    NAME    ycircuit_parse
    SOURCES circuit-test.c
    LIBS    yetty_ycircuit yetty_yclass yetty_yface yetty_ydraw_core yetty_ysdf yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  golden
    TIMEOUT 30)
