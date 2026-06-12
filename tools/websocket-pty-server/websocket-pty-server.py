#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.11"
# dependencies = ["websockets>=12"]
# ///
"""WebSocket PTY server — the host side of yetty's webasm "websocket" mode.

Each WebSocket connection gets its own PTY running a shell. The wire
protocol matches src/yetty/ypty/websocket-pty.{h,c}:

  client -> server (binary frames, first payload byte = message type):
    0x00  input    remaining bytes are keyboard/paste data for the PTY
    0x01  resize   8 payload bytes, all uint16 big-endian:
                   cols, rows, pixel_width, pixel_height

  server -> client: raw PTY output, the whole binary frame is data
                    (no type byte).

Usage:
    ./websocket-pty-server.py                       # 127.0.0.1:8090, $SHELL
    ./websocket-pty-server.py -v                    # + per-frame debug log
    ./websocket-pty-server.py --port 9001
    ./websocket-pty-server.py --host 0.0.0.0 --command /bin/zsh

Then open the webasm build with ?mode=websocket&url=ws://HOST:8090
(or pick "WebSocket shell" in the start dialog).

Debugging "I see nothing in the terminal":
  - every connection logs `connection #N from ...` here. If that line
    never appears, the browser never reached this server (wrong URL,
    wrong port, or the page defaulted to a hostname this server isn't
    bound to — note the default bind is 127.0.0.1).
  - with -v every frame is logged in both directions. A healthy session
    shows `rx resize`, then `tx N bytes` lines (the shell prompt) right
    after the spawn.

Security note: every connection gets a shell with this process's
privileges. Bind to 127.0.0.1 (the default) unless the network is
trusted, and put a TLS-terminating reverse proxy (wss://) in front for
anything non-local.
"""

from __future__ import annotations

import argparse
import asyncio
import fcntl
import logging
import os
import pty
import signal
import struct
import sys
import termios

import websockets

MSG_INPUT = 0x00
MSG_RESIZE = 0x01

READ_CHUNK_SIZE = 65536

log = logging.getLogger("websocket-pty")


def preview(data: bytes, limit: int = 48) -> str:
    """Printable preview of a byte payload for debug logs."""
    shown = data[:limit]
    suffix = f"… (+{len(data) - limit} more)" if len(data) > limit else ""
    return repr(shown) + suffix


def set_pty_window_size(master_fd: int, cols: int, rows: int,
                        pixel_width: int, pixel_height: int) -> None:
    winsize = struct.pack("HHHH", rows, cols, pixel_width, pixel_height)
    fcntl.ioctl(master_fd, termios.TIOCSWINSZ, winsize)


class PtySession:
    """One shell-on-a-PTY, bridged to one WebSocket connection."""

    def __init__(self, command: list[str], connection_id: int):
        self.command = command
        self.connection_id = connection_id
        self.process_pid: int | None = None
        self.master_fd: int | None = None

    def spawn(self) -> None:
        process_pid, master_fd = pty.fork()
        if process_pid == 0:
            # Child: become the shell. TERM is what yetty renders.
            os.environ.setdefault("TERM", "xterm-256color")
            try:
                os.execvp(self.command[0], self.command)
            except OSError as exec_error:
                print(f"exec failed: {exec_error}", file=sys.stderr)
                os._exit(127)
        self.process_pid = process_pid
        self.master_fd = master_fd
        os.set_blocking(master_fd, False)
        log.info("[#%d] spawned pid=%d master_fd=%d command=%s",
                 self.connection_id, process_pid, master_fd,
                 " ".join(self.command))

    def write_input(self, data: bytes) -> None:
        if self.master_fd is None:
            log.warning("[#%d] rx input %d bytes but PTY is gone — dropped",
                        self.connection_id, len(data))
            return
        if data:
            written = os.write(self.master_fd, data)
            log.debug("[#%d] rx input %d bytes -> pty (wrote %d): %s",
                      self.connection_id, len(data), written, preview(data))

    def resize(self, payload: bytes) -> None:
        if self.master_fd is None:
            log.warning("[#%d] rx resize but PTY is gone — dropped",
                        self.connection_id)
            return
        if len(payload) < 8:
            log.warning("[#%d] rx resize with short payload (%d bytes) — dropped",
                        self.connection_id, len(payload))
            return
        cols, rows, pixel_width, pixel_height = struct.unpack(">HHHH", payload[:8])
        if cols == 0 or rows == 0:
            log.warning("[#%d] rx resize %dx%d ignored (zero dimension)",
                        self.connection_id, cols, rows)
            return
        set_pty_window_size(self.master_fd, cols, rows, pixel_width, pixel_height)
        log.info("[#%d] rx resize -> %dx%d (%dx%d px)",
                 self.connection_id, cols, rows, pixel_width, pixel_height)
        if self.process_pid:
            try:
                os.kill(self.process_pid, signal.SIGWINCH)
            except ProcessLookupError:
                log.warning("[#%d] SIGWINCH: pid %d already gone",
                            self.connection_id, self.process_pid)

    def close(self) -> None:
        if self.master_fd is not None:
            try:
                os.close(self.master_fd)
            except OSError:
                pass
            self.master_fd = None
        if self.process_pid is not None:
            exit_status = None
            try:
                os.kill(self.process_pid, signal.SIGHUP)
                _, exit_status = os.waitpid(self.process_pid, os.WNOHANG)
            except (ProcessLookupError, ChildProcessError):
                pass
            log.info("[#%d] closed PTY, pid=%d (wait status: %s)",
                     self.connection_id, self.process_pid, exit_status)
            self.process_pid = None


async def pump_pty_to_websocket(session: PtySession, websocket) -> None:
    """Forward PTY output to the WebSocket until EOF/close."""
    loop = asyncio.get_running_loop()
    master_fd = session.master_fd
    connection_id = session.connection_id
    readable = asyncio.Event()
    loop.add_reader(master_fd, readable.set)
    total_sent = 0
    try:
        while True:
            await readable.wait()
            readable.clear()
            try:
                data = os.read(master_fd, READ_CHUNK_SIZE)
            except BlockingIOError:
                continue
            except OSError as read_error:
                # Usual end-of-session path: the shell exited and the
                # slave side closed (EIO on Linux).
                log.info("[#%d] pty read ended (%s) — shell exited?",
                         connection_id, read_error)
                break
            if not data:
                log.info("[#%d] pty EOF — shell exited", connection_id)
                break
            await websocket.send(data)
            total_sent += len(data)
            log.debug("[#%d] tx %d bytes pty -> ws (total %d): %s",
                      connection_id, len(data), total_sent, preview(data))
    finally:
        loop.remove_reader(master_fd)
        log.info("[#%d] pty->ws pump finished, %d bytes total",
                 connection_id, total_sent)


async def handle_connection(websocket, command: list[str],
                            connection_id: int) -> None:
    peer = websocket.remote_address
    log.info("[#%d] connection from %s", connection_id, peer)
    session = PtySession(command, connection_id)
    session.spawn()

    pty_pump = asyncio.create_task(pump_pty_to_websocket(session, websocket))
    close_reason = "client closed"
    try:
        async for message in websocket:
            if isinstance(message, str):
                log.debug("[#%d] rx TEXT frame (%d chars) — treating as bytes",
                          connection_id, len(message))
                message = message.encode()
            if not message:
                log.debug("[#%d] rx empty frame — ignored", connection_id)
                continue
            message_type, payload = message[0], message[1:]
            if message_type == MSG_INPUT:
                session.write_input(payload)
            elif message_type == MSG_RESIZE:
                session.resize(payload)
            else:
                log.warning("[#%d] rx unknown message type 0x%02x (%d bytes) — dropped",
                            connection_id, message_type, len(message))
            if pty_pump.done():
                close_reason = "shell exited"
                break
    except websockets.ConnectionClosed as closed:
        close_reason = f"ws closed (code={closed.code} reason={closed.reason!r})"
    except Exception:
        log.exception("[#%d] unexpected error in ws receive loop", connection_id)
        close_reason = "internal error"
    finally:
        pty_pump.cancel()
        session.close()
        try:
            await websocket.close()
        except Exception:
            pass
        log.info("[#%d] session for %s ended: %s",
                 connection_id, peer, close_reason)


async def serve(host: str, port: int, command: list[str]) -> None:
    connection_counter = 0

    async def handler(websocket):
        nonlocal connection_counter
        connection_counter += 1
        await handle_connection(websocket, command, connection_counter)

    async with websockets.serve(handler, host, port, max_size=16 * 1024 * 1024):
        log.info("listening on ws://%s:%d (command: %s)",
                 host, port, " ".join(command))
        if host in ("127.0.0.1", "localhost", "::1"):
            log.info("bound to loopback only — browsers on OTHER machines "
                     "can't reach this; use --host 0.0.0.0 for that")
        await asyncio.Future()  # run forever


def main() -> None:
    parser = argparse.ArgumentParser(
        description="WebSocket PTY server for yetty's webasm websocket mode")
    parser.add_argument("--host", default="127.0.0.1",
                        help="listen address (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8090,
                        help="listen port (default: 8090)")
    parser.add_argument("--command", default=os.environ.get("SHELL", "/bin/bash"),
                        help="shell/command to run per connection "
                             "(default: $SHELL)")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="log every frame in both directions "
                             "(input, output, resize) with payload previews")
    args = parser.parse_args()

    # logging handlers flush per record, so the log stays live even when
    # stderr is redirected to a file (plain print() is block-buffered
    # there and looks like the server is silent).
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s.%(msecs)03d %(levelname)-7s %(message)s",
        datefmt="%H:%M:%S",
        stream=sys.stderr,
    )
    # websockets' own DEBUG logging (frame-level) is noisy; keep it at
    # INFO unless someone turns it on by hand.
    logging.getLogger("websockets").setLevel(logging.INFO)

    command = args.command.split()
    try:
        asyncio.run(serve(args.host, args.port, command))
    except KeyboardInterrupt:
        log.info("shutting down")


if __name__ == "__main__":
    main()
