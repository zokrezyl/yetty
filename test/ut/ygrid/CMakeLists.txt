# ygrid unit tests
#
# Wire-driven receiver tests: feed ydraw command bytes through
# ygrid_process_bytes and assert entity / prim state via the ygrid
# ops->dump.

enable_testing()

set(YGRID_TEST_SHIM_SOURCES
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)

add_executable(ygrid-wire-test
    ygrid-wire-test.c
    ${YGRID_TEST_SHIM_SOURCES}
)
target_include_directories(ygrid-wire-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)
target_link_libraries(ygrid-wire-test PRIVATE
    yetty_ygrid
    yetty_ydraw_factory
    yetty_ydraw_core
    yetty_ysdf
    yetty_yfigure
    yetty_ycore
    m
)
add_test(NAME ygrid_wire COMMAND ygrid-wire-test)
