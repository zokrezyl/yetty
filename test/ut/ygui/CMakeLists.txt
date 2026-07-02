# Unit tests for the ygui widget toolkit.
#
# Exercises class registration, instance allocation, data slice access,
# method dispatch, super chains, flex layout, the clickable mixin, and
# end-to-end two-pass wire emission for figure widgets. Assertions use the
# shared ytest.h harness (NDEBUG-safe — see test/support/ytest.h).

yetty_add_c_test(
    NAME    ygui_object
    SOURCES object-test.c
    LIBS    yetty_ygui yetty_ycore yetty_ydraw_core
    SHIMS   trace platform_thread platform_term
    LABELS  unit
    TIMEOUT 30)

yetty_add_c_test(
    NAME    ygui_layout
    SOURCES layout-test.c
    # yetty_ydraw provides the default drawable-list registry the paint test
    # iterates with (drift-proof prim walk); it PUBLIC-links ydraw_core/ysdf/…
    LIBS    yetty_ygui yetty_ycore yetty_ydraw_core yetty_ydraw
    SHIMS   trace platform_thread platform_term
    LABELS  unit
    TIMEOUT 30)

yetty_add_c_test(
    NAME    ygui_figure
    SOURCES figure-test.c
    LIBS    yetty_ygui yetty_ycore yetty_yfigure yetty_yplatform_core
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)
