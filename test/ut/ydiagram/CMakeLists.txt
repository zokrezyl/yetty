# ydiagram golden tests (parse/IR/layout structure + byte-exact determinism).

yetty_add_c_test(
    NAME    ydiagram_golden
    SOURCES ydiagram-smoke-test.c
    LIBS    yetty_ydiagram yetty_ydraw_list yetty_ysdf yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  golden
    TIMEOUT 30)
