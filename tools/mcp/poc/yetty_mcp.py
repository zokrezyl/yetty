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

import base64
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


# Sentinel wrapping a base64 OSC envelope returned to a parent process. When
# YETTY_MCP_VIA_PARENT is set (e.g. a custom loop that owns the terminal and
# serializes its own output), we do NOT write to /dev/tty — a second concurrent
# writer corrupts large envelopes. Instead the parent scans the tool result for
# this marker and renders the bytes itself, as the sole writer of the PTY.
_FIGURE_OPEN = "<<CCLOOP_FIGURE "
_FIGURE_CLOSE = " CCLOOP_FIGURE>>"


def _emit_figure(data: bytes, label: str) -> str:
    """Either write the envelope to the terminal, or hand it back to a parent
    loop via the result sentinel, depending on YETTY_MCP_VIA_PARENT."""
    if os.environ.get("YETTY_MCP_VIA_PARENT"):
        encoded = base64.b64encode(data).decode("ascii")
        return f"{_FIGURE_OPEN}{encoded}{_FIGURE_CLOSE} drew {label}"
    written = _write_to_terminal(data)
    return f"Drew {label} to the terminal ({written} bytes)."


def _run_tool(tool: str, args: list[str], stdin: bytes | None = None) -> bytes:
    """Run a yetty figure tool (ycat / yplot) with TERM_PROGRAM=yetty and
    return its stdout (the OSC bytes)."""
    binary = _find_tool(tool)
    if not binary:
        raise RuntimeError(
            f"{tool} not found. Put it on PATH or set YETTY_BIN_DIR to its directory."
        )
    env = dict(os.environ)
    env["TERM_PROGRAM"] = "yetty"
    proc = subprocess.run(
        [binary, *args],
        input=stdin,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    )
    if not proc.stdout:
        msg = proc.stderr.decode("utf-8", "replace").strip() or "no output"
        raise RuntimeError(f"{tool} produced no figure: {msg}")
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
    osc = _run_tool("ycat", full_args, stdin)
    return _emit_figure(osc + b"\n" * _trailing_newlines(), label)


def _draw_plot(args: list[str]) -> str:
    """Run yplot (it emits the same OSC envelope as ycat) and write it to the
    terminal. Unlike ycat, yplot's -w/-H are pixel dimensions, not character
    columns — so we do NOT inject the terminal column count here."""
    osc = _run_tool("yplot", args)
    return _emit_figure(osc + b"\n" * _trailing_newlines(), "plot")


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
def draw_plot(
    expression: str,
    width: int = 0,
    height: int = 0,
    xrange: str = "",
    yrange: str = "",
    grid: bool = True,
    axes: bool = True,
    labels: bool = True,
) -> str:
    """Render a function/signal plot as a figure in the yetty terminal, using
    the standalone `yplot` engine. Use this to visualise math functions,
    sampled data buffers, and multi-curve dashboards inline at the cursor.

    `expression` is a yplot-language source string. It supports several layers,
    all separable by `;`:

    • Single function — the variable is `x`:
        sin(x)
      Operators + - * / and functions sin cos exp abs sqrt etc.; constants
      `pi`, `tau`, `e`. An unnamed expression auto-names to `plot1`.

    • Multiple named functions (each drawn as its own curve):
        f=sin(x); g=cos(x)

    • Per-curve color overrides (#RRGGBB), keyed by the curve's name:
        f=sin(x); g=cos(x); @f.color=#FF6B6B; @g.color=#4ECDC4

    • Inline domain / viewport (alternative to the xrange/yrange args):
        x=-pi..pi; sine=sin(x)
        x=-10..10; @view=-2..2,-1..1; damped=sin(x)*exp(-abs(x)/3)
      `x=A..B` / `y=A..B` set the evaluation domain; `@view=A..B,C..D` sets the
      initial visible rectangle without resampling. Bounds accept pi/tau/e and
      unary minus.

    • Data buffers (sampled inputs) — `name=buffer` declares one, then:
        data=buffer; @data.size=8; @data.values=0,0.3,0.6,0.9,0.6,0,-0.4,-0.2
      `@name.size=N` sets capacity (k/m suffixes allowed); `@name.values=...`
      fills it with inline samples spread across the x domain. Reference a
      buffer inside an expression as `name(x)` to sample it at the current x:
        env=buffer; @env.size=4; @env.values=0,1,0.5,0; out=env(x)*sin(x*40)

    • Animation — referencing `time` in an expression auto-subscribes the plot
      to the animation timer, producing a live-updating figure:
        wave=buffer; @wave.size=4; @wave.values=0,1,0,-1; live=wave(x)*sin(time*2)

    Args:
        expression: the yplot source (may contain many `;`-separated parts).
        width: plot width in PIXELS (0 → yplot default, 400).
        height: plot height in PIXELS (0 → yplot default, 200).
        xrange: X axis range as "lo..hi" (e.g. "-3.14..3.14"); empty → default.
        yrange: Y axis range as "lo..hi" (e.g. "-1..1"); empty → default.
            Ignored if the expression sets the domain inline via `x=`/`y=`.
        grid: draw the grid overlay (default True).
        axes: draw the axes overlay (default True).
        labels: draw axis labels (default True).
    """
    args: list[str] = []
    if width > 0:
        args += ["-w", str(width)]
    if height > 0:
        args += ["-H", str(height)]
    if xrange:
        args.append(f"--xrange={xrange}")
    if yrange:
        args.append(f"--yrange={yrange}")
    if not grid:
        args.append("--no-grid")
    if not axes:
        args.append("--no-axes")
    if not labels:
        args.append("--no-labels")
    args.append(expression)
    return _draw_plot(args)


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
    return _emit_figure(envelope, "clear")


if __name__ == "__main__":
    mcp.run()
