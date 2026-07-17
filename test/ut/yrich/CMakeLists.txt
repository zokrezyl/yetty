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

# Command/history transactional-integrity invariants (Phase-0A steps 3-4):
# a declined merge never drops an applied edit; a failed undo/redo apply keeps
# the command on its original stack.
yetty_add_c_test(
    NAME    yrich_command
    SOURCES command-test.c
    LIBS    yetty_yrich
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)

# Atomic-save contract (Phase-0A steps 5-6): round-trip, and a failed save
# leaves the prior destination file intact.
yetty_add_c_test(
    NAME    yrich_save_atomic
    SOURCES save-test.c
    LIBS    yetty_yrich
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)
