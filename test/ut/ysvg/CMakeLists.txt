# ysvg parser + render golden test — headless, deterministic.
yetty_add_c_test(
    NAME    ysvg_parse
    SOURCES svg-test.c
    LIBS    yetty_ysvg yetty_ydraw_core yetty_ysdf yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  golden
    TIMEOUT 30)

# File-driven diagnostic harness: `ysvg_diag-test <file.svg>...` prints the full
# error chain per file. No-op under ctest (no args); used to triage real-world
# SVGs and capture failing ones as regression assets.
yetty_add_c_test(
    NAME    ysvg_diag
    SOURCES svg-diag.c
    LIBS    yetty_ysvg yetty_ydraw_core yetty_ysdf yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  golden
    TIMEOUT 30)
