# ydraw-core command wire + golden contract test — headless, deterministic.
# yetty_ydraw PUBLIC-links ydraw_core/ysdf, and supplies the default registry
# the parser strides with.

yetty_add_c_test(
    NAME    ydraw_core_wire
    SOURCES wire-test.c
    LIBS    yetty_ydraw yetty_ydraw_core yetty_ysdf yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)
