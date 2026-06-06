# yflame — flame graphs in the terminal

Render [flame graphs](https://www.brendangregg.com/flamegraphs.html) inline in
a yetty session. Two pieces:

- **`yflame`** — a small C tool: folded stacks in, a flame-graph OSC envelope
  out.
- **`yflame.sh`** — a friendly wrapper that records with `perf`, collapses the
  stacks, and renders, in one command.

The renderer is the `yflame:flame` yclass class; the CLI is a thin frontend
over it (`create → configure → parse → render → emit_osc`). For the rendering
internals (tree, layout, colours, primitives) and the full class API see the
module docs in [`src/yetty/yflame/README.md`](../../src/yetty/yflame/README.md).

---

## Quick start

```sh
# Profile yetty itself: use it, quit it — the flame graph pops up in a NEW
# yetty window. (--view is the key: the profiled instance is gone, so a fresh
# one displays the result.)
yflame.sh --view -- yetty -e 'vim big_file.c'

# Try it instantly — synthetic data, no perf needed:
yflame.sh --demo

# Profile a command start-to-finish, rendered inline (run inside a yetty):
yetty -e 'yflame.sh -- ./my_program --work'

# Profile a running process for 15s, wider graph:
yflame.sh -p "$(pgrep -n my_server)" -d 15 -w 2000

# Whole system for 5s (needs privileges):
sudo yflame.sh -a -d 5
```

### "Profile it, and when it exits show me the graph"

This is the headline workflow. Run a program under `perf record`; when it
exits, the flame graph is displayed. Two cases:

- **Profiling a normal program while you're already in yetty** — no `--view`
  needed; the graph renders inline in the current session when the program
  exits:
  ```sh
  yetty -e 'yflame.sh -- ./bench'
  ```
- **Profiling yetty itself (or running outside yetty)** — use `--view`. The
  instance you profiled is the one you just quit, so a *fresh* yetty window
  opens with the graph:
  ```sh
  yflame.sh --view -- yetty -e 'your workload'
  ```
  Under the hood: `perf record -- yetty …` runs, you use and quit yetty,
  `yflame.sh` collapses the capture, renders the envelope, and launches a new
  `yetty` that displays it and drops you into a shell (type `exit` to close).

> Good stacks need symbols: profile a build with frame pointers or debug info.
> A fully `-O2`/stripped binary yields `[unknown]` frames. DWARF unwinding
> (the default `--call-graph dwarf`) helps when frame pointers are omitted.

A flame graph appears in the scrollback: **width = time** (samples), stacked
**bottom-up** by call depth. Wide plateaus are where the time goes; tall
spikes are deep call chains.

---

## The `yflame` tool (low level)

Reads folded stacks from a file or stdin, writes a `YDRAW_BIN` OSC envelope
(DCS `600001`) to stdout.

```
yflame [options] [file]
```

| Option | Default | Meaning |
|---|---|---|
| `-w, --width <px>` | 1200 | graph width |
| `-f, --frame-height <px>` | 18 | height per stack level |
| `--min-width <px>` | 0.5 | skip boxes narrower than this |
| `--icicle` | off | root at top, growing down |
| `--no-labels` | off | omit frame-name labels |
| `-n` | off | no trailing newline |
| `-h, --help` | | usage |

### The folded format

One collapsed stack per line: `;`-separated frames, a space, a sample count.

```
main;parse;lex 42
main;eval;exec 128
```

This is profiler-agnostic — anything that can emit "collapsed/folded" output
works:

```sh
# Linux perf
perf script | stackcollapse-perf.pl | yflame

# async-profiler (Java)
asprof -d 10 -o collapsed -f out.folded <pid> && yflame out.folded

# py-spy (Python)
py-spy record --format raw -o prof.folded -- python app.py && yflame prof.folded

# bpftrace / eBPF, DTrace, rbspy, … all have stackcollapse-* converters
```

`yflame` parses **no** profiler binary format and bundles **no** profiler
code — it just reads text.

---

## The `yflame.sh` wrapper (recommended)

Handles record → `perf script` → collapse → render so you don't have to.

### Input modes (pick one; auto-detected)

| Invocation | What it does |
|---|---|
| `yflame.sh -- <cmd> [args]` | `perf record` the command until it exits |
| `yflame.sh -p <pid>` | profile a running process for `--duration` s |
| `yflame.sh -a` | whole-system profile for `--duration` s |
| `yflame.sh -i <perf.data>` | render an existing capture |
| `yflame.sh -F <file.folded>` | render existing folded stacks (no perf) |
| `… \| yflame.sh` | read folded stacks piped on stdin |
| `yflame.sh --demo` | built-in synthetic data (no perf) |

### Capture options

| Option | Default | Meaning |
|---|---|---|
| `-d, --duration <sec>` | 10 | sampling time for `--pid`/`--all` |
| `--freq <hz>` | 997 | sampling frequency |
| `--fp` | dwarf | frame-pointer stacks instead of DWARF unwinding |
| `--perf-data <file>` | temp | keep the raw `perf.data` |
| `--keep-folded <file>` | — | also save the collapsed folded stacks |
| `--stackcollapse <path>` | built-in | use FlameGraph's `stackcollapse-perf.pl` |

### Render options (forwarded to `yflame`)

`-w/--width`, `--frame-height`, `--icicle`, `--no-labels`.

### Display / other

| Option | Meaning |
|---|---|
| `--view` | when the profiled program exits, open the graph in a **new yetty window** (use for profiling yetty itself, or when not already inside yetty) |
| `--yetty <path>` | yetty binary used by `--view` (default: auto/PATH/build dir) |
| `--dry-run` | print the pipeline without running it |
| `-h, --help` | usage |

Without `--view`, the graph's OSC envelope is written to **stdout**, so it
renders inline when the script runs inside a yetty session (`yetty -e '…'`).
With `--view`, stdout is not used for the picture — a dedicated viewer yetty
is launched instead.

### Design notes

- **Status on stderr, picture on stdout.** All progress messages go to stderr,
  so the OSC envelope on stdout stays clean and renders correctly when piped or
  run under `yetty -e`.
- **Built-in collapser.** A small, original `awk` collapser turns `perf script`
  output into folded stacks, so the common case needs no external Perl script.
  For full fidelity (inlined frames, kernel/user split, annotations) pass
  `--stackcollapse stackcollapse-perf.pl`.
- **DWARF by default.** `--call-graph dwarf` gives good stacks even without
  frame pointers; use `--fp` when your binaries are built with frame pointers
  (faster, smaller captures).

### Privileges

`--all` and some `--pid` captures need root or a relaxed paranoia level:

```sh
sudo sysctl kernel.perf_event_paranoid=1   # allow user-space sampling
```

---

## Licensing

`yflame.sh` **shells out** to your system `perf` (GPL) as a separate process
and bundles no profiler code. The tool and library are first-party only and
read a plain text data format, keeping the figure BSL-clean. If you point
`--stackcollapse` at FlameGraph's `stackcollapse-perf.pl`, that runs as your
own external process too — it is never vendored.
