# yplot resolver unit tests
#
# Pure, headless contract tests for yetty_yplot_resolve(): expression↔options
# merge, precedence policies, per-property presence, and semantic validation
# (unknown targets/attrs/keywords, declaration-order independence). Links the
# GPU-less yetty_yplot_core; no device, I/O, or context.

yetty_add_c_test(
    NAME    yplot_resolve
    SOURCES resolve-test.c
    LIBS    yetty_yplot_core yetty_yexpr yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract parser
    TIMEOUT 30)

# emit_into contract: input validation, append-into-existing-list, log-range
# failure. GPU-less (yetty_yplot_core + ydraw-core).
yetty_add_c_test(
    NAME    yplot_emit
    SOURCES emit-test.c
    LIBS    yetty_yplot_core yetty_ydraw_core yetty_yexpr yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)
