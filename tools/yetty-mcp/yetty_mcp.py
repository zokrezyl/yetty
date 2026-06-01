#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["mcp>=1.2.0"]
# ///
"""
yetty-mcp — let an AI agent draw rich content into a yetty terminal.

How it works
------------
yetty renders rich figures (markdown, mermaid diagrams, images, SVG, PDF,
syntax-highlighted code) when a program prints an OSC envelope

    ESC ] 600001 ; <base64 args> ; <base64 body> ESC \\

to the terminal. The `ycat` tool already produces that envelope. This MCP
server is a thin wrapper: each tool runs `ycat` (telling it it's inside
yetty via TERM_PROGRAM=yetty), captures the OSC bytes, and writes them to
the controlling terminal `/dev/tty` — which is the yetty PTY when the agent
runs inside a yetty session. The figure appears inline at the cursor.

Requirements
------------
- The agent (and therefore this server) must run inside a yetty terminal,
  so that /dev/tty is the yetty PTY.
- `ycat` must be on PATH (or set YETTY_BIN_DIR to its directory).

Register with an MCP client, e.g. Claude Code `.mcp.json`:

    {
      "mcpServers": {
        "yetty": { "command": "/abs/path/tools/yetty-mcp/yetty_mcp.py" }
      }
    }
"""

from __future__ import annotations

import os
import shutil
import struct
import subprocess
from pathlib import Path

from mcp.server.fastmcp import FastMCP

mcp = FastMCP("yetty")

# ----------------------------------------------------------------------------
# Locating ycat
# ----------------------------------------------------------------------------


def _find_tool(name: str) -> str | None:
    """Resolve a yetty tool binary: $YETTY_BIN_DIR, then PATH, then ~/.local/bin."""
    bin_dir = os.environ.get("YETTY_BIN_DIR")
    if bin_dir:
        cand = Path(bin_dir) / name
        if cand.is_file() and os.access(cand, os.X_OK):
            return str(cand)
    found = shutil.which(name)
    if found:
        return found
    cand = Path.home() / ".local" / "bin" / name
    if cand.is_file() and os.access(cand, os.X_OK):
        return str(cand)
    return None


# ----------------------------------------------------------------------------
# Writing to the live terminal
# ----------------------------------------------------------------------------


def _tty_columns(fd: int) -> int | None:
    try:
        return os.get_terminal_size(fd).columns
    except OSError:
        return None


def _write_to_terminal(data: bytes) -> int:
    """Write raw bytes to the controlling terminal (the yetty PTY). Returns
    bytes written. Raises RuntimeError with a helpful message if there is no
    controlling terminal (i.e. the agent is not running inside yetty)."""
    try:
        fd = os.open("/dev/tty", os.O_WRONLY)
    except OSError as exc:
        raise RuntimeError(
            "no controlling terminal (/dev/tty). Run the agent inside a "
            f"yetty session so the figure has somewhere to render. ({exc})"
        ) from exc
    try:
        return os.write(fd, data)
    finally:
        os.close(fd)


def _run_ycat(args: list[str], stdin: bytes | None = None) -> bytes:
    """Run ycat with TERM_PROGRAM=yetty and return its stdout (the OSC bytes)."""
    ycat = _find_tool("ycat")
    if not ycat:
        raise RuntimeError(
            "ycat not found. Put it on PATH or set YETTY_BIN_DIR to its directory."
        )
    env = dict(os.environ)
    env["TERM_PROGRAM"] = "yetty"
    proc = subprocess.run(
        [ycat, *args],
        input=stdin,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    )
    if not proc.stdout:
        msg = proc.stderr.decode("utf-8", "replace").strip() or "no output"
        raise RuntimeError(f"ycat produced no figure: {msg}")
    return proc.stdout


# yetty reserves the figure's rows on decode but lands the cursor on the
# figure's LAST occupied row (see ycat handler-markdown.c). Without a trailing
# newline the next output (a shell prompt, or a TUI agent's repaint) starts on
# that last row and overlaps the figure. Emit newlines so the cursor steps off
# the figure onto a fresh line below it. Override with YETTY_MCP_TRAILING_NL.
def _trailing_newlines() -> int:
    try:
        return max(0, int(os.environ.get("YETTY_MCP_TRAILING_NL", "1")))
    except ValueError:
        return 1


def _draw_via_ycat(args: list[str], stdin: bytes | None, label: str) -> str:
    # Match the figure width to the real terminal when we can.
    try:
        fd = os.open("/dev/tty", os.O_WRONLY)
        cols = _tty_columns(fd)
        os.close(fd)
    except OSError:
        cols = None
    full_args = (["-w", str(cols)] if cols else []) + args
    osc = _run_ycat(full_args, stdin)
    n = _write_to_terminal(osc + b"\n" * _trailing_newlines())
    return f"Drew {label} to the terminal ({n} bytes)."


# ----------------------------------------------------------------------------
# Tools
# ----------------------------------------------------------------------------


@mcp.tool()
def draw_markdown(markdown: str) -> str:
    """Render Markdown (headings, lists, tables, code blocks, bold/italic,
    links) as a rich figure in the yetty terminal. Use this to present
    structured explanations, summaries, or formatted notes to the user."""
    return _draw_via_ycat(["-c", "markdown", "-"], markdown.encode(), "markdown")


@mcp.tool()
def draw_diagram(mermaid: str) -> str:
    """Render a diagram from Mermaid text. Five diagram families are supported,
    selected by the first line's keyword:

    • Flowchart — `graph TD` / `flowchart LR`:
        graph TD
          A[Start] --> B{ok?}
          B -->|yes| C[Go]
          B -->|no| D[Stop]
      Nodes: A[rect] A(rounded) A([pill]) A((circle)) A{diamond} A{{hex}}
      A[(db)]. Edges: --> (arrow) -.-> (dashed) ==> (thick) -->|label|.

    • State machine — `stateDiagram-v2`:
        stateDiagram-v2
          [*] --> Idle
          Idle --> Running : start
          Running --> [*] : done
      `[*]` is the initial (as source) / final (as target) pseudostate.

    • UML class — `classDiagram`:
        classDiagram
          class Animal { +int age \\n +isMammal() bool }
          Animal <|-- Dog      %% inheritance (hollow triangle)
          Animal *-- Leg       %% composition (filled diamond)
          Animal o-- Tail      %% aggregation (hollow diamond)
          Animal --> Food      %% association
          Animal ..> Util      %% dependency (dashed)

    • Entity-relationship — `erDiagram` (crow's-foot cardinality):
        erDiagram
          CUSTOMER ||--o{ ORDER : places
          CUSTOMER { string name \\n string id PK }

    • Sequence — `sequenceDiagram`:
        sequenceDiagram
          participant A as Alice
          A->>B: request      %% solid arrow
          B-->>A: response     %% dashed return
          Note right of B: a note

    Pick the family that fits; for UML class/sequence/state/ER use the matching
    keyword rather than a flowchart approximation."""
    return _draw_via_ycat(["-c", "mermaid", "-"], mermaid.encode(), "diagram")


@mcp.tool()
def draw_svg(svg: str) -> str:
    """Render an SVG document (shapes, paths, text, gradients) as a figure.
    Pass the full <svg>…</svg> markup. Use this for custom vector graphics
    the agent composes itself."""
    return _draw_via_ycat(["-c", "svg", "-"], svg.encode(), "SVG")


@mcp.tool()
def show_file(path: str, kind: str = "") -> str:
    """Display a file as a rich figure in the terminal: images (PNG/JPG/GIF),
    PDFs, SVGs, Markdown, or source code. Auto-detects by extension; pass
    `kind` to force a handler: one of markdown, pdf, image, svg, mermaid, text."""
    p = Path(path).expanduser()
    if not p.is_file():
        raise RuntimeError(f"file not found: {p}")
    args = (["-c", kind] if kind else []) + [str(p)]
    return _draw_via_ycat(args, None, f"{p.name}")


@mcp.tool()
def clear() -> str:
    """Clear the yetty ydraw figure layer (remove drawn figures)."""
    # OSC 600000 = YDRAW_CLEAR: empty args + empty body.
    envelope = b"\x1b]600000;;\x1b\\"
    n = _write_to_terminal(envelope)
    return f"Cleared the figure layer ({n} bytes)."


if __name__ == "__main__":
    mcp.run()
