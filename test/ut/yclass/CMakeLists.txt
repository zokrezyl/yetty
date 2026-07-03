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
