#!/usr/bin/env python3
"""
cc_loop — a minimal, hackable Claude Code loop that renders into yetty.

It drives the headless `claude` CLI over a bidirectional stream-json pipe,
owns its own scrollback transcript, streams responses live, and renders rich
content as yetty figures.

Two independent rendering paths reach yetty (both rely on this process running
*inside* a yetty pane, so /dev/tty and our stdout are the yetty PTY):

  1. Agent-drawn figures — the sub-`claude` is given the `yetty` MCP server,
     whose tools (draw_svg, draw_diagram, draw_plot, draw_markdown, show_file)
     write the OSC 600001 envelope straight to /dev/tty. We just allow them;
     the figures land inline in our screen with zero work here.

  2. Loop-rendered answers — each completed assistant turn is written to a
     temp .md and piped through `ycat`, which emits the same OSC envelope, so
     normal markdown replies become proper yetty markdown figures instead of
     flat text. Toggle with LOOP_RENDER (see config below).

Scrollback: every event is appended to an in-memory transcript and mirrored to
tmp/transcript-<session>.jsonl, so the conversation is replayable/searchable
beyond whatever the terminal keeps.

Usage:
    ./cc_loop.py                       # start a fresh session
    ./cc_loop.py --resume <uuid>       # resume a prior session id
    LOOP_RENDER=stream ./cc_loop.py    # live plain text, no ycat (default)
    LOOP_RENDER=ycat   ./cc_loop.py    # blank while writing, figure at the end
    LOOP_RENDER=snap   ./cc_loop.py    # live draft text, erased + redrawn as a figure
    LOOP_RENDER=window ./cc_loop.py    # live text + latest answer in a floating window
    SHOW_THINKING=1    ./cc_loop.py    # show dim thinking text
    LOOP_FOLD_LINES=20 ./cc_loop.py    # tool-output preview cap (default 8)

Type your message at the prompt. Ctrl-D or /quit to exit.
"""

from __future__ import annotations

import base64
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import unicodedata
import uuid

import ywin  # sibling module: floating compositor windows via the yview FFI
import ystats  # sibling module: floating ygui dialog for token/cost stats

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

ALLOWED_TOOLS = [
    "Read",
    "Bash(git *)",
    "mcp__yetty",  # allow the whole yetty MCP server (all draw_* tools)
]
PERMISSION_MODE = os.environ.get("LOOP_PERMISSION_MODE", "auto")
MODEL = os.environ.get("LOOP_MODEL")  # None -> CLI default
RENDER_MODE = os.environ.get("LOOP_RENDER", "stream")  # stream | ycat | snap | window
SHOW_THINKING = os.environ.get("SHOW_THINKING", "") not in ("", "0", "no")
YCAT = os.environ.get("YCAT_BIN", "ycat")
FOLD_LINES = int(os.environ.get("LOOP_FOLD_LINES", "8"))  # tool-output preview cap

# ANSI helpers (the loop owns the screen, so we style it ourselves).
DIM = "\033[2m"
BOLD = "\033[1m"
MINT = "\033[38;2;107;168;146m"  # brand accent
MUTED = "\033[38;2;133;141;143m"
RED = "\033[38;2;220;100;100m"
RESET = "\033[0m"


# ---------------------------------------------------------------------------
# Transcript / scrollback
# ---------------------------------------------------------------------------


class Transcript:
    """In-memory scrollback plus a JSONL mirror on disk."""

    def __init__(self, session_id: str):
        self.session_id = session_id
        self.events: list[dict] = []
        path = os.path.join("tmp", f"transcript-{session_id}.jsonl")
        os.makedirs("tmp", exist_ok=True)
        self.file = open(path, "a", buffering=1)
        self.path = path

    def append(self, event: dict) -> None:
        self.events.append(event)
        self.file.write(json.dumps(event) + "\n")


# ---------------------------------------------------------------------------
# Token / cost accounting
# ---------------------------------------------------------------------------


def fmt_tokens(n: int) -> str:
    """Compact human-readable token count: 2391 -> '2.4k', 970 -> '970'."""
    if n >= 1000:
        return f"{n / 1000:.1f}k"
    return str(n)


class SessionUsage:
    """Accumulates per-turn token/cost numbers across the whole session."""

    def __init__(self, panel=None):
        self.input = 0
        self.output = 0
        self.cache_read = 0
        self.cache_creation = 0
        self.cost = 0.0
        self.turns = 0
        # Optional floating ygui dialog; when present the token line goes there
        # instead of into the terminal scrollback.
        self.panel = panel

    def render_turn(self, usage: dict, cost, duration_ms) -> None:
        """Update session totals from one turn's `result.usage` and surface the
        token/cost numbers — in the ygui dialog if we have one, else as a line."""
        turn_in = usage.get("input_tokens", 0)
        turn_out = usage.get("output_tokens", 0)
        cache_read = usage.get("cache_read_input_tokens", 0)
        cache_creation = usage.get("cache_creation_input_tokens", 0)
        self.input += turn_in
        self.output += turn_out
        self.cache_read += cache_read
        self.cache_creation += cache_creation
        self.cost += cost or 0.0
        self.turns += 1

        seconds = (duration_ms or 0) / 1000.0
        rate = f" · {turn_out / seconds:.0f} tok/s" if seconds > 0 else ""
        cost_str = f" · ${cost:.4f}" if cost is not None else ""
        turn_line = (
            f"↑{fmt_tokens(turn_in)} in · {fmt_tokens(cache_read)} cached"
            f" · ↓{fmt_tokens(turn_out)} out"
            f"{f' · {seconds:.1f}s' if seconds else ''}{rate}{cost_str}"
        )
        session_line = (
            f"session: ↓{fmt_tokens(self.output)} out"
            f" · ${self.cost:.4f} · {self.turns} turn(s)"
        )

        if self.panel is not None:
            # Tokens go to the ygui dialog; the terminal scrollback stays clean.
            self.panel.update(turn_line, session_line)
            return
        sys.stdout.write(f"{MUTED}  {turn_line}{RESET}\n{DIM}  {session_line}{RESET}\n")
        sys.stdout.flush()


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------


ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[a-zA-Z]")


def char_cells(ch: str) -> int:
    """Display width of one character in terminal cells (0/1/2)."""
    if unicodedata.category(ch) in ("Mn", "Me", "Cf"):
        return 0
    if unicodedata.east_asian_width(ch) in ("W", "F"):
        return 2
    return 1


def rows_advanced(text: str, width: int) -> int:
    """How many rows the cursor moves DOWN printing `text` from column 0.

    Accounts for explicit newlines, line wrapping at `width`, tab stops, and
    zero-/double-width characters. ANSI SGR codes are stripped first (they
    occupy no cells). This is the `n` used to jump back and erase.
    """
    plain = ANSI_RE.sub("", text)
    column = 0
    down = 0
    for ch in plain:
        if ch == "\n":
            down += 1
            column = 0
            continue
        if ch == "\t":
            advance = 8 - (column % 8)
        else:
            advance = char_cells(ch)
        if advance == 0:
            continue
        if column + advance > width:
            down += 1
            column = 0
        column += advance
    return down


def render_markdown_figure(markdown: str) -> None:
    """Render markdown as a yetty figure by piping it through ycat."""
    with tempfile.NamedTemporaryFile(
        "w", suffix=".md", dir="tmp", delete=False
    ) as handle:
        handle.write(markdown)
        temp_path = handle.name
    env = dict(os.environ, TERM_PROGRAM="yetty")
    try:
        subprocess.run([YCAT, temp_path], env=env, check=False)
    except FileNotFoundError:
        # ycat missing: fall back to plain text so we never lose output.
        sys.stdout.write(markdown + "\n")
        sys.stdout.flush()


def summarize_tool_input(name: str, tool_input: dict) -> str:
    """Pick the most informative one-line summary for a tool call."""
    for key in ("command", "file_path", "pattern", "path", "url", "query"):
        if key in tool_input:
            value = str(tool_input[key])
            return value if len(value) <= 100 else value[:97] + "..."
    summary = json.dumps(tool_input)
    return summary if len(summary) <= 100 else summary[:97] + "..."


def tool_result_text(content) -> str:
    """Normalize a tool_result content field (string or block list) to text."""
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        parts = []
        for block in content:
            if isinstance(block, dict):
                parts.append(block.get("text") or json.dumps(block))
            else:
                parts.append(str(block))
        return "\n".join(parts)
    return str(content)


def fold(text: str) -> str:
    """Show only the first FOLD_LINES lines; note how many were hidden."""
    lines = text.rstrip("\n").split("\n")
    if len(lines) <= FOLD_LINES:
        return text.rstrip("\n")
    hidden = len(lines) - FOLD_LINES
    head = "\n".join(lines[:FOLD_LINES])
    return f"{head}\n{DIM}    … {hidden} more line(s) (full in transcript){RESET}"


def render_tool_call(name: str, tool_input: dict) -> None:
    sys.stdout.write(
        f"\n{MUTED}⚙ {BOLD}{name}{RESET}{MUTED}{RESET} "
        f"{DIM}{summarize_tool_input(name, tool_input)}{RESET}\n"
    )
    sys.stdout.flush()


# Matches the sentinels the yetty MCP server emits under YETTY_MCP_VIA_PARENT:
# the loop is the single PTY writer, so it renders the envelope itself. The
# FILE form carries a temp-file path holding the raw envelope bytes (large
# envelopes — music scores embed font glyphs — must not travel through the
# tool result, where they blow the agent's token cap). The inline-base64
# form is the legacy variant, still accepted from older servers.
FIGURE_FILE_RE = re.compile(r"<<CCLOOP_FIGURE_FILE (\S+) CCLOOP_FIGURE_FILE>>")
FIGURE_RE = re.compile(r"<<CCLOOP_FIGURE ([A-Za-z0-9+/=]+) CCLOOP_FIGURE>>")


def read_figure_file(path: str) -> bytes | None:
    """Read and unlink a figure spool file. None when unreadable."""
    try:
        with open(path, "rb") as spool:
            data = spool.read()
        os.unlink(path)
        return data or None
    except OSError:
        return None


def write_figure_bytes(data: bytes) -> None:
    """Write raw OSC figure bytes to the terminal, serialized after any pending
    text output (single-writer: no other process touches this PTY)."""
    sys.stdout.flush()
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()


def render_tool_result(content, is_error: bool, tool_name: str | None) -> None:
    raw = tool_result_text(content)
    figure_bytes: bytes | None = None
    if not is_error:
        file_match = FIGURE_FILE_RE.search(raw)
        if file_match:
            figure_bytes = read_figure_file(file_match.group(1))
        else:
            inline_match = FIGURE_RE.search(raw)
            if inline_match:
                figure_bytes = base64.b64decode(inline_match.group(1))
    if figure_bytes:
        sys.stdout.write(f"  {MINT}✓{RESET} {MUTED}{tool_name or 'figure'}{RESET}\n")
        sys.stdout.flush()
        write_figure_bytes(figure_bytes)
        return
    text = fold(raw)
    if not text.strip():
        text = "(no output)"
    marker = f"{RED}✗{RESET}" if is_error else f"{MINT}✓{RESET}"
    label = f" {MUTED}{tool_name}{RESET}" if tool_name else ""
    indented = "\n".join("  " + line for line in text.split("\n"))
    sys.stdout.write(
        f"  {marker}{label}\n{DIM if not is_error else RED}{indented}{RESET}\n"
    )
    sys.stdout.flush()


def render_hook_event(event: dict) -> None:
    """Best-effort display of a hook lifecycle frame (only emitted when hooks
    are configured in settings.json; shape may vary by CLI version)."""
    name = (
        event.get("hook_event_name")
        or event.get("subtype")
        or event.get("event")
        or "hook"
    )
    sys.stdout.write(f"{MUTED}⤷ hook: {name}{RESET}\n")
    sys.stdout.flush()


# Lazily-created floating window for `window` render mode. A 3-state holder:
# None  = not tried yet, False = tried and unavailable (don't retry every turn),
# Window = live. The window persists across turns; set_text updates it in place.
_WINDOW: "ywin.Window | bool | None" = None


def get_window() -> "ywin.Window | None":
    """Return the shared floating window, creating it on first use. Returns None
    (once, with a notice) if the yview FFI library isn't available, so the loop
    degrades to its inline text without spamming the failure."""
    global _WINDOW
    if _WINDOW is None:
        _WINDOW = ywin.Window.create() or False
        if _WINDOW is False:
            sys.stdout.write(
                f"{MUTED}⚠ window mode: libyetty_ffi.so not found "
                f"(build it with `make build-desktop-ffi-release`, or set "
                f"YETTY_FFI_LIB); falling back to inline text.{RESET}\n"
            )
            sys.stdout.flush()
    return _WINDOW or None


class TurnRenderer:
    """Renders one assistant turn from the streamed events."""

    # Modes that stream raw text live before any final render.
    LIVE_MODES = ("stream", "snap", "window")

    def __init__(self):
        self.text_parts: list[str] = []
        self.in_text = False
        self.in_thinking = False
        # Visible (drawable) text emitted since this message started, used to
        # compute how many rows to erase in `snap` mode.
        self.drawn_visible = ""

    def _emit(self, visible: str, styled: str) -> None:
        """Write styled bytes to the screen; remember the visible run for erase."""
        sys.stdout.write(styled)
        sys.stdout.flush()
        self.drawn_visible += visible

    def on_text_delta(self, text: str) -> None:
        self.text_parts.append(text)
        if RENDER_MODE not in self.LIVE_MODES:
            return
        if not self.in_text:
            style = DIM if RENDER_MODE == "snap" else ""
            self._emit("claude ", f"\n{MINT}{BOLD}claude{RESET} {style}")
            self.in_text = True
        self._emit(text, text)

    def on_thinking_delta(self, text: str) -> None:
        if not SHOW_THINKING or RENDER_MODE not in self.LIVE_MODES:
            return
        if not self.in_thinking:
            self._emit("thinking ", f"\n{MUTED}thinking {RESET}{DIM}")
            self.in_thinking = True
        self._emit(text, text)

    def end_thinking(self) -> None:
        if self.in_thinking:
            self._emit("\n", RESET + "\n")
            self.in_thinking = False

    def _erase_drawn(self) -> bool:
        """Erase what was streamed this message. Returns False if it scrolled
        off-screen and cannot be safely erased."""
        size = shutil.get_terminal_size((80, 24))
        down = rows_advanced(self.drawn_visible, size.columns)
        if down >= size.lines - 1:
            return False  # taller than the viewport — would erase the wrong rows
        up = f"\033[{down}A" if down > 0 else ""
        sys.stdout.write(up + "\r\033[J")
        sys.stdout.flush()
        return True

    def finish(self) -> None:
        full = "".join(self.text_parts)
        if RENDER_MODE == "ycat":
            if full.strip():
                sys.stdout.write(f"\n{MINT}{BOLD}claude{RESET}\n")
                render_markdown_figure(full)
            return
        if RENDER_MODE == "snap":
            erased = self._erase_drawn() if self.in_text else True
            if not erased and self.in_text:
                sys.stdout.write(f"\n{DIM}— formatted —{RESET}\n")
            if full.strip():
                sys.stdout.write(f"{MINT}{BOLD}claude{RESET}\n")
                render_markdown_figure(full)
            return
        if RENDER_MODE == "window":
            # Live text already streamed inline (window is in LIVE_MODES); now
            # mirror the final answer into the floating panel over the buffer.
            if self.in_text:
                sys.stdout.write("\n")
                sys.stdout.flush()
            window = get_window()
            if window and full.strip():
                if not window.set_text(full):
                    sys.stdout.write(f"{RED}⚠ window emit failed{RESET}\n")
                    sys.stdout.flush()
            return
        # stream
        if self.in_text:
            sys.stdout.write("\n")
            sys.stdout.flush()


# ---------------------------------------------------------------------------
# Event pump (reader thread)
# ---------------------------------------------------------------------------


def reader_loop(
    proc: subprocess.Popen,
    transcript: Transcript,
    turn_done: threading.Event,
    state: dict,
) -> None:
    """Parse the stream-json output and drive rendering until the pipe closes."""
    renderer: TurnRenderer | None = None
    tool_names: dict[str, str] = {}  # tool_use_id -> tool name, to label results
    for raw in proc.stdout:
        raw = raw.strip()
        if not raw:
            continue
        try:
            event = json.loads(raw)
        except json.JSONDecodeError:
            continue
        transcript.append(event)
        kind = event.get("type")

        if kind == "system" and event.get("subtype") == "init":
            state["session_id"] = event.get("session_id", state.get("session_id"))
            renderer = None
            continue

        if kind == "stream_event":
            inner = event.get("event", {})
            inner_type = inner.get("type")
            if inner_type == "message_start":
                renderer = TurnRenderer()
            elif inner_type == "content_block_delta" and renderer:
                delta = inner.get("delta", {})
                if delta.get("type") == "text_delta":
                    renderer.end_thinking()
                    renderer.on_text_delta(delta.get("text", ""))
                elif delta.get("type") == "thinking_delta":
                    renderer.on_thinking_delta(delta.get("thinking", ""))
            elif inner_type == "content_block_stop" and renderer:
                renderer.end_thinking()
            continue

        if kind == "assistant":
            if renderer:
                renderer.end_thinking()
            for block in event.get("message", {}).get("content", []):
                if block.get("type") == "tool_use":
                    name = block.get("name", "?")
                    if block.get("id"):
                        tool_names[block["id"]] = name
                    render_tool_call(name, block.get("input", {}))
            continue

        if kind == "user":
            for block in event.get("message", {}).get("content", []):
                if isinstance(block, dict) and block.get("type") == "tool_result":
                    name = tool_names.get(block.get("tool_use_id"))
                    render_tool_result(
                        block.get("content"), bool(block.get("is_error")), name
                    )
            continue

        if (kind and "hook" in kind) or event.get("hook_event_name"):
            render_hook_event(event)
            continue

        if kind == "result":
            if renderer:
                renderer.finish()
                renderer = None
            state["usage"].render_turn(
                event.get("usage", {}),
                event.get("total_cost_usd"),
                event.get("duration_ms"),
            )
            turn_done.set()
            continue


# ---------------------------------------------------------------------------
# Main REPL
# ---------------------------------------------------------------------------


def build_command(session_id: str, resume: str | None) -> list[str]:
    cmd = [
        "claude",
        "--print",
        "--input-format", "stream-json",
        "--output-format", "stream-json",
        "--include-partial-messages",
        "--include-hook-events",
        "--verbose",
        "--permission-mode", PERMISSION_MODE,
        "--allowedTools", ",".join(ALLOWED_TOOLS),
    ]
    if resume:
        cmd += ["--resume", resume]
    else:
        cmd += ["--session-id", session_id]
    if MODEL:
        cmd += ["--model", MODEL]
    return cmd


def send_user_message(proc: subprocess.Popen, text: str) -> None:
    message = {"type": "user", "message": {"role": "user", "content": text}}
    proc.stdin.write(json.dumps(message) + "\n")
    proc.stdin.flush()


def main() -> int:
    resume = None
    args = sys.argv[1:]
    if args and args[0] == "--resume" and len(args) > 1:
        resume = args[1]
    session_id = resume or str(uuid.uuid4())

    transcript = Transcript(session_id)
    # Floating ygui dialog for token/cost stats (None if the FFI lib isn't
    # built — then the stats fall back to a printed line). The conversation
    # itself always streams to the terminal.
    stats_panel = ystats.StatsPanel.create(sys.stdout.fileno())
    state = {"session_id": session_id, "usage": SessionUsage(panel=stats_panel)}
    turn_done = threading.Event()

    stderr_log = open(os.path.join("tmp", f"claude-stderr-{session_id}.log"), "w")
    # The loop owns the terminal, so the yetty MCP server must hand figures back
    # to us instead of racing them to /dev/tty (which corrupts large envelopes
    # while we stream text). This env var switches it to the parent-render path.
    child_env = dict(os.environ, YETTY_MCP_VIA_PARENT="1")
    proc = subprocess.Popen(
        build_command(session_id, resume),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=stderr_log,
        env=child_env,
        text=True,
        bufsize=1,
    )

    reader = threading.Thread(
        target=reader_loop, args=(proc, transcript, turn_done, state), daemon=True
    )
    reader.start()

    sys.stdout.write(
        f"{MINT}{BOLD}cc_loop{RESET} {MUTED}session {session_id}{RESET}\n"
        f"{DIM}render={RENDER_MODE}  perm={PERMISSION_MODE}  "
        f"transcript={transcript.path}{RESET}\n"
        f"{DIM}Type a message. /quit or Ctrl-D to exit.{RESET}\n"
    )

    try:
        while True:
            try:
                line = input(f"\n{MINT}you ▸ {RESET}")
            except EOFError:
                break
            line = line.strip()
            if not line:
                continue
            if line in ("/quit", "/exit"):
                break
            turn_done.clear()
            send_user_message(proc, line)
            turn_done.wait()  # block until the turn's result event arrives
    finally:
        try:
            proc.stdin.close()
        except Exception:
            pass
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
        stderr_log.close()
        if stats_panel is not None:
            stats_panel.destroy(sys.stdout.fileno())
        sys.stdout.write(f"\n{DIM}transcript saved to {transcript.path}{RESET}\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
