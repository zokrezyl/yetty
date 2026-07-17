# yperf — performance and tracing workbench

> Part of the visual Linux tooling portfolio — epic
> [#623](https://github.com/zokrezyl/yetty/issues/623) · tool issue
> [#627](https://github.com/zokrezyl/yetty/issues/627).
> **Status: feature-complete** (`main.c`, `ui.c`, `profile.c` over the extended
> `yflame` class). See [Implementation status](#implementation-status).

## Purpose

`yperf` productizes the existing `yflame` renderer and connects it to common
Linux profiling workflows. It turns folded stacks and `perf` output into a
flame graph with a synchronized top-symbol table.

## Running

```sh
# record a command and see its flame graph immediately (shells out to perf):
yetty -e './yperf -- ./prog --args'
yetty -e './yperf -p 4242 -d 5'       # a running pid for 5s
yetty -e './yperf -a -d 5'            # whole system for 5s (needs privileges)
yetty -e './yperf --demo'             # synthetic data, no perf needed

# or ingest an existing capture:
yetty -e './yperf out.folded'                 # folded stacks
yetty -e './yperf --perf perf.data'           # collapse a perf.data (needs perf)
cat out.folded | yetty -e './yperf'           # or pipe folded stacks on stdin

# differential flame graph (red = grew, blue = shrank vs the baseline):
yetty -e './yperf --diff before.folded after.folded'

# open zoomed to one symbol:
yetty -e './yperf --focus my_hot_function out.folded'

# scrollback figure (renders inline, no dashboard) — consumption mode 2:
yetty -e './yperf --emit out.folded'
yetty -e './yperf --emit --focus my_hot_function out.folded'

# headless top-symbol table (no GUI) — also the model test path:
./yperf --print out.folded
./yperf --print --perf-script raw-perf-script.txt
```

**Keyboard:** `[j/k]` move · `[/]` search (type, `[enter]` apply, `[esc]` clear) ·
`[enter]` zoom the flame to the selected symbol · `[f]` filter to stacks
containing it · `[F]` clear filter · `[s]` cycle sort (self / total / name) ·
`[i]` flame/icicle · `[g/G]` first/last · `[r]` reset zoom · `[q]` quit.

**Mouse:** hover a frame for its sample detail; left-click to zoom into it;
`up` / `root` buttons (or right-click / wheel) to zoom back out.

## Consumption modes

1. **Interactive pane** — live/static flame graph with drill-down and search.
2. **Scrollback figure** — emit a profile or a focused subtree inline.
3. **Structured output** — top-symbol / aggregate tables for scripts.

## Features

- Import folded stack samples.
- Record or import profiles from Linux `perf`.
- Static and live flame graphs.
- Flame and icicle orientations.
- Hover details, zoom, search, focus, and reset.
- Synchronize a top-symbol table with the flame graph.
- Filter by process, thread, module, and symbol.
- CPU / sample timeline.
- Compare two profiles with differential coloring.
- Emit a profile or focused subtree as a scrollback figure.

## Architecture

`yperf` is a plain-C ygui *client* under `tools/yperf`, mirroring `ytop`/`ydu`:
it attaches to a host yetty pane and drives ygui widgets. The flame graph is
produced by the **`yflame` class** — `configure` → `parse(folded)` → `render()`
hands back a ydraw drawable list pushed into a ygui `ydraw_embed` widget — so the
layout, coloring, and labels are reused rather than reimplemented. Files:
`profile.c` (folded aggregation + perf-script collapse + symbol table + timeline,
no yetty dependency beyond the Result type), `ui.c` (flame pane, timeline strip,
symbol table, mouse routing), `main.c` (client harness, ingestion, keyboard).

- Because `yflame` parses folded text but does not expose its call tree, yperf
  parses the same folded text itself to build the top-symbol table (self/total
  per symbol) and to filter/diff at the folded-text level.
- **Interaction** rides the pane input path: stdin is fronted by a `yetty_yface`
  that splits OSC envelopes (figure mouse / resize) from raw keystrokes, and the
  tool subscribes to pane mouse **and** keyboard (DEC ?1500/?1501/?1502) so
  keyboard nav keeps working after a click focuses the pane figure.
- The `yflame` class was extended with the methods this workbench needs:
  `highlight_name` (search + cross-highlight), `focus_name` (zoom to a symbol),
  `set_baseline` (differential coloring), and `node_name` / `node_value` /
  `root_value` (hover detail).
- **Target shape.** As shared components mature, the profile model and the
  symbol table are the pieces to lift into reusable yclass classes; a
  `src/yetty/yperf` module can then own them with this as the thin entry.

## Implementation status

Feature-complete against issue #627:

- Ingest folded stacks from a file or stdin.
- Built-in **perf-script collapser**: `--perf-script` treats input as raw `perf
  script` output; `--perf <perf.data>` runs `perf script -i` and collapses it.
- Flame graph rendered by `yflame` into a `ydraw_embed` pane, row height fitted
  to the deepest stack, **flame / icicle** orientation toggle.
- **In-pane flame interaction:** mouse hover (frame detail in the status line),
  left-click zoom-to-frame, `up` / `root` nav buttons, right-click / wheel zoom
  out; keyboard `[enter]` zoom-to-symbol and `[r]` reset.
- **Search** (`[/]`) highlights every matching frame; the selected symbol is
  cross-highlighted in the flame whenever no search is active.
- **Filter** (`[f]`) drills the profile down to stacks containing the selected
  symbol; `[F]` restores the full capture.
- **Differential coloring** — `--diff base cur` colors each frame by the delta of
  its sample fraction (red = grew, blue = shrank).
- **CPU / sample timeline** — a bucketed sample-rate strip above the flame,
  extracted from `perf script` timestamps (absent for untimestamped folded input).
- **Scrollback figure emit** — `--emit` renders the (optionally `--focus`ed)
  flame as a `YDRAW_BIN` DCS envelope that renders inline when run in a pane.
- Synchronized **top-symbol table** (self / self% / total / total%) with
  keyboard navigation and sort by self / total / name.
- `--print` headless mode (consumption mode 3) — verified against `du`-style
  hand oracles and a crafted perf-script sample.

Deferred (tracked on issue #627):

- Live/streaming profiles (the flame is rebuilt from a static capture).
- Filter by process/thread/module beyond the folded frame level (folded text
  carries only the collapsed frame names).

## Scope / later

- The initial implementation is **not** a new eBPF framework.
- Later adapters may visualize `strace`, `bpftrace`, scheduler events, off-CPU
  stacks, and syscall latency.
