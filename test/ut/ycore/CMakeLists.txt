# ycore foundation unit tests
#
# Pure data-structure contract tests: growable buffer, base64 codec, hex colour
# parsing, fixed-capacity hash map, Result macros, and the error cause chain
# (including its serialize/deserialize wire round-trip). No GPU/I/O/context.

yetty_add_c_test(
    NAME    ycore_contract
    SOURCES contract-test.c
    LIBS    yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)
