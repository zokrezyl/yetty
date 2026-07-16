# ygit — repository history explorer

> Part of the visual Linux tooling portfolio — epic
> [#623](https://github.com/zokrezyl/yetty/issues/623) · tool issue
> [#629](https://github.com/zokrezyl/yetty/issues/629).
> **Status: first slice landed.** The reusable `ygit:repo` yclass component
> (`src/yetty/ygit`, over vendored libgit2) and the structured/textual CLI
> (this `tools/ygit` entry) are implemented and build under the desktop target.
> The interactive pane, scrollback figures, syntax-highlighted diffs, blame
> heatmap, and rich-revision previews remain future slices per the design below.

## Implemented so far

- **`ygit:repo` yclass component** — `src/yetty/ygit/repo.c`. The reusable
  object surface (create / open / is_repo / status / branches / log / show /
  destroy); codegen emits its dispatch, `model.yaml`, RPC skeleton, and binding
  model, so the same component backs the CLI today and the interactive pane
  later. Git access is libgit2 (`git-backend.c`); DAG lane layout is
  `commit-graph.c`.
- **libgit2 vendoring** — `build-tools/3rdparty/libgit2/` (`_build.sh`,
  `build.sh`, `version`) + `build-tools/yetty/libs/libgit2.cmake`, following the
  same prebuilt-tarball/fetch model as the other 3rdparty libs. Local-only
  (no HTTPS/SSH), self-contained static archive.
- **CLI (structured/textual mode)** — `ygit [-C DIR] status | log [-n N] [REV]
  | graph [-n N] [REV] | branches | show REV`. The `graph` view computes real
  DAG lane columns (not `git log --graph` ASCII art).

## Purpose

`ygit` focuses on the parts of Git that benefit most from rich visualization: a
real commit DAG (not ASCII art), syntax-highlighted side-by-side diffs, a blame
age heatmap, and inline rendering of Markdown, SVG, images, and PDFs from any
revision. It is initially a **read-only** history and inspection application, not
a complete Git client. Built after the shared graph and rich-inspection
components mature.

## Consumption modes

1. **Interactive pane** — commit DAG, diff, blame, and file history.
2. **Scrollback figure** — emit commit graphs and diff summaries inline.
3. **Structured output** — history/metadata for scripts and pipelines.

## Features

- Repository status and branch overview.
- Interactive commit DAG with branches and tags.
- Search and filter commits.
- Inspect commit metadata and changed files.
- Syntax-highlighted unified and side-by-side diffs.
- Browse the history of a file.
- Blame information with an age heatmap.
- Visualize branch divergence.
- Render Markdown, SVG, images, and PDFs from selected revisions.
- Copy the equivalent Git commands for visible operations.
- Emit commit graphs and diff summaries into scrollback.

## Architecture

- Module `src/yetty/ygit`, authored as yclass classes: repo, commit graph, diff,
  blame model, and view objects. Thin application entry in `tools/ygit`.
- Reuses the shared node-and-edge graph, virtualized table, and the rich-preview
  renderers `ymarkdown`, `ysvg`, `yimage`, and `ypdf`.

## Scope / deferred

- Staging, rebasing, conflict resolution, pushing, pulling, and credential
  handling are deferred. Read-only exploration delivers the distinctive visual
  value with much lower risk.
