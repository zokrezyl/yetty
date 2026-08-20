# ymarkdown golden tests (primitive-family structure + byte-exact determinism).

yetty_add_c_test(
    NAME    ymarkdown_golden
    SOURCES ymarkdown-smoke-test.c
    LIBS    yetty_ymarkdown yetty_ydraw_list yetty_ysdf yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  golden
    TIMEOUT 30)
