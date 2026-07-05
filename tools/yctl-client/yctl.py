#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.11"
# dependencies = ["msgpack", "click", "pyyaml"]
# ///
"""
Async RPC client for yetty terminal.

Protocol: msgpack-rpc over TCP
- Notification: [2, channel, method, params]
- Request:      [0, msgid, channel, method, params]
- Response:     [1, msgid, error, result]

Channels:
- 0: EventLoop (keyboard, mouse, window events)
- 1: CardStream (card buffer/texture streaming)
"""

from __future__ import annotations

import asyncio
import os
from dataclasses import dataclass
from enum import IntEnum
from typing import Any

import msgpack


class Channel(IntEnum):
    EventLoop = 0
    CardStream = 1


class MessageType(IntEnum):
    Request = 0
    Response = 1
    Notification = 2


@dataclass
class RpcResponse:
    msgid: int
    error: Any
    result: Any


class RpcClient:
    """Async RPC client for yetty terminal."""

    def __init__(self, host: str = "127.0.0.1", port: int = 9999):
        self.host = host
        self.port = port
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._msgid = 0
        self._pending: dict[int, asyncio.Future[RpcResponse]] = {}
        self._recv_task: asyncio.Task | None = None
        self._unpacker = msgpack.Unpacker(raw=False)
        self._connected = False

    async def connect(self) -> None:
        """Connect to the yetty RPC server."""
        self._reader, self._writer = await asyncio.open_connection(self.host, self.port)
        self._connected = True
        self._recv_task = asyncio.create_task(self._recv_loop())

    async def disconnect(self) -> None:
        """Disconnect from the server."""
        if self._recv_task:
            self._recv_task.cancel()
            try:
                await self._recv_task
            except asyncio.CancelledError:
                pass
            self._recv_task = None
        if self._writer:
            self._writer.close()
            await self._writer.wait_closed()
            self._writer = None
            self._reader = None
        self._connected = False

    async def __aenter__(self) -> RpcClient:
        await self.connect()
        return self

    async def __aexit__(self, *args) -> None:
        await self.disconnect()

    def _next_msgid(self) -> int:
        self._msgid += 1
        return self._msgid

    async def _recv_loop(self) -> None:
        """Background task to receive and dispatch responses."""
        assert self._reader is not None
        try:
            while True:
                data = await self._reader.read(65536)
                if not data:
                    break
                self._unpacker.feed(data)
                for msg in self._unpacker:
                    self._handle_message(msg)
        except asyncio.CancelledError:
            pass
        except Exception as e:
            for fut in self._pending.values():
                if not fut.done():
                    fut.set_exception(e)
            self._pending.clear()

    def _handle_message(self, msg: list) -> None:
        """Handle an incoming message."""
        if not isinstance(msg, list) or len(msg) < 3:
            return
        msg_type = msg[0]
        if msg_type == MessageType.Response:
            if len(msg) < 4:
                return
            msgid = msg[1]
            error = msg[2]
            result = msg[3]
            fut = self._pending.pop(msgid, None)
            if fut and not fut.done():
                fut.set_result(RpcResponse(msgid, error, result))

    async def notify(self, channel: Channel, method: str, params: dict) -> None:
        """Send a notification (no response expected)."""
        if not self._connected:
            raise ConnectionError("Not connected")
        msg = [MessageType.Notification, int(channel), method, params]
        data = msgpack.packb(msg, use_bin_type=True)
        assert self._writer is not None
        self._writer.write(data)
        await self._writer.drain()

    async def request(self, channel: Channel, method: str, params: dict) -> Any:
        """Send a request and wait for response."""
        if not self._connected:
            raise ConnectionError("Not connected")
        msgid = self._next_msgid()
        msg = [MessageType.Request, msgid, int(channel), method, params]
        data = msgpack.packb(msg, use_bin_type=True)

        fut: asyncio.Future[RpcResponse] = asyncio.get_event_loop().create_future()
        self._pending[msgid] = fut

        assert self._writer is not None
        self._writer.write(data)
        await self._writer.drain()

        response = await fut
        if response.error is not None:
            raise RuntimeError(f"RPC error: {response.error}")
        return response.result

    # --- EventLoop Channel Methods ---

    async def key_down(self, key: int, mods: int = 0, scancode: int = 0) -> None:
        """Send key down event."""
        await self.notify(Channel.EventLoop, "key_down", {
            "key": key, "mods": mods, "scancode": scancode
        })

    async def key_up(self, key: int, mods: int = 0, scancode: int = 0) -> None:
        """Send key up event."""
        await self.notify(Channel.EventLoop, "key_up", {
            "key": key, "mods": mods, "scancode": scancode
        })

    async def char_input(self, codepoint: int, mods: int = 0) -> None:
        """Send character input event."""
        await self.notify(Channel.EventLoop, "char", {
            "codepoint": codepoint, "mods": mods
        })

    async def mouse_down(self, x: float, y: float, button: int) -> None:
        """Send mouse button down event."""
        await self.notify(Channel.EventLoop, "mouse_down", {
            "x": x, "y": y, "button": button
        })

    async def mouse_up(self, x: float, y: float, button: int) -> None:
        """Send mouse button up event."""
        await self.notify(Channel.EventLoop, "mouse_up", {
            "x": x, "y": y, "button": button
        })

    async def mouse_move(self, x: float, y: float) -> None:
        """Send mouse move event."""
        await self.notify(Channel.EventLoop, "mouse_move", {"x": x, "y": y})

    async def mouse_scroll(self, x: float, y: float, dx: float, dy: float, mods: int = 0) -> None:
        """Send mouse-wheel scroll event (cursor x/y + wheel dx/dy ticks)."""
        await self.notify(Channel.EventLoop, "mouse_scroll", {
            "x": x, "y": y, "dx": dx, "dy": dy, "mods": mods
        })

    async def resize(self, width: float, height: float) -> None:
        """Resize the window."""
        await self.notify(Channel.EventLoop, "resize", {
            "width": width, "height": height
        })

    async def shutdown(self) -> None:
        """Tell yetty to stop its event loop and exit cleanly."""
        await self.notify(Channel.EventLoop, "shutdown", {})

    async def screenshot(self, path: str = "") -> None:
        """Capture the current frame to a PPM file (empty path = default)."""
        await self.notify(Channel.EventLoop, "screenshot", {"path": path})

    async def ui_tree(self) -> str:
        """Get UI tree (request, returns string)."""
        return await self.request(Channel.EventLoop, "ui_tree", {})

    async def memtags(self) -> str:
        """Dump per-owner allocation accounting (request, returns text table)."""
        return await self.request(Channel.EventLoop, "memtags", {})

    # --- High-Level Commands ---

    async def type_text(self, text: str, delay: float = 0.01) -> None:
        """Type text by sending character events."""
        for char in text:
            await self.char_input(ord(char))
            if delay > 0:
                await asyncio.sleep(delay)

    async def run_command(self, command: str, delay: float = 0.01) -> None:
        """Type a command and press Enter."""
        await self.type_text(command, delay)
        await self.char_input(ord('\n'))

    async def press_key(self, key: int, mods: int = 0) -> None:
        """Press and release a key."""
        await self.key_down(key, mods)
        await self.key_up(key, mods)


# --- GLFW Key Codes ---

class Keys:
    """GLFW key codes."""
    SPACE = 32
    ENTER = 257
    TAB = 258
    BACKSPACE = 259
    ESCAPE = 256
    DELETE = 261
    RIGHT = 262
    LEFT = 263
    DOWN = 264
    UP = 265
    PAGE_UP = 266
    PAGE_DOWN = 267
    HOME = 268
    END = 269
    F1 = 290
    F12 = 301


class Mods:
    """GLFW modifier flags."""
    SHIFT = 0x0001
    CONTROL = 0x0002
    ALT = 0x0004
    SUPER = 0x0008


# --- Script runner (YAML scenarios) ---

import random


_SPECIAL_CHARS = {
    "ENTER": Keys.ENTER,
    "TAB": Keys.TAB,
    "BACKSPACE": Keys.BACKSPACE,
    "ESCAPE": Keys.ESCAPE,
}

_DEFAULT_TYPING = {
    "avg-speed": 5.0,        # chars/second
    "speed-deviation": 0.2,  # fraction of mean delay (0.2 = ±20%)
    "avg-typo": 0.0,         # probability per character [0..1]
}


def _merge_typing(defaults: dict, override: dict | None) -> dict:
    """Per-step typing settings = defaults overridden by step-level keys."""
    merged = dict(defaults)
    if override:
        for k in ("avg-speed", "speed-deviation", "avg-typo"):
            if k in override:
                merged[k] = override[k]
    return merged


def _char_delay(speed: float, deviation: float, rng: random.Random) -> float:
    """Sample one inter-character delay (seconds) from the typing settings.

    Mean = 1/speed. Jitter is uniform within ±(deviation * mean). Clamped
    to ≥ 0 so a deviation > 1 can't yield negative delays."""
    mean = 1.0 / max(speed, 0.001)
    jitter = mean * deviation
    return max(0.0, mean + rng.uniform(-jitter, jitter))


def _typo_char(target: str, rng: random.Random) -> str:
    """Pick a wrong-but-plausible character to stand in for `target`.

    Simple model: random lowercase a-z, retried up to a few times to avoid
    matching the target. Falls back to 'x' on the (vanishingly rare) case
    of repeated collisions."""
    for _ in range(5):
        c = chr(rng.randrange(ord('a'), ord('z') + 1))
        if c.lower() != target.lower():
            return c
    return 'x'


def _mods_from_dict(d: dict) -> int:
    """Translate {ctrl, shift, alt} flags into a GLFW modifier mask."""
    mods = 0
    if d.get("ctrl"):
        mods |= Mods.CONTROL
    if d.get("shift"):
        mods |= Mods.SHIFT
    if d.get("alt"):
        mods |= Mods.ALT
    return mods


async def _play_type(client: RpcClient, text: str, typing: dict,
                     rng: random.Random) -> None:
    """Type `text` one char at a time with timing + typo simulation per
    `typing` (avg-speed, speed-deviation, avg-typo)."""
    speed = float(typing["avg-speed"])
    deviation = float(typing["speed-deviation"])
    typo_prob = float(typing["avg-typo"])

    for ch in text:
        # Typo simulation only on visible characters; whitespace gets typed
        # straight through to keep the timing of long pauses (newlines in
        # multiline `text:` blocks) predictable.
        if ch.isprintable() and not ch.isspace() and rng.random() < typo_prob:
            wrong = _typo_char(ch, rng)
            await client.char_input(ord(wrong))
            await asyncio.sleep(_char_delay(speed, deviation, rng))
            await client.press_key(Keys.BACKSPACE)
            await asyncio.sleep(_char_delay(speed, deviation, rng))

        await client.char_input(ord(ch))
        await asyncio.sleep(_char_delay(speed, deviation, rng))


async def _play_step(client: RpcClient, step, defaults: dict,
                     rng: random.Random) -> None:
    """Dispatch a single script step. See SCRIPT.md / module docs for the
    YAML grammar — every step is either a bare scalar (currently unused;
    we kept all step types as mappings for now) or a single-key mapping."""
    if not isinstance(step, dict) or len(step) != 1:
        raise ValueError(f"each script step must be a single-key mapping, got: {step!r}")

    (verb, payload), = step.items()

    if verb == "sleep":
        await asyncio.sleep(float(payload))
        return

    if verb == "char":
        # Two forms: scalar 'c' (plain char) or {char, ctrl, shift, alt}.
        if isinstance(payload, str):
            if len(payload) != 1:
                raise ValueError(f"char: scalar form expects a single character, got {payload!r}")
            await client.char_input(ord(payload))
            return
        if isinstance(payload, dict):
            ch = payload.get("char")
            if not isinstance(ch, str) or len(ch) != 1:
                raise ValueError(f"char: dict form requires single-char `char` field, got {payload!r}")
            await client.char_input(ord(ch), _mods_from_dict(payload))
            return
        raise ValueError(f"char: unsupported payload type: {payload!r}")

    if verb == "special-char":
        name = str(payload).upper()
        key = _SPECIAL_CHARS.get(name)
        if key is None:
            raise ValueError(f"special-char: unknown name {name!r} (allowed: {sorted(_SPECIAL_CHARS)})")
        await client.press_key(key)
        return

    if verb == "type":
        if isinstance(payload, str):
            await _play_type(client, payload, defaults, rng)
            return
        if isinstance(payload, dict):
            text = payload.get("text", "")
            typing = _merge_typing(defaults, payload)
            await _play_type(client, text, typing, rng)
            return
        raise ValueError(f"type: unsupported payload type: {payload!r}")

    if verb == "mouse-move":
        if not isinstance(payload, dict):
            raise ValueError(f"mouse-move: expected mapping {{x, y}}, got {payload!r}")
        await client.mouse_move(float(payload["x"]), float(payload["y"]))
        return

    if verb in ("mouse-down", "mouse-up"):
        if not isinstance(payload, dict):
            raise ValueError(f"{verb}: expected mapping {{x, y, button?}}, got {payload!r}")
        x = float(payload["x"])
        y = float(payload["y"])
        # button: 0=left, 1=middle, 2=right (matches GLFW mouse-button codes
        # used elsewhere in the RPC). Default to left for ergonomics.
        button = int(payload.get("button", 0))
        if verb == "mouse-down":
            await client.mouse_down(x, y, button)
        else:
            await client.mouse_up(x, y, button)
        return

    if verb == "mouse-scroll":
        if not isinstance(payload, dict):
            raise ValueError(f"mouse-scroll: expected mapping {{x, y, dx?, dy?, mods?}}, got {payload!r}")
        x = float(payload["x"])
        y = float(payload["y"])
        # dx / dy are wheel ticks (positive = up/right, matching GLFW). At
        # least one of them is needed for the event to do anything; we
        # don't enforce it here so callers can send a zero-tick event if
        # they have a reason to.
        dx = float(payload.get("dx", 0))
        dy = float(payload.get("dy", 0))
        mods = int(payload.get("mods", 0))
        await client.mouse_scroll(x, y, dx, dy, mods)
        return

    if verb == "shutdown":
        # Payload is ignored. Use as the last step in a script when you
        # want yetty (and any --record video output) to wind down cleanly.
        await client.shutdown()
        return

    raise ValueError(f"unknown script verb: {verb!r}")


async def play_script(client: RpcClient, doc: dict) -> None:
    """Execute a parsed YAML script document against an open RpcClient."""
    cfg = doc.get("config") or {}
    defaults = _merge_typing(_DEFAULT_TYPING,
                             (cfg.get("defaults") or {}).get("typing"))
    steps = doc.get("script") or []
    if not isinstance(steps, list):
        raise ValueError("`script` must be a list of steps")

    rng = random.Random()  # unseeded — reproducibility intentionally not a goal

    for i, step in enumerate(steps):
        try:
            await _play_step(client, step, defaults, rng)
        except Exception as e:
            raise RuntimeError(f"script step {i}: {e}") from e


# --- CLI ---

def run_async(coro):
    """Run an async coroutine."""
    return asyncio.run(coro)


try:
    import click
except ImportError:
    click = None  # type: ignore

if click:
    @click.group()
    @click.option("--host", "-h", default="127.0.0.1", help="Server host")
    @click.option("--port", "-p", default=9999, type=int, help="Server port")
    @click.pass_context
    def cli(ctx, host, port):
        """Yetty RPC client."""
        ctx.ensure_object(dict)
        ctx.obj["host"] = host
        ctx.obj["port"] = port

    @cli.command()
    @click.argument("key", type=int)
    @click.option("--mods", "-m", default=0, type=int)
    @click.pass_context
    def key_down(ctx, key, mods):
        """Send key down event."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.key_down(key, mods)
        run_async(_run())

    @cli.command()
    @click.argument("key", type=int)
    @click.option("--mods", "-m", default=0, type=int)
    @click.pass_context
    def key_up(ctx, key, mods):
        """Send key up event."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.key_up(key, mods)
        run_async(_run())

    @cli.command()
    @click.argument("key", type=int)
    @click.option("--mods", "-m", default=0, type=int)
    @click.pass_context
    def press(ctx, key, mods):
        """Press and release a key."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.press_key(key, mods)
        run_async(_run())

    @cli.command("char")
    @click.argument("codepoint", type=int)
    @click.option("--mods", "-m", default=0, type=int)
    @click.pass_context
    def char_cmd(ctx, codepoint, mods):
        """Send character input."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.char_input(codepoint, mods)
        run_async(_run())

    @cli.command()
    @click.argument("x", type=float)
    @click.argument("y", type=float)
    @click.argument("button", type=int, default=0)
    @click.pass_context
    def mouse_down(ctx, x, y, button):
        """Send mouse down event."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.mouse_down(x, y, button)
        run_async(_run())

    @cli.command()
    @click.argument("x", type=float)
    @click.argument("y", type=float)
    @click.argument("button", type=int, default=0)
    @click.pass_context
    def mouse_up(ctx, x, y, button):
        """Send mouse up event."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.mouse_up(x, y, button)
        run_async(_run())

    @cli.command()
    @click.argument("x", type=float)
    @click.argument("y", type=float)
    @click.pass_context
    def mouse_move(ctx, x, y):
        """Send mouse move event."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.mouse_move(x, y)
        run_async(_run())

    @cli.command("mouse-scroll")
    @click.argument("x", type=float)
    @click.argument("y", type=float)
    @click.argument("dx", type=float)
    @click.argument("dy", type=float)
    @click.option("--mods", "-m", default=0, type=int)
    @click.pass_context
    def mouse_scroll_cmd(ctx, x, y, dx, dy, mods):
        """Send mouse-wheel scroll event."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.mouse_scroll(x, y, dx, dy, mods)
        run_async(_run())

    @cli.command()
    @click.argument("width", type=float)
    @click.argument("height", type=float)
    @click.pass_context
    def resize(ctx, width, height):
        """Resize window."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.resize(width, height)
        run_async(_run())

    @cli.command("screenshot")
    @click.argument("path", default="")
    @click.pass_context
    def screenshot_cmd(ctx, path):
        """Capture the current frame to a PPM file."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.screenshot(path)
        run_async(_run())

    @cli.command()
    @click.pass_context
    def shutdown(ctx):
        """Tell yetty to stop its event loop and exit cleanly."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.shutdown()
        run_async(_run())

    @cli.command()
    @click.pass_context
    def ui_tree(ctx):
        """Get UI tree."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                tree = await client.ui_tree()
                click.echo(tree)
        run_async(_run())

    @cli.command()
    @click.pass_context
    def memtags(ctx):
        """Dump per-owner allocation accounting (live/peak bytes per tag)."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                table = await client.memtags()
                click.echo(table if isinstance(table, str) else table.decode())
        run_async(_run())

    @cli.command("type")
    @click.argument("text")
    @click.option("--delay", "-d", default=0.01, type=float)
    @click.pass_context
    def type_cmd(ctx, text, delay):
        """Type text."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.type_text(text, delay)
        run_async(_run())

    @cli.command()
    @click.argument("command")
    @click.option("--delay", "-d", default=0.01, type=float)
    @click.pass_context
    def run(ctx, command, delay):
        """Run a command (type + Enter)."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.run_command(command, delay)
        run_async(_run())

    @cli.command()
    @click.pass_context
    def enter(ctx):
        """Press Enter."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.char_input(ord('\n'))
        run_async(_run())

    @cli.command()
    @click.pass_context
    def tab(ctx):
        """Press Tab."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.press_key(Keys.TAB)
        run_async(_run())

    @cli.command()
    @click.pass_context
    def escape(ctx):
        """Press Escape."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.press_key(Keys.ESCAPE)
        run_async(_run())

    @cli.command("ctrl-c")
    @click.pass_context
    def ctrl_c(ctx):
        """Send Ctrl+C."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.char_input(3, Mods.CONTROL)
        run_async(_run())

    @cli.command("ctrl-d")
    @click.pass_context
    def ctrl_d(ctx):
        """Send Ctrl+D."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await client.char_input(4, Mods.CONTROL)
        run_async(_run())

    @cli.command()
    @click.pass_context
    def interactive(ctx):
        """Interactive mode."""
        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                click.echo("Interactive mode. Ctrl+D to exit.")
                while True:
                    try:
                        line = input("> ")
                        await client.run_command(line, delay=0.005)
                    except EOFError:
                        click.echo("\nExiting.")
                        break
                    except KeyboardInterrupt:
                        await client.char_input(3, Mods.CONTROL)
                        click.echo("^C")
        run_async(_run())

    @cli.command()
    @click.argument("script_file", type=click.Path(exists=True, dir_okay=False))
    @click.pass_context
    def plays(ctx, script_file):
        """Play a YAML script of typing/key/sleep steps."""
        import yaml
        with open(script_file, "r", encoding="utf-8") as f:
            doc = yaml.safe_load(f)
        if not isinstance(doc, dict):
            raise click.ClickException(f"{script_file}: expected a YAML mapping at top level")

        async def _run():
            async with RpcClient(ctx.obj["host"], ctx.obj["port"]) as client:
                await play_script(client, doc)
        run_async(_run())

    @cli.command()
    def keys():
        """Show key codes."""
        click.echo("GLFW Key Codes:")
        click.echo(f"  ENTER={Keys.ENTER} TAB={Keys.TAB} ESCAPE={Keys.ESCAPE}")
        click.echo(f"  BACKSPACE={Keys.BACKSPACE} DELETE={Keys.DELETE}")
        click.echo(f"  LEFT={Keys.LEFT} RIGHT={Keys.RIGHT} UP={Keys.UP} DOWN={Keys.DOWN}")
        click.echo(f"  HOME={Keys.HOME} END={Keys.END}")
        click.echo(f"  PAGE_UP={Keys.PAGE_UP} PAGE_DOWN={Keys.PAGE_DOWN}")
        click.echo("Mods: SHIFT=1 CONTROL=2 ALT=4 SUPER=8")

    def main():
        cli()

else:
    def main():
        print("Click not installed. Use: uv run yctl.py")


if __name__ == "__main__":
    main()
