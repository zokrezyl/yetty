# ydrawlist2/ysdf2 byte-identity contract test — headless, deterministic.
# Pins that the v2 yclass pack path emits records byte-identical to the
# plain-C producer builders (same schema, two code paths) — the migration
# safety net for retiring the old surface (epic #712).

yetty_add_c_test(
    NAME    ydrawlist2_byte_identity
    SOURCES byte-identity-test.c
    LIBS    yetty_ysdf2 yetty_ydrawlist2 yetty_ysdf yetty_ydraw_list
            yetty_yface yetty_yclass yetty_ywire yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)
