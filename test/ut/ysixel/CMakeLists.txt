# ysixel decoder contract test — pure pixel math, headless, deterministic.
# Links only the decoder core (which pulls yimage_core → ydraw_list → yface).

yetty_add_c_test(
    NAME    ysixel_decode
    SOURCES decode-test.c
    LIBS    yetty_ysixel_core yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)
