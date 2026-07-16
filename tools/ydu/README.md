# ydu — visual disk-usage explorer

> Part of the visual Linux tooling portfolio — epic
> [#623](https://github.com/zokrezyl/yetty/issues/623) · tool issue
> [#625](https://github.com/zokrezyl/yetty/issues/625).
> **Status: first slice implemented** (`main.c`, `ui.c`, `scan.c`). See
> [Implementation status](#implementation-status) below for what is live and
> what is still deferred.

## Running

```sh
# interactive treemap + table inside a yetty pane:
yetty -e './ydu /some/path'          # defaults to the current directory

# headless text summary (no GUI) — also the scan test path:
./ydu --print /some/path
```

Keys: `[j/k]` move · `[enter/l]` open · `[u/h]` up · `[s]` cycle sort · `[r]`
rescan · `[g/G]` first/last · `[q]` quit.

## Purpose

`ydu` combines an `ncdu`-style filesystem browser with a zoomable treemap. It is
the recommended **first** build in the portfolio: fast to ship, high-visibility,
and backed by a small, well-understood backend (a filesystem walk). It is also
the vehicle that **seeds the shared components** — the virtualized table, the
zoomable treemap, streaming scan progress, cross-view selection, and the
scrollback snapshot path — which later tools generalize rather than reinvent.

## Consumption modes

The same scan model and renderer serve all three modes:

1. **Interactive pane** — treemap + directory tree + file table, navigable.
2. **Scrollback figure** — emit a selected directory's treemap inline.
3. **Structured output** — machine-readable scan results for scripts/pipelines.

## Features

- Scan a directory, mount, or filesystem incrementally.
- Nested treemap sized by disk usage.
- Synchronize the treemap with a directory tree and a sortable file table.
- Color entries by file type, age, owner, or hierarchy depth.
- Sort by apparent size, allocated size, inode count, or modification time.
- Handle hard links correctly and make mount boundaries visible.
- Show scan progress and support cancellation.
- Drill down, navigate back, reveal paths, and copy paths.
- Configurable exclusion patterns.
- Save scan results and compare two scans.
- Emit a selected directory treemap into scrollback.

## Architecture

**Current shape (first slice).** `ydu` is a plain-C ygui *client* under
`tools/ydu`, mirroring `ytop`: it attaches to a host yetty pane over the
yclass-RPC transport and drives ygui widgets, painting the treemap into a
`ydraw_embed` canvas. Files: `scan.c` (the size tree, no yetty dependency beyond
the Result type), `ui.c` (treemap + table presentation), `main.c` (libuv client
harness + keyboard navigation).

- Composes shared visual primitives: the ygui `table`, a squarified `ydraw_embed`
  treemap, `panel`/`label` chrome, and cross-view selection between the table row
  and the highlighted tile.
- **Target shape.** As the second consumer (`ynet`, `ylog`) appears, the size
  model, treemap, and table are the components to lift into reusable yclass
  classes (per the portfolio's harvest-don't-design-up-front rule) — at which
  point a `src/yetty/ydu` module can own them and this stays the thin entry.

## Scope / deferred

- Deletion, duplicate removal, and other destructive operations are deliberately
  deferred. They introduce substantial safety requirements without improving the
  initial infrastructure demonstration.

## Keyboard-first

Navigation, drill-down, sorting, and export are all reachable from the keyboard;
mouse interaction enriches zooming, brushing, and selection but never gates an
operation.

## Implementation status

Live in this first slice:

- Recursive scan (`opendir`/`readdir`/`lstat`) aggregating allocated bytes
  (`st_blocks * 512`), apparent size, recursive item counts, and mtime.
- Hard-link dedup: files with `nlink > 1` are charged once per `(device, inode)`.
- Squarified treemap of the current directory, tiles sized by disk usage,
  directories vs files colour-coded, big tiles labelled with name + size.
- Synchronized sortable table (name / size / items / modified) with the selected
  row mirrored as the highlighted treemap tile.
- Keyboard navigation: move, drill in, go up, cycle sort (size / apparent /
  items / name / mtime), rescan, jump first/last.
- `--print` headless mode (consumption mode 3) — verified against `du` totals.

Deferred (tracked on issue #625):

- Asynchronous scan with a live progress bar and cancellation (the scan is
  currently synchronous at startup / on rescan).
- Mount-boundary visibility; colour-by owner/age/type toggles.
- Save a scan and compare two scans; configurable exclusion patterns.
- Scrollback treemap emit (consumption mode 2); reveal/copy path.
- Mouse-driven zoom/drill and brushing.
- Destructive operations (delete / dedup) remain intentionally out of scope.
