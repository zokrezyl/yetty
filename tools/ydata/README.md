# ydata — piped-data explorer

> Part of the visual Linux tooling portfolio — epic
> [#623](https://github.com/zokrezyl/yetty/issues/623) · tool issue
> [#628](https://github.com/zokrezyl/yetty/issues/628).
> **Status: design — not yet implemented.** This README is the tool's design
> document; the module and application entry are scaffolded around it.

## Purpose

`ydata` turns common structured data into an interactive table and coordinated
charts — the visual continuation of a shell pipeline. It generalizes the shared
table-and-chart exploration surface: pipe data in, get a sortable table plus
auto-generated histograms, bar charts, scatter plots, and time series that brush
back onto the table.

## Consumption modes

1. **Interactive pane** — table + coordinated charts with brushing.
2. **Scrollback figure** — emit a selected chart inline.
3. **Structured output** — export filtered/transformed data for the next stage.

## Features

- Read CSV, TSV, JSON Lines, and standard input.
- Infer column types and allow explicit overrides.
- Sortable, filterable, virtualized table.
- Summarize null count, distinct count, range, and quantiles per column.
- Histograms for numeric columns.
- Bar charts for categorical columns.
- Scatter plots from selected numeric columns.
- Detect timestamps and generate time-series plots.
- Brush chart regions to filter or select table rows.
- Choose columns and chart types interactively.
- Export filtered data and emit selected charts into scrollback.

## Architecture

- Module `src/yetty/ydata`, authored as yclass classes: dataset, column model,
  chart spec, and view objects. Thin application entry in `tools/ydata`.
- Reuses the shared virtualized table plus `yplot` / `ychart`, and the
  cross-view selection model for chart-to-table brushing.

## Later

- DuckDB is a valuable adapter for Parquet, SQL, aggregation, and data sets
  larger than memory. It is **not** required by the first version, and validating
  the interaction model here comes before any dedicated database client (`ydb`).
