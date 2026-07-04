"""GENERATED reactor-seam facade — do not edit.

The multiplexed wire connection (yetty_ywire_connection over
yetty_yclass_transport_pty): the sole owner of the terminal byte stream,
demuxing rpc / input / raw / dynamic channels. All the dangerous byte handling
— framing, raw mode, flow control, chunking — lives in C; this facade only
registers the connection fd with a loop and calls pump() on readiness. No
frame parsing on the Python side.

Two drivers, per the endpoint contract:
  - sync:   Connection.run_forever(...) — a selectors loop for loop-less apps.
  - async:  await Connection.run_async(...) — add_reader/add_writer on the
            running asyncio loop; the host loop owns the fd.
"""

from __future__ import annotations

import ctypes as C
import sys
from typing import Callable

from .. import runtime as _rt
from . import _types as _t

# Well-known channel ids + protocol constants (include/yetty/ywire/channel.h).
CHANNEL_RPC = 1
CHANNEL_INPUT = 2
CHANNEL_RAW = 3
CHANNEL_DYNAMIC_BASE = 16
WINDOW_DEFAULT = 256 * 1024
CHUNK_MAX = 16 * 1024

# enum yetty_ywire_channel_event
EVENT_REMOTE_EOF = 1
EVENT_CLOSED = 2

ENVELOPE_SINK = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_void_p, C.c_size_t,
                            C.c_void_p, C.c_size_t)
RAW_SINK = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p, C.c_size_t)
RESIZE_CB = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_int)
EVENT_CB = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p, C.c_int)
ACCEPT_CB = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p)


class _PtrResultUnion(C.Union):
    _fields_ = [("value", C.c_void_p), ("error", _t.yetty_ycore_error)]


class _PtrResult(C.Structure):
    _anonymous_ = ("_anon",)
    _fields_ = [("ok", C.c_int), ("_anon", _PtrResultUnion)]


class _Reactor(C.Structure):
    """struct yetty_yclass_transport_reactor — {userdata, ops}, by value."""

    _fields_ = [("userdata", C.c_void_p), ("ops", C.c_void_p)]


def _cresult(name, restype, argtypes, *args, convert=None):
    return _rt.result_from_c(_rt.cfn(name, restype, list(argtypes))(*args), convert)


def _cvoid(name, argtypes, *args):
    return _cresult(name, _t.yetty_ycore_void_result, argtypes, *args)


def _raw(name, restype, argtypes, *args):
    return _rt.cfn(name, restype, list(argtypes))(*args)


class Channel:
    """One logical lane of a Connection (SSH's "channel"): an independent,
    ordered byte sub-stream. Sinks fire during the connection's pump."""

    def __init__(self, connection: "Connection", pointer):
        self._connection = connection
        self._pointer = pointer
        self._env_trampoline = None
        self._raw_trampoline = None
        self._event_trampoline = None

    @property
    def pointer(self):
        return self._pointer

    def id(self) -> int:
        return _raw("yetty_ywire_channel_id", C.c_uint, [C.c_void_p], self._pointer)

    def write(self, data: bytes) -> _rt.Result:
        buf = C.create_string_buffer(data, len(data))
        return _cresult("yetty_ywire_channel_write", _t.yetty_ycore_size_result,
                        (C.c_void_p, C.c_void_p, C.c_size_t), self._pointer, buf, len(data))

    def flush(self) -> _rt.Result:
        return _cvoid("yetty_ywire_channel_flush", (C.c_void_p,), self._pointer)

    def read(self, max_bytes: int = 65536) -> _rt.Result:
        buf = C.create_string_buffer(max_bytes)
        res = _cresult("yetty_ywire_channel_read", _t.yetty_ycore_size_result,
                       (C.c_void_p, C.c_void_p, C.c_size_t), self._pointer, buf, max_bytes)
        if res.error is not None:
            return res
        return _rt.Result(value=buf.raw[: res.value or 0])

    def recv_blocking(self, max_bytes: int = 65536) -> _rt.Result:
        buf = C.create_string_buffer(max_bytes)
        res = _cresult("yetty_ywire_channel_recv_blocking", _t.yetty_ycore_size_result,
                       (C.c_void_p, C.c_void_p, C.c_size_t), self._pointer, buf, max_bytes)
        if res.error is not None:
            return res
        return _rt.Result(value=buf.raw[: res.value or 0])

    def transport(self) -> _rt.Result:
        """A heap yetty_yclass_transport riding this channel — hand it to an
        RPC session / framework attach, which takes ownership."""
        return _cresult("yetty_ywire_channel_transport", _PtrResult, (C.c_void_p,),
                        self._pointer)

    def set_envelope_sink(self, sink: Callable[[int, bytes, bytes], None]) -> _rt.Result:
        def trampoline(_user, wire_code, args, args_len, payload, payload_len):
            args_bytes = C.string_at(args, args_len) if args and args_len else b""
            payload_bytes = C.string_at(payload, payload_len) if payload and payload_len else b""
            sink(wire_code, args_bytes, payload_bytes)

        self._env_trampoline = ENVELOPE_SINK(trampoline)
        return _cvoid("yetty_ywire_channel_set_envelope_sink",
                      (C.c_void_p, ENVELOPE_SINK, C.c_void_p),
                      self._pointer, self._env_trampoline, None)

    def set_raw_sink(self, sink: Callable[[bytes], None]) -> _rt.Result:
        def trampoline(_user, data, n):
            sink(C.string_at(data, n) if data and n else b"")

        self._raw_trampoline = RAW_SINK(trampoline)
        return _cvoid("yetty_ywire_channel_set_raw_sink", (C.c_void_p, RAW_SINK, C.c_void_p),
                      self._pointer, self._raw_trampoline, None)

    def set_event_cb(self, callback: Callable[["Channel", int], None]) -> _rt.Result:
        def trampoline(_user, _channel_ptr, event):
            callback(self, event)

        self._event_trampoline = EVENT_CB(trampoline)
        return _cvoid("yetty_ywire_channel_set_event_cb", (C.c_void_p, EVENT_CB, C.c_void_p),
                      self._pointer, self._event_trampoline, None)

    def send_eof(self) -> _rt.Result:
        return _cvoid("yetty_ywire_channel_send_eof", (C.c_void_p,), self._pointer)

    def close(self) -> _rt.Result:
        return _cvoid("yetty_ywire_channel_close", (C.c_void_p,), self._pointer)

    def send_window(self) -> int:
        return _raw("yetty_ywire_channel_send_window", C.c_int64, [C.c_void_p], self._pointer)

    def remote_eof(self) -> bool:
        return bool(_raw("yetty_ywire_channel_remote_eof", C.c_int, [C.c_void_p], self._pointer))

    def pending_out(self) -> int:
        return _raw("yetty_ywire_channel_pending_out", C.c_size_t, [C.c_void_p], self._pointer)


class Connection:
    """Sole owner of the terminal byte stream and the channel mux over it.

    Owns a yetty_yclass_transport_pty (raw mode, ordered non-blocking writer)
    — or the side-channel fd pair when YETTY_YWIRE_SIDE_CHANNEL is set — and a
    yetty_ywire_connection on top. Register fd() with any loop and call
    pump_readable()/pump_writable() on readiness, or use the bundled
    run_forever()/run_async() drivers.
    """

    def __init__(self, in_fd: int | None = None, out_fd: int | None = None, *,
                 compressed: bool = True, raw_mode: bool = True,
                 side_channel_env: bool = True):
        self._transport = None
        self._pointer = None
        self._channels: dict[int, Channel] = {}
        self._resize_trampoline = None
        self._accept_trampoline = None
        self._closed = False

        in_fd = in_fd if in_fd is not None else sys.stdin.fileno()
        out_fd = out_fd if out_fd is not None else sys.stdout.fileno()
        creator = ("yetty_yclass_transport_pty_create_from_env" if side_channel_env
                   else "yetty_yclass_transport_pty_create")
        try:
            transport_res = _cresult(creator, _PtrResult, (C.c_int, C.c_int), in_fd, out_fd)
            if transport_res.error is not None:
                raise _rt.YettyError(transport_res.error.message)
            self._transport = transport_res.value
            if raw_mode:
                raw_res = _cvoid("yetty_yclass_transport_pty_enable_raw_mode", (C.c_void_p,),
                                 self._transport)
                if raw_res.error is not None:
                    raise _rt.YettyError(raw_res.error.message)
            reactor = _raw("yetty_yclass_transport_pty_reactor", _Reactor, [C.c_void_p],
                           self._transport)
            conn_res = _cresult("yetty_ywire_connection_create", _PtrResult,
                                (_Reactor, C.c_int), reactor, 1 if compressed else 0)
            if conn_res.error is not None:
                raise _rt.YettyError(conn_res.error.message)
            self._pointer = conn_res.value
        except BaseException:
            # Raw mode may already be applied — restore by tearing down.
            self.close()
            raise

    # --- channels ------------------------------------------------------------

    def channel(self, channel_id: int) -> Channel | None:
        pointer = _raw("yetty_ywire_connection_channel", C.c_void_p, [C.c_void_p, C.c_uint],
                       self._pointer, channel_id)
        if not pointer:
            self._channels.pop(channel_id, None)
            return None
        cached = self._channels.get(channel_id)
        if cached is None or cached.pointer != pointer:
            cached = Channel(self, pointer)
            self._channels[channel_id] = cached
        return cached

    @property
    def rpc(self) -> Channel:
        return self.channel(CHANNEL_RPC)

    @property
    def input(self) -> Channel:
        return self.channel(CHANNEL_INPUT)

    @property
    def raw(self) -> Channel:
        return self.channel(CHANNEL_RAW)

    def open_channel(self, initial_recv_window: int = 0) -> _rt.Result:
        res = _cresult("yetty_ywire_connection_open_channel", _PtrResult,
                       (C.c_void_p, C.c_uint), self._pointer, initial_recv_window)
        if res.error is not None:
            return res
        channel = Channel(self, res.value)
        self._channels[channel.id()] = channel
        return _rt.Result(value=channel)

    def set_role(self, acceptor: bool) -> _rt.Result:
        return _cvoid("yetty_ywire_connection_set_role", (C.c_void_p, C.c_int),
                      self._pointer, 1 if acceptor else 0)

    def set_accept_cb(self, callback: Callable[[Channel], bool]) -> _rt.Result:
        def trampoline(_user, channel_ptr):
            channel = Channel(self, channel_ptr)
            accepted = bool(callback(channel))
            if accepted:
                self._channels[channel.id()] = channel
            return 1 if accepted else 0

        self._accept_trampoline = ACCEPT_CB(trampoline)
        return _cvoid("yetty_ywire_connection_set_accept_cb",
                      (C.c_void_p, ACCEPT_CB, C.c_void_p),
                      self._pointer, self._accept_trampoline, None)

    # --- reactor seam ----------------------------------------------------------

    def fd(self) -> int:
        return _raw("yetty_ywire_connection_fd", C.c_int, [C.c_void_p], self._pointer)

    def out_fd(self) -> int:
        return _raw("yetty_ywire_connection_out_fd", C.c_int, [C.c_void_p], self._pointer)

    def want_write(self) -> bool:
        return bool(_raw("yetty_ywire_connection_want_write", C.c_int, [C.c_void_p],
                         self._pointer))

    def is_eof(self) -> bool:
        return bool(_raw("yetty_ywire_connection_is_eof", C.c_int, [C.c_void_p], self._pointer))

    def pump_readable(self) -> _rt.Result:
        return _cresult("yetty_ywire_connection_pump_readable", _t.yetty_ycore_size_result,
                        (C.c_void_p,), self._pointer)

    def pump_writable(self) -> _rt.Result:
        return _cresult("yetty_ywire_connection_pump_writable", _t.yetty_ycore_size_result,
                        (C.c_void_p,), self._pointer)

    def set_resize_cb(self, callback: Callable[[int, int, int, int], None]) -> _rt.Result:
        def trampoline(_user, width_px, height_px, cols, rows):
            callback(width_px, height_px, cols, rows)

        self._resize_trampoline = RESIZE_CB(trampoline)
        return _cvoid("yetty_ywire_connection_set_resize_cb",
                      (C.c_void_p, RESIZE_CB, C.c_void_p),
                      self._pointer, self._resize_trampoline, None)

    def pickup_winsize(self) -> _rt.Result:
        return _cvoid("yetty_ywire_connection_pickup_winsize", (C.c_void_p,), self._pointer)

    # --- drivers ---------------------------------------------------------------

    def run_forever(self, on_tick: Callable[[], None] | None = None,
                    should_stop: Callable[[], bool] | None = None,
                    tick: float = 0.033) -> int:
        """Sync facade for loop-less callers: a selectors loop over fd() plus a
        periodic tick (ship dirty frames there)."""
        import selectors

        selector = selectors.DefaultSelector()
        selector.register(self.fd(), selectors.EVENT_READ)
        try:
            while True:
                for _key, _mask in selector.select(timeout=tick):
                    self.pump_readable()
                if on_tick is not None:
                    on_tick()
                self.pump_writable()
                if self.is_eof() or (should_stop is not None and should_stop()):
                    return 0
        except KeyboardInterrupt:
            return 0
        finally:
            selector.close()

    async def run_async(self, on_tick: Callable[[], None] | None = None,
                        should_stop: Callable[[], bool] | None = None,
                        tick: float = 0.033) -> int:
        """Async facade: the running asyncio loop owns fd(); pump on readiness,
        tick periodically. The C connection owns the bytes."""
        import asyncio

        loop = asyncio.get_running_loop()
        fd = self.fd()
        done: asyncio.Future = loop.create_future()

        def finish() -> None:
            if not done.done():
                done.set_result(0)

        def on_readable() -> None:
            self.pump_readable()
            if on_tick is not None:
                on_tick()
            self.pump_writable()
            if self.is_eof() or (should_stop is not None and should_stop()):
                finish()

        async def ticker() -> None:
            try:
                while not done.done():
                    if on_tick is not None:
                        on_tick()
                    self.pump_writable()
                    if self.is_eof() or (should_stop is not None and should_stop()):
                        finish()
                        break
                    await asyncio.sleep(tick)
            except asyncio.CancelledError:
                pass

        loop.add_reader(fd, on_readable)
        ticker_task = loop.create_task(ticker())
        try:
            await done
        finally:
            loop.remove_reader(fd)
            ticker_task.cancel()
        return 0

    # --- lifecycle ---------------------------------------------------------------

    def close(self) -> None:
        """Destroy connection then transport (restores raw mode). Idempotent."""
        if self._closed:
            return
        self._closed = True
        if self._pointer:
            _cvoid("yetty_ywire_connection_destroy", (C.c_void_p,), self._pointer)
            self._pointer = None
        if self._transport:
            _cvoid("yetty_yclass_transport_pty_flush_blocking", (C.c_void_p,), self._transport)
            _cvoid("yetty_yclass_transport_pty_destroy", (C.c_void_p,), self._transport)
            self._transport = None
        self._channels.clear()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False
