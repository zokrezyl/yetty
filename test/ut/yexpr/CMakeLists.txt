# yexpr expression-parser unit tests
#
# Pure grammar/AST contract tests: operator precedence, right-associative power,
# unary negation, parentheses, function calls (+ arg-count limit), buffer refs,
# and malformed-input rejection. No GPU/I/O/context.

yetty_add_c_test(
    NAME    yexpr_parse
    SOURCES parse-test.c
    LIBS    yetty_yexpr yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract parser
    TIMEOUT 30)
