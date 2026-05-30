# ymarkdown unit tests.

enable_testing()

set(YMARKDOWN_TEST_SHIM_SOURCES
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)

add_executable(ymarkdown-smoke-test
    ymarkdown-smoke-test.c
    ${YMARKDOWN_TEST_SHIM_SOURCES}
)
target_include_directories(ymarkdown-smoke-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)
target_link_libraries(ymarkdown-smoke-test PRIVATE
    yetty_ymarkdown yetty_ydraw_core yetty_ysdf yetty_ycore m)

add_test(NAME ymarkdown_smoke COMMAND ymarkdown-smoke-test)
