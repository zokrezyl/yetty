# yfigure unit tests
#
# Receiver-side tests: drive the container through the typed yclass slots
# (yetty_yfigure_create_child / _set_child_rect / _apply_child_body /
# _delete_child / _clear_all), locally and over a real yclass RPC session,
# and assert state via yetty_yfigure_dump. The RPC round-trip runs a server
# loop on one socketpair end, hence Threads::Threads.

yetty_add_c_test(
    NAME    yfigure_wire
    SOURCES yfigure-wire-test.c
    LIBS    yetty_yfigure yetty_yclass yetty_ydraw_core yetty_ycore Threads::Threads
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire
    TIMEOUT 30)

# Container z-order / hit-test / dirty lifecycle (complements the wire test).
yetty_add_c_test(
    NAME    yfigure_container
    SOURCES container-test.c
    LIBS    yetty_yfigure yetty_yclass yetty_ydraw_core yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)
