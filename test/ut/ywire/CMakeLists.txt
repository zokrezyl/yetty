# ywire OSC/DCS protocol contract test — headless, deterministic. yetty_ywire
# PUBLIC-links ycore/lz4/coroutine, so linking it pulls the codec + framer.

yetty_add_c_test(
    NAME    ywire_protocol
    SOURCES protocol-test.c
    LIBS    yetty_ywire yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)

# Malformed-stream + recovery hardening (#417): garbage framing, corrupt
# base64/LZ4, chunk boundaries, desync detection, oversized args, yface boundary.
yetty_add_c_test(
    NAME    ywire_malformed
    SOURCES malformed-test.c
    LIBS    yetty_ywire yetty_yface yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)

# Foreign-DCS / foreign-OSC passthrough (#579): XTGETTCAP, DECRQSS and
# unhandled standard OSC (52/title/…) must traverse the framer byte-identical
# instead of being swallowed + leaked as screen text; owned envelopes still
# parse and are not relayed as text; truncated foreign DCS does not wedge.
yetty_add_c_test(
    NAME    ywire_passthrough
    SOURCES passthrough-test.c
    LIBS    yetty_ywire yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)

# Streaming-encoder reuse (#30): one long-lived SM emitting many envelopes
# back-to-back — LZ4F context reuse across frames/block boundaries, mixed
# compression, encode+decode interleaved on one SM (the DCS-transport shape),
# guard-error recovery, mid-envelope teardown.
yetty_add_c_test(
    NAME    ywire_emit_reuse
    SOURCES emit-reuse-test.c
    LIBS    yetty_yface yetty_ywire yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)

# Connection layer (#435/#436/#437): two connections back-to-back over pipes —
# dynamic-channel OPEN/DATA/EOF/CLOSE handshake, flow-control windows +
# WINDOW_ADJUST credit, DATA chunking + fair interleaving, well-known lanes.
yetty_add_c_test(
    NAME    ywire_connection
    SOURCES connection-test.c
    LIBS    yetty_yclass yetty_ywire yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)

# Host-side connection layer (#455): attach-mode acceptor over a borrowed
# statemachine (the terminal's shape) against an owned-mode client —
# default rejection, accept/echo, host-initiated channels, credit grants.
yetty_add_c_test(
    NAME    ywire_host_connection
    SOURCES host-connection-test.c
    LIBS    yetty_yclass yetty_ywire yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)
