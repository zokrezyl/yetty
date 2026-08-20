# ydraw-list command wire + golden contract test — headless, deterministic.
# yetty_ysdf supplies the default registry the parser strides with;
# yetty_ywire the statemachine + coroutine the drawable iterator pulls.

yetty_add_c_test(
    NAME    ydraw_list_wire
    SOURCES wire-test.c
    LIBS    yetty_ysdf yetty_ydraw_list yetty_ywire yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)
