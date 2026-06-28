# yfigure unit tests
#
# Receiver-side tests: drive the container through the typed yclass slots
# (yetty_yfigure_create_child / _set_child_rect / _apply_child_body /
# _delete_child / _clear_all), locally and over a real yclass RPC session,
# and assert state via yetty_yfigure_dump.

enable_testing()

# Threads for the RPC round-trip test (server loop on one socketpair end).
find_package(Threads REQUIRED)

set(YFIGURE_TEST_SHIM_SOURCES
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)

add_executable(yfigure-wire-test
    yfigure-wire-test.c
    ${YFIGURE_TEST_SHIM_SOURCES}
)
target_include_directories(yfigure-wire-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)
target_link_libraries(yfigure-wire-test PRIVATE
    yetty_yfigure
    yetty_yclass
    yetty_ydraw_core
    yetty_ycore
    Threads::Threads
    m
)
add_test(NAME yfigure_wire COMMAND yfigure-wire-test)
