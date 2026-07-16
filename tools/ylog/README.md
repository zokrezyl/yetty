# ylog — structured log explorer

> Part of the visual Linux tooling portfolio — epic
> [#623](https://github.com/zokrezyl/yetty/issues/623) · tool issue
> [#626](https://github.com/zokrezyl/yetty/issues/626).
> **Status: design — not yet implemented.** This README is the tool's design
> document; the module and application entry are scaffolded around it.

## Purpose

`ylog` is a high-volume streaming log viewer. Journald is its first structured
adapter, but the application stays useful for plain files and pipelines. It is
the tool that validates the shared **high-volume streaming** model and
**coordinated time-range filtering** — brushing a chart to filter a table.

## Consumption modes

1. **Interactive pane** — live-following log table with charts.
2. **Scrollback figure** — export a selected incident as Markdown with embedded
   figures.
3. **Structured output** — filtered/normalized records for scripts and pipelines.

## Features

- Read standard input, log files, and `journalctl` output.
- Follow live streams while allowing the visible view to be paused.
- Parse plain text, journald JSON, and generic JSON Lines.
- Virtualized log table and expandable structured fields.
- Search and filter by time, severity, source, service, and arbitrary fields.
- Severity histograms and an activity heatmap over time.
- Group and count events by service, executable, host, or selected field.
- Brush a time range in a chart to filter the table.
- Bookmark events and preserve surrounding context.
- Export a selected incident as Markdown with embedded figures.

## Architecture

- Module `src/yetty/ylog`, authored as yclass classes: source/adapter, record,
  field model, and view objects. Thin application entry in `tools/ylog`.
- Promotes the streaming and time-series/heatmap views to shared status for the
  rest of the portfolio.
- Bounded buffering with pause/resume is a shared streaming concern, first
  hardened here under real high-volume load.

## Later

- Container and Kubernetes log sources are implemented as **adapters** to this
  tool, not as separate log-viewing applications.
