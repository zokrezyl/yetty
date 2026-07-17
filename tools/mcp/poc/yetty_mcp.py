#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["mcp>=1.2.0"]
# ///
"""
yetty-mcp — let an AI agent draw rich content into a yetty terminal.

How it works
------------
yetty renders rich figures (markdown, mermaid diagrams, data charts, images,
SVG, PDF, LilyPond music scores, syntax-highlighted code) when a program prints
an OSC envelope

    ESC P 600001 ; <base64 args> ; <base64 body> ESC \\

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
import tempfile
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


# Sentinel handing an OSC envelope to a parent process. When
# YETTY_MCP_VIA_PARENT is set (e.g. a custom loop that owns the terminal and
# serializes its own output), we do NOT write to /dev/tty — a second concurrent
# writer corrupts large envelopes. Instead the envelope bytes go to a temp
# FILE and the tool result carries only its path; the parent scans for the
# marker, writes the bytes itself as the sole PTY writer, and unlinks the
# file. (An older inline-base64 variant, <<CCLOOP_FIGURE b64 CCLOOP_FIGURE>>,
# pushed the whole envelope through the tool result — that blew the agent's
# tool-result token cap for large figures, e.g. music scores embedding font
# glyphs, and wasted model context for small ones. Parents still accept it.)
_FIGURE_FILE_OPEN = "<<CCLOOP_FIGURE_FILE "
_FIGURE_FILE_CLOSE = " CCLOOP_FIGURE_FILE>>"

# Preferred parent-render protocol: hand the parent the RAW content plus the
# render kind and let IT run the tool (it knows the real terminal width and is
# the single PTY writer). The temp file holds the raw source (markdown, svg,
# …), NOT a pre-rendered envelope. Format:
#   <<YAI:DRAW kind=<kind> path=</abs/tmp/yetty-draw-XXXX>>>
# Used for the text→ycat tools; plot / show_file still pre-render via
# _emit_figure (their inputs don't fit the "raw content + ycat -c kind" shape).
_YAI_DRAW_OPEN = "<<YAI:DRAW "
_YAI_DRAW_CLOSE = ">>"


def _emit_figure(data: bytes, label: str) -> str:
    """Either write the envelope to the terminal, or hand it back to a parent
    loop via the result sentinel, depending on YETTY_MCP_VIA_PARENT."""
    if os.environ.get("YETTY_MCP_VIA_PARENT"):
        with tempfile.NamedTemporaryFile(
            prefix="yetty-figure-", suffix=".bin", delete=False
        ) as spool:
            spool.write(data)
            spool_path = spool.name
        return f"{_FIGURE_FILE_OPEN}{spool_path}{_FIGURE_FILE_CLOSE} drew {label}"
    written = _write_to_terminal(data)
    return f"Drew {label} to the terminal ({written} bytes)."


def _emit_content(kind: str, content: bytes, label: str) -> str:
    """Render `content` of the given `kind`. Under a parent loop, stage the
    raw bytes in a temp file and return the YAI:DRAW marker so the parent
    renders it (one PTY writer). Standalone, render here via ycat and write
    the envelope to the terminal."""
    if os.environ.get("YETTY_MCP_VIA_PARENT"):
        with tempfile.NamedTemporaryFile(prefix="yetty-draw-", delete=False) as spool:
            spool.write(content)
            spool_path = spool.name
        return f"{_YAI_DRAW_OPEN}kind={kind} path={spool_path}{_YAI_DRAW_CLOSE} drew {label}"
    return _draw_via_ycat(["-c", kind, "-"], content, label)


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


def _draw_flame(args: list[str], folded: bytes) -> str:
    """Run yflame (it emits the same OSC envelope as ycat) and write it to the
    terminal. Like yplot, yflame's -w is a pixel width, not character columns —
    so we do NOT inject the terminal column count here. We pass -n so yflame
    omits its own trailing newline and let _trailing_newlines() add exactly the
    spacing the other figure tools use."""
    osc = _run_tool("yflame", ["-n", *args], folded)
    return _emit_figure(osc + b"\n" * _trailing_newlines(), "flame graph")


def _draw_mesh(args: list[str]) -> str:
    """Run ymesh (it emits the same OSC envelope as the other figure tools) and
    write it to the terminal. Like yplot/yflame, ymesh's -w/-H are pixel
    dimensions, not character columns — so we do NOT inject the terminal column
    count. We pass --once so ymesh emits a single envelope and exits instead of
    entering its interactive orbit-viewer loop (this subprocess has no
    controlling TTY to drive that loop). ymesh appends its own trailing newline
    in one-shot mode; strip it and re-add via _trailing_newlines() so the
    YETTY_MCP_TRAILING_NL override applies uniformly across the figure tools."""
    osc = _run_tool("ymesh", ["--once", *args]).rstrip(b"\n")
    return _emit_figure(osc + b"\n" * _trailing_newlines(), "mesh")


# ----------------------------------------------------------------------------
# Tools
# ----------------------------------------------------------------------------


@mcp.tool()
def draw_markdown(markdown: str) -> str:
    """Render Markdown (headings, lists, tables, code blocks, bold/italic,
    links) as a rich figure in the yetty terminal. Use this to present
    structured explanations, summaries, or formatted notes to the user."""
    return _emit_content("markdown", markdown.encode(), "markdown")


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
    return _emit_content("mermaid", mermaid.encode(), "diagram")


@mcp.tool()
def draw_svg(svg: str) -> str:
    """Render an SVG document (shapes, paths, text, gradients) as a figure.
    Pass the full <svg>…</svg> markup. Use this for custom vector graphics
    the agent composes itself. Do NOT hand-draw music notation with SVG —
    use draw_music with LilyPond source instead; it produces properly
    engraved scores."""
    return _emit_content("svg", svg.encode(), "SVG")


@mcp.tool()
def draw_music(lilypond: str) -> str:
    """Engrave music notation from LilyPond source and render it as a figure
    in the yetty terminal — staff, clefs, noteheads, beams, rests and
    accidentals typeset with the Emmentaler music font. ALWAYS use this
    (never draw_svg) when the user wants sheet music, a melody, a scale,
    a riff, or any musical notation.

    Pass standard LilyPond (a pragmatic subset is supported), e.g.:

        \\version "2.24.0"
        \\relative c' { \\clef treble \\time 3/4 c4 d e f g a b c }

    Notes use pitch letters a..g with is/es accidentals, ' / , octave marks
    and duration suffixes (c4 = quarter, d8 = eighth, dots allowed); r4 is a
    rest, <c e g>4 a chord, | a bar check. Parsed commands: \\clef, \\time
    N/D, \\key <pitch> \\major|\\minor, \\relative [<pitch>]. Wrapping such
    as \\version, \\header, \\score, \\new Staff and braces is tolerated and
    skipped. Long scores wrap into systems at the terminal width."""
    return _emit_content("music", lilypond.encode(), "music score")


@mcp.tool()
def draw_shader(wgsl: str) -> str:
    """Render an animated GPU shader from a WGSL `mainImage` function as a live
    figure in the yetty terminal (a Shadertoy-style fragment shader). The
    figure animates on yetty's frame timer — use this for procedural visuals,
    demos, generative art, and animated backgrounds.

    Pass a WGSL fragment with exactly this entry-point signature (the receiving
    factory compiles it; nothing is parsed sender-side):

        fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,
                     iTime: f32, iMouse: vec4<f32>) -> vec4<f32> {
            let uv = fragCoord / iResolution.xy;
            let col = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3<f32>(0.0, 2.0, 4.0));
            return vec4<f32>(col, 1.0);
        }

    Inputs (Shadertoy convention): `fragCoord` is in pixels with the origin at
    the figure's BOTTOM-LEFT; `iResolution.xy` is the figure size in pixels;
    `iTime` is seconds since the figure appeared; `iMouse.xy` is the pointer
    position in pixels. Return linear RGBA in `vec4<f32>`. The figure is sized
    to the terminal width with a 16:9 default height."""
    return _emit_content("shadertoy", wgsl.encode(), "shader")


@mcp.tool()
def draw_circuit(circuit: str) -> str:
    """Render an electronic circuit schematic from the ycircuit DSL as a figure
    in the yetty terminal — resistors, capacitors, inductors, diodes, sources,
    transistors, op-amps, grounds and wires drawn as proper schematic symbols
    with reference designators and values.

    The DSL is line-based; `#` starts a comment; coordinates are in grid units
    (floats allowed). The first line should be `circuit <title>`:

        circuit Half-wave rectifier
        grid 14                          # optional px-per-grid-unit hint

        # component: <kind> <x> <y> [<rot>] [<name>] [<value>]
        #   rot: h | v | r0 | r90 | r180 | r270 (default h)
        vsource   2  6  v  V1  9V
        diode     8  2  h  D1  1N4148
        resistor 14  6  v  R1  10k

        wire 2 2  5 2                    # wire x0 y0 x1 y1 [x2 y2 ...] (polyline)
        wire 2 10  14 10
        dot 14 10                        # junction dot
        gnd 8 10                         # ground symbol
        label 15.5 2 Vout               # free text (rest of line)

    Component kinds (with aliases): resistor(r), capacitor(cap,c),
    inductor(coil,l), diode(d), led, battery(bat), vsource(v), isource(i),
    acsource(ac), gnd(ground), vcc, npn, pnp, switch(sw), opamp."""
    return _emit_content("circuit", circuit.encode(), "circuit")


@mcp.tool()
def draw_chart(data: str) -> str:
    """Render a data chart — bar, column, line, area, scatter, pie, donut,
    radar, treemap, or sankey — as a figure in the yetty terminal, from a
    small data document. Use this to visualise categorical or tabular data
    (counts, shares, comparisons, distributions, flows) inline at the cursor.
    For continuous math functions / sampled signals / 2D fields use draw_plot
    instead; for call-tree costs use draw_flame.

    `data` is a self-identifying chart document in one of three formats; the
    chart KIND is taken from a directive/key inside it (default: column).

    • CSV/TSV with a leading `#ychart` directive line:
        #ychart type=column title="Quarterly revenue" y=kUSD
        quarter,revenue
        Q1,120
        Q2,138
        Q3,99
      Extra value columns become extra series; an optional header row names
      them. Directive keys: type/kind, title, x/xlabel, y/ylabel, legend,
      values (label each datum), stacked (stack series instead of grouping).

    • JSON with a top-level "chart" key:
        {"chart":"pie","title":"Browser share",
         "data":{"Chrome":65,"Safari":19,"Edge":9}}
        {"chart":"column","categories":["Q1","Q2"],
         "series":[{"name":"2021","values":[10,20]},
                   {"name":"2022","color":"#5B8FF9","values":[12,18]}]}
        {"chart":"sankey",
         "links":[{"source":"Coal","target":"Power","value":25}]}
      `data` may be an object {label:value}, an array of numbers, or an array
      of {label,value} objects.

    • YAML with a top-level `chart:` key (numeric lists use inline [...]):
        chart: radar
        title: Skills
        categories: [speed, power, range, control]
        series:
          - name: Alice
            values: [3, 5, 2, 4]
          - name: Bob
            color: "#F6BD16"
            values: [4, 2, 5, 3]

    Kinds (aliases): bar(hbar) | column(col,vbar) | line | area |
    scatter(points) | pie | donut(doughnut) | radar(spider) | treemap |
    sankey(flow). pie/donut/treemap use one series (one slice/cell per
    category); scatter accepts per-point {x,y}; sankey uses `links`/`flows`
    (source/target/value) instead of categories/series. Omit the kind to let
    it pick column (single series) or grouped column / line (multi-series)
    from the data shape; pie/donut/radar/treemap/sankey must be requested
    explicitly via the directive/key."""
    return _emit_content("chart", data.encode(), "chart")


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
    sampled data buffers, multi-curve dashboards, and 2D field heatmaps
    inline at the cursor.

    `expression` is a yplot-language source string. It supports several layers,
    all separable by `;`:

    • Single function — the variable is `x`:
        sin(x)
      Operators + - * / % and constants `pi`, `tau`, `e`. An unnamed
      expression auto-names to `plot1`. Built-in functions:
        trig:        sin cos tan asin acos atan atan2(y,x) sinc
        hyperbolic:  sinh cosh tanh asinh acosh atanh
        exp/log:     exp exp2 log log2 pow(b,e) sqrt rsqrt
        rounding:    floor ceil round trunc fract sign abs mod(a,b)
        clamping:    min max clamp(x,lo,hi) saturate mix(a,b,t)
                     step(edge,x) smoothstep(edge0,edge1,x)
        statistics:  erf erfc
        angles:      radians degrees
        stochastic:  rand(x) — deterministic white noise in [0,1);
                     noise(x) — smooth value noise; rand2(x,y) noise2(x,y)
                     for 2D fields.

    • Comparisons & piecewise functions — lt/gt/le/ge/eq/ne(a,b) return
      1.0 or 0.0; select(falseVal, trueVal, cond) picks branchlessly:
        piecewise=select(x*x, sin(4*x), ge(x,0))
      Together they build gates, domain guards, and piecewise curves.

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

    • 2D heatmaps — an expression that references `y` is a field f(x,y) and
      renders as a colormapped heatmap (perceptually uniform viridis)
      instead of a line curve:
        field=sin(x)*cos(y)
        rings=sin(sqrt(x*x+y*y)*3)
      The field value is mapped from [-1, 1] to the colormap — scale the
      expression (or wrap it in tanh) to fit that range. xrange/yrange (or
      inline x=A..B; y=A..B) set both domains; square width/height pixels
      keep the aspect true.

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
def draw_flame(
    folded: str,
    width: int = 0,
    frame_height: int = 0,
    icicle: bool = False,
    labels: bool = True,
) -> str:
    """Render a flame graph as a figure in the yetty terminal, using the
    standalone `yflame` engine. Use this to visualise where time (or any
    additive cost) is spent across a call tree — profiler output, allocation
    sites, or any hierarchical counts.

    `folded` is collapsed/folded-stack text, one stack per line: a `;`-joined
    frame path followed by a space and an integer count. Frames read root → leaf
    left to right. Example:

        main;parse;lex 42
        main;parse;eval 17
        main;render 31

    This is the format produced by the FlameGraph toolkit
    (`perf script | stackcollapse-perf.pl`) and most language profilers.

    Args:
        folded: the folded-stack text (one `path count` per line).
        width: graph width in PIXELS (0 → yflame default, 1200).
        frame_height: height per stack level in PIXELS (0 → yflame default, 18).
        icicle: root at the TOP growing downward, instead of bottom-up (default
            False — classic flame graph with the root at the bottom).
        labels: draw frame-name labels on the boxes (default True).
    """
    args: list[str] = []
    if width > 0:
        args += ["-w", str(width)]
    if frame_height > 0:
        args += ["-f", str(frame_height)]
    if icicle:
        args.append("--icicle")
    if not labels:
        args.append("--no-labels")
    return _draw_flame(args, folded.encode())


@mcp.tool()
def show_file(path: str, kind: str = "") -> str:
    """Display a file as a rich figure in the terminal: images (PNG/JPG/GIF),
    PDFs, SVGs, Markdown, Mermaid diagrams, LilyPond music scores (.ly —
    engraved with the Emmentaler font), WGSL shaders, ycircuit schematics,
    Lottie animations, H.264 video, or source code. Auto-detects by extension;
    pass `kind` to force a handler: one of markdown, pdf, image, svg, mermaid,
    music (alias: lilypond), shadertoy (alias: wgsl), circuit (alias:
    schematic), lottie, video, text."""
    p = Path(path).expanduser()
    if not p.is_file():
        raise RuntimeError(f"file not found: {p}")
    args = (["-c", kind] if kind else []) + [str(p)]
    return _draw_via_ycat(args, None, f"{p.name}")


@mcp.tool()
def show_mesh(path: str, width: int = 0, height: int = 0) -> str:
    """Render a 3D mesh from a glTF 2.0 binary (.glb) file as a figure in the
    yetty terminal, using the standalone `ymesh` engine. Use this to show 3D
    geometry — models, CAD exports, procedurally generated meshes — inline at
    the cursor. The mesh is Lambert-shaded from its surface normals and framed
    by a bounding-box orbit camera.

    `path` must point to a `.glb` file: a glTF 2.0 *binary* container with the
    geometry embedded. Only the first mesh / first primitive is drawn, using its
    positions, normals and indices — textures, UVs and materials are not yet
    supported. If you have geometry as separate `.gltf` + `.bin` (+ textures),
    pack it into a single self-contained `.glb` first, then pass that path.

    Args:
        path: filesystem path to the `.glb` file.
        width: display width in PIXELS (0 → ymesh default, 600).
        height: display height in PIXELS (0 → ymesh default, 600).
    """
    p = Path(path).expanduser()
    if not p.is_file():
        raise RuntimeError(f"file not found: {p}")
    args: list[str] = []
    if width > 0:
        args += ["-w", str(width)]
    if height > 0:
        args += ["-H", str(height)]
    args.append(str(p))
    return _draw_mesh(args)


@mcp.tool()
def clear() -> str:
    """Clear the yetty ydraw figure layer (remove drawn figures)."""
    # OSC 600000 = YDRAW_CLEAR: empty args + empty body.
    envelope = b"\x1b]600000;;\x1b\\"
    return _emit_figure(envelope, "clear")


if __name__ == "__main__":
    mcp.run()
