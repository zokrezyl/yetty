# ydiagram unit tests.

enable_testing()

set(YDIAGRAM_TEST_SHIM_SOURCES
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)

add_executable(ydiagram-smoke-test
    ydiagram-smoke-test.c
    ${YDIAGRAM_TEST_SHIM_SOURCES}
)
target_include_directories(ydiagram-smoke-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)
target_link_libraries(ydiagram-smoke-test PRIVATE
    yetty_ydiagram yetty_ypaint_core yetty_ysdf yetty_ycore m)

add_test(NAME ydiagram_smoke COMMAND ydiagram-smoke-test)
