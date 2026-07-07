# yclass contract tests — the object/dispatch spine and the RPC request layer.
#
# Both are headless, deterministic, and use only the yclass + ycore libs (yclass
# PUBLIC-links ywire). The RPC test spins a server thread over a socketpair, so
# it links Threads and carries the `rpc` domain label.

yetty_add_c_test(
    NAME    yclass_object
    SOURCES object-test.c
    LIBS    yetty_yclass yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)

yetty_add_c_test(
    NAME    yclass_rpc
    SOURCES rpc-test.c
    LIBS    yetty_yclass yetty_ycore Threads::Threads
    SHIMS   trace platform_thread platform_term
    LABELS  contract rpc
    TIMEOUT 30)

# Endpoint selection (#438): side-channel fd pair via YETTY_YWIRE_SIDE_CHANNEL
# vs in-band fallback, with loud failure on malformed/stale specs.
yetty_add_c_test(
    NAME    yclass_transport_env
    SOURCES transport-env-test.c
    LIBS    yetty_yclass yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)

# DCS-over-PTY transport (#30): sequential flush reuse of the shared wire SM
# (compressed and not), duplex encode/decode interleaving on one SM, empty
# flush no-op. The test plays the host over two pipes.
yetty_add_c_test(
    NAME    yclass_transport_dcs
    SOURCES transport-dcs-test.c
    LIBS    yetty_yclass yetty_ywire yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)
