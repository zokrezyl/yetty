# ygrid unit tests
#
# Wire-driven receiver tests: feed ydraw command bytes through
# ygrid_process_bytes and assert entity / prim state via the ygrid
# ops->dump.

yetty_add_c_test(
    NAME    ygrid_wire
    SOURCES ygrid-wire-test.c
    LIBS    yetty_ygrid yetty_ydraw_factory yetty_ydraw_core yetty_ysdf yetty_yfigure yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)

# Malformed-buffer recovery + dump stability (complements the wire goldens).
yetty_add_c_test(
    NAME    ygrid_robustness
    SOURCES robustness-test.c
    LIBS    yetty_ygrid yetty_ydraw_factory yetty_ydraw_core yetty_ysdf yetty_yfigure yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)
