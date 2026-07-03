# yctl RPC message-layer unit tests
#
# Pure msgpack-RPC wire contract: request/notification decode, response
# encode→decode round-trip, malformed-frame rejection, partial-frame streaming.
# No TCP socket, no event loop, no display.

yetty_add_c_test(
    NAME    yctl_message
    SOURCES yctl-message-test.c
    LIBS    yetty_yctl yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract rpc
    TIMEOUT 30)
