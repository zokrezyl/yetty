# Unit tests for the ygui widget toolkit.
#
# Exercises class registration, instance allocation, data slice access,
# method dispatch, super chains, flex layout, the clickable mixin, and
# end-to-end two-pass wire emission for figure widgets. Assertions use the
# shared ytest.h harness (NDEBUG-safe — see test/support/ytest.h).

yetty_add_c_test(
    NAME    ygui_object
    SOURCES object-test.c
    LIBS    yetty_ygui yetty_ycore yetty_ydraw_list
    SHIMS   trace platform_thread platform_term
    LABELS  unit
    TIMEOUT 30)

yetty_add_c_test(
    NAME    ygui_layout
    SOURCES layout-test.c
    # yetty_ysdf provides the default drawable-list registry the paint test
    # iterates with (drift-proof prim walk); it PUBLIC-links ydraw_list.
    LIBS    yetty_ygui yetty_ycore yetty_ysdf yetty_ydraw_list
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

# Layout matrix (justify/align/gap/flex/nested) + click-driven event state
# machines (clickable, checkbox, toggle).
yetty_add_c_test(
    NAME    ygui_matrix
    SOURCES matrix-test.c
    LIBS    yetty_ygui yetty_ycore yetty_ydraw_list
    SHIMS   trace platform_thread platform_term
    LABELS  unit
    TIMEOUT 30)

# Widget behaviour + mixin composition beyond layout (#420): slider press→value,
# draggable mixin sequence, clickable reused by radio/selectable, tabbar model.
yetty_add_c_test(
    NAME    ygui_behavior
    SOURCES behavior-test.c
    LIBS    yetty_ygui yetty_ycore yetty_ydraw_list
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)

# Single-line textinput edit box: caret editing, keyboard + mouse selection,
# select-all, word motion, double-click word selection, and the insert_text
# paste/cut primitive. platform_time backs the double-click clock.
yetty_add_c_test(
    NAME    ygui_textinput
    SOURCES textinput-test.c
    LIBS    yetty_ygui yetty_ycore yetty_ydraw_list
    SHIMS   trace platform_thread platform_term platform_time
    LABELS  contract
    TIMEOUT 30)
