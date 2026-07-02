# ywire OSC/DCS protocol contract test — headless, deterministic. yetty_ywire
# PUBLIC-links ycore/lz4/coroutine, so linking it pulls the codec + framer.

yetty_add_c_test(
    NAME    ywire_protocol
    SOURCES protocol-test.c
    LIBS    yetty_ywire yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)
