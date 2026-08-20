# yvterm terminal-core contract test — headless (no fork, no GPU). Drives the
# libvterm-backed grid model directly + via the in-process memory-PTY seam, and
# checks the wire statemachine's terminal-text vs DCS/OSC-envelope split.
#
# memory-pty.c is compiled per-platform in the real build (not its own lib), so
# the test pulls the source in directly. yetty_yvterm PUBLIC-links the GPU libs
# for the vterm figure, but the grid path touches none of them.

yetty_add_c_test(
    NAME    yvterm_terminal
    SOURCES terminal-test.c
            ${YETTY_ROOT}/src/yetty/ypty/memory-pty.c
            ${YETTY_ROOT}/src/yetty/yplatform/time/default.c
    LIBS    yetty_yvterm yetty_yclass yetty_ywire yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract terminal wire
    TIMEOUT 30)

# Terminal-state matrix (#418): BCE, wide-glyph spill, autowrap, tab stops,
# cursor save/restore, insert/delete char, scroll-region isolation, extended
# SGR, and the alt-screen enter/restore contract. Pure grid model — no
# memory-PTY or wire statemachine.
yetty_add_c_test(
    NAME    yvterm_state_matrix
    SOURCES state-matrix-test.c
    LIBS    yetty_yvterm yetty_api_ytermsink yetty_yclass yetty_ywire yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract terminal
    TIMEOUT 30)

# Tiered scrollback (#466): hot-window figure-runtime eviction, wire-envelope
# retention in the line arena, view-driven re-materialization + sweep.
# Complexes are fabricated with a test-local ops table — no factory, no GPU.
yetty_add_c_test(
    NAME    yvterm_scroll_tiers
    SOURCES scroll-tiers-test.c
    LIBS    yetty_yvterm yetty_yclass yetty_ywire yetty_ydraw_factory yetty_ydraw_core yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract terminal scrollback
    TIMEOUT 30)
