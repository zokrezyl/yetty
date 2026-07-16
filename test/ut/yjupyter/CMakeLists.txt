# yjupyter protocol contract test — Jupyter v5 message build/encode/decode plus
# session correlation (parent_header.msg_id) and kernel busy/idle tracking.

include(${YETTY_ROOT}/build-tools/yetty/libs/yyjson.cmake)

yetty_add_c_test(
    NAME    yjupyter_protocol
    SOURCES protocol-test.c
    LIBS    yetty_yjupyter yetty_yclass yetty_ycore yyjson::yyjson
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)

# Live end-to-end test against a real Jupyter Server (REST + WebSocket). Self-
# skips (exit 0) unless YJUPYTER_URL is set, so it stays green in headless CI;
# point it at a running server to exercise the actual wire.
yetty_add_c_test(
    NAME    yjupyter_kernel_e2e
    SOURCES kernel-e2e-test.c
    LIBS    yetty_yjupyter yetty_yclass yetty_ycore yyjson::yyjson
    SHIMS   trace platform_thread platform_term
    LABELS  contract network
    TIMEOUT 60)
