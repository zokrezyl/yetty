# yless demos

`yless` is a `less`-semantics pager that renders its content into a
**positioned, server-side scrollable figure** (via the `yview` capability)
rather than the scrolling buffer. The content is shipped once; scrolling
happens on the server. On exit the surface is cleared.

These scripts must run **inside a yetty terminal** so the wire envelopes have a
consumer. Launch one with:

```sh
./build-desktop-ytrace-release/yetty -e demo/scripts/yless/svg.sh
```

or, from a shell already running inside yetty, run a script directly.

Each script draws one content type into a chosen region (position/size in
terminal cells, `-x -y -w -H`). Quit with `q` (or Ctrl-C / Esc) — the surface
clears on exit.

## Timed (unattended) runs

`yless` takes `--duration <seconds>` (float): it shows the content for that
long, then auto-exits and clears the surface — no keypress needed. This is how
the demos run under `../all.sh`, which exports `YLESS_DURATION` (default 2s, set
via `ALL_YLESS_DURATION`); each script passes it through as `--duration`. Run a
script standalone (without `YLESS_DURATION` in the environment) and it stays
fully interactive instead — page with the keys above and quit with `q`.

| script | content | position |
|---|---|---|
| `code.sh`    | syntax-highlighted source | whole pane |
| `svg.sh`     | an SVG drawing            | top-left box |
| `diagram.sh` | a mermaid diagram (chart) | right-hand half |
| `pdf.sh`     | a PDF (first page)        | centred box |
| `gallery.sh` | the above, one after another (quit each to advance) |

## Why sequential, not all-at-once

`yless` is interactive and owns the keyboard, so running several at once would
make them fight over input. Drawing **many** views to **many** positions
**simultaneously** is the job of the `yview` library directly (one controlling
program owning several views) — e.g. the planned nvim plugin. `gallery.sh`
therefore shows the positions one at a time.

## Note on "plot"

A genuine function-plot is produced by the `yplot` tool, which currently emits
to the scrolling layer. Rendering yplot output into a `yview` figure is a
planned follow-up; `diagram.sh` stands in as the chart-like example for now.
