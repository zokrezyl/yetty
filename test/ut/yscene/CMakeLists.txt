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

# yscene_scene exercises the scene's wire adapter against ymux projector data, so
# it links the ymux libs — which exist on desktop Linux only (YETTY_YMUX_SUPPORTED).
# Skip it where ymux is not built; the other yscene tests need no ymux and run
# everywhere.
if(TARGET yetty_ymux)
    yetty_add_c_test(
        NAME    yscene_scene
        SOURCES scene-test.c
        LIBS    yetty_yscene yetty_yfigure yetty_ydraw_core yetty_ysdf yetty_ymux yetty_api_ymux yetty_ycore vterm lz4_static
        SHIMS   trace platform_thread platform_term
        LABELS  contract wire
        TIMEOUT 30)
endif()

# The cluster-preserving store (#699.5 retention stage): pins mark retention,
# wide shape, pen/BCE, scroll/shift, resize, and reverse-at-read semantics
# BEFORE the vtermgrid state-callback cutover swaps it into the live path.
yetty_add_c_test(
    NAME    yscene_vtermgrid_store
    SOURCES vtermgrid-store-test.c
    LIBS    yetty_yscene yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)

yetty_add_c_test(
    NAME    yscene_vtermgrid
    SOURCES vtermgrid-test.c
    LIBS    yetty_yscene yetty_ycore vterm lz4_static
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)

# Receiver coverage ported from the retired ygrid suite: nested groups,
# the multi-group delta pattern, dirty/reset lifecycle, malformed-body
# recovery + dump stability, and the complex-instance lifecycle.
yetty_add_c_test(
    NAME    yscene_wire_parity
    SOURCES wire-parity-test.c
    LIBS    yetty_yscene yetty_yfigure yetty_ydraw_factory yetty_ydraw_core yetty_ysdf yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)
