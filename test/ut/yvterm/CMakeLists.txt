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
    LIBS    yetty_yvterm yetty_yclass yetty_ywire yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract terminal wire
    TIMEOUT 30)
