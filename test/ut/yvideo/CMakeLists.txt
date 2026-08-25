# yvideo H.264 SPS dimension-extraction boundary tests — headless,
# deterministic, all payloads synthesized in the test (no assets). Pins the
# hardened parser's accept/reject envelope: standard-derived Level 6.2
# bounds, hostile Exp-Golomb values (wraparound, cycle-count spin), garbage
# input, and chroma-derived crop units.

yetty_add_c_test(
    NAME    yvideo_h264_dims
    SOURCES h264-dims-test.c
    LIBS    yetty_yvideo_core yetty_ydraw_list yetty_yface yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract parser
    TIMEOUT 30)
