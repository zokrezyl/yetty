# yscene unit tests — headless (no GPU): the dom's retained-tree
# contract straight against the module-internal API, and the scene's
# behavioral surface (typed methods, wire adapter, derive/paint order,
# hit-test) through the generated public API.

yetty_add_c_test(
    NAME    yscene_dom
    SOURCES dom-test.c
    LIBS    yetty_yscene yetty_ydraw_core yetty_ysdf yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)

yetty_add_c_test(
    NAME    yscene_scene
    SOURCES scene-test.c
    LIBS    yetty_yscene yetty_yfigure yetty_ydraw_core yetty_ysdf yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)
