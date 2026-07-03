# ymusic parser + render golden test — headless, deterministic.
yetty_add_c_test(
    NAME    ymusic_parse
    SOURCES music-test.c
    LIBS    yetty_ymusic yetty_yclass yetty_yface yetty_ydraw_core yetty_ysdf yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  golden
    TIMEOUT 30)
