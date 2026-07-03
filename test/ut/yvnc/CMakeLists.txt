# yvnc capture-protocol unit tests
#
# Pure tile-grid math + packed wire-format headers (no GPU, no socket). The
# framebuffer readback + JPEG/H.264 encoding are GPU-bound and covered by the
# nightly VNC capture lane instead.

yetty_add_c_test(
    NAME    yvnc_protocol
    SOURCES protocol-test.c
    LIBS    yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)
