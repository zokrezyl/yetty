#!/usr/bin/env python3
"""
FFI connection-facade smoke test (#439).

Exercises the GENERATED Python reactor-seam facade
(bindings/python/yetty/generated/connection.py) against the real
libyetty_ffi.so: two Connections wired back-to-back over two pipes in one
process, driving the dynamic-channel handshake (open / accept / DATA both
ways / EOF / CLOSE), chunked bulk transfer under flow control, and the
async (asyncio add_reader) driver.

SKIPS (exit 77) when libyetty_ffi.so is absent — the FFI shared object lives
in its own PIC build tree (make build-desktop-ffi-release), which not every
CI lane builds.

Env:
  YETTY_FFI_LIB  path to libyetty_ffi.so (falls back to the ffi build tree)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
LIB = os.environ.get(
    "YETTY_FFI_LIB",
    os.path.join(ROOT, "build-desktop-ffi-release", "src", "yetty", "yffi", "libyetty_ffi.so"))

if not os.path.exists(LIB):
    print(f"SKIP: no FFI library at {LIB} (build with: make build-desktop-ffi-release)")
    sys.exit(77)

sys.path.insert(0, os.path.join(ROOT, "bindings", "python"))

from yetty import runtime  # noqa: E402
from yetty.generated import connection as wire  # noqa: E402

runtime.load(LIB)

FAILURES = []


def check(condition, label):
    print(("ok   " if condition else "FAIL ") + label)
    if not condition:
        FAILURES.append(label)


def make_pair():
    """Two Connections joined by two pipes (initiator, acceptor, pump)."""
    a_to_b = os.pipe()
    b_to_a = os.pipe()
    initiator = wire.Connection(b_to_a[0], a_to_b[1], compressed=False,
                                side_channel_env=False)
    acceptor = wire.Connection(a_to_b[0], b_to_a[1], compressed=False,
                               side_channel_env=False)
    acceptor.set_role(True)

    def pump():
        for _spin in range(10000):
            moved = 0
            moved += initiator.pump_writable().value or 0
            moved += acceptor.pump_readable().value or 0
            moved += acceptor.pump_writable().value or 0
            moved += initiator.pump_readable().value or 0
            if moved == 0:
                return
        raise AssertionError("pump did not settle")

    fds = list(a_to_b) + list(b_to_a)
    return initiator, acceptor, pump, fds


def test_dynamic_channel_lifecycle():
    initiator, acceptor, pump, fds = make_pair()
    try:
        accepted = []
        received = []
        events = []

        def on_accept(channel):
            channel.set_raw_sink(lambda data: received.append(data))
            channel.set_event_cb(lambda _chan, event: events.append(event))
            accepted.append(channel)
            return True

        acceptor.set_accept_cb(on_accept)

        opened = initiator.open_channel()
        check(opened.error is None, "open_channel returns a channel")
        channel = opened.value
        pump()
        check(len(accepted) == 1, "accept callback fired once")
        check(accepted[0].id() == channel.id() == wire.CHANNEL_DYNAMIC_BASE,
              "peer channel ids agree at DYNAMIC_BASE")

        channel.write(b"hello dynamic")
        channel.flush()
        pump()
        check(b"".join(received) == b"hello dynamic", "DATA initiator -> acceptor")

        accepted[0].write(b"echo back")
        accepted[0].flush()
        pump()
        reply = channel.read().value
        check(reply == b"echo back", "DATA acceptor -> initiator")

        channel.send_eof()
        pump()
        check(events == [wire.EVENT_REMOTE_EOF], "EOF surfaced as REMOTE_EOF event")
        check(accepted[0].remote_eof(), "remote_eof() reads true after EOF")

        channel.close()
        pump()
        check(wire.EVENT_CLOSED in events, "CLOSE surfaced as CLOSED event")
        check(initiator.channel(wire.CHANNEL_DYNAMIC_BASE) is None,
              "initiator slot released after close handshake")
        check(acceptor.channel(wire.CHANNEL_DYNAMIC_BASE) is None,
              "acceptor slot released after close handshake")
    finally:
        initiator.close()
        acceptor.close()
        for fd in fds:
            os.close(fd)


def test_bulk_transfer_flow_control():
    initiator, acceptor, pump, fds = make_pair()
    try:
        accepted = []
        # No sink on the accepted channel: a PULL consumer, so inbound bytes
        # buffer and credit is granted only as read() drains — that is what
        # makes the window observably run dry (a push sink consumes eagerly
        # and refills credit inside the pump itself).
        acceptor.set_accept_cb(lambda channel: accepted.append(channel) or True)
        channel = initiator.open_channel().value
        pump()

        payload = bytes((i * 131 + 7) & 0xFF for i in range(wire.WINDOW_DEFAULT + 40000))
        channel.write(payload)
        channel.flush()
        pump()
        # The window ran dry with the tail still pending locally.
        check(channel.send_window() == 0, "send window exhausted at WINDOW_DEFAULT")
        check(channel.pending_out() == len(payload) - wire.WINDOW_DEFAULT,
              "tail beyond the window stayed pending")

        # Drain; each read grants credit back, unblocking the tail.
        assembled = bytearray()
        for _spin in range(10000):
            data = accepted[0].read().value
            assembled.extend(data)
            pump()
            if len(assembled) == len(payload):
                break
        check(bytes(assembled) == payload, "bulk payload reassembled in order")
        check(channel.pending_out() == 0, "pending drained after credit returned")
    finally:
        initiator.close()
        acceptor.close()
        for fd in fds:
            os.close(fd)


def test_async_driver():
    import asyncio

    initiator, acceptor, pump, fds = make_pair()
    try:
        received = []

        def on_accept(channel):
            channel.set_raw_sink(lambda data: received.append(data))
            return True

        acceptor.set_accept_cb(on_accept)
        channel = initiator.open_channel().value
        channel.write(b"ping over asyncio")
        channel.flush()

        async def drive():
            # The acceptor rides run_async (host loop owns its fd); the
            # initiator is pumped from the tick callback.
            def tick():
                initiator.pump_writable()
                initiator.pump_readable()

            await acceptor.run_async(on_tick=tick, tick=0.005,
                                     should_stop=lambda: bool(received))

        asyncio.run(asyncio.wait_for(drive(), timeout=10))
        check(b"".join(received) == b"ping over asyncio",
              "run_async delivered DATA via the host asyncio loop")
    finally:
        initiator.close()
        acceptor.close()
        for fd in fds:
            os.close(fd)


def main():
    test_dynamic_channel_lifecycle()
    test_bulk_transfer_flow_control()
    test_async_driver()
    if FAILURES:
        print(f"{len(FAILURES)} check(s) failed")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
