# yrich geometry/style unit tests
#
# Pure header-only helpers: rect containment/intersection, spreadsheet cell
# addressing, default text style, and ABGR colour packing. No context, no GPU;
# the yclass document model is out of scope for this pure test.

yetty_add_c_test(
    NAME    yrich_geometry
    SOURCES geometry-test.c
    LIBS    yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)
