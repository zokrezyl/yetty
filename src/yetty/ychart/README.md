# ychart — data → chart → ydraw buffer

`ychart` turns tabular / flow data (CSV, TSV, JSON, YAML) into a ydraw
buffer of SDF shape and MSDF text primitives, the same way
[`ydiagram`](../ydiagram) turns Mermaid text into one. The buffer is handed
to a yetty pane (over the DCS `YDRAW_BIN` envelope) or to a ygui widget.

It is pure C and depends only on `ycore`, `ydraw-core`, and `ysdf` — no
platform-specific code.

## Chart kinds

| kind | aliases | notes |
|------|---------|-------|
| `bar` | `hbar` | horizontal bars |
| `column` | `col`, `vbar` | vertical bars (the auto default) |
| `line` | | poly-line per series with markers |
| `area` | | line + translucent fill to the baseline |
| `scatter` | `points` | markers at (x, y); honours per-point x values |
| `pie` | | slices tessellated as triangle fans |
| `donut` | `doughnut` | annular slices + centre total |
| `radar` | `spider` | one filled polygon per series over category spokes |
| `treemap` | | squarified rectangles weighted by the first series |
| `sankey` | `flow` | flow bands between longest-path-layered nodes |

Grouped and `stacked` bar/column/area are both supported. Bar/column/line/
area/scatter share one value axis with "nice" round-number gridlines.

## Input formats

A document self-identifies one of three ways so a plain data file is never
mistaken for a chart:

### 1. CSV/TSV with a `#ychart` directive

```
#ychart type=column title="Quarterly revenue" y=kUSD
quarter,revenue
Q1,120
Q2,140
Q3,90
Q4,160
```

Extra columns become extra series; an optional header row names them:

```
#ychart type=column title="Sales by region" stacked=on
region,2021,2022,2023
North,10,20,30
South,15,25,20
```

Directive keys: `type`/`kind`, `title`, `x`/`xlabel`, `y`/`ylabel`,
`legend`, `values`, `stacked`. Values may be quoted.

### 2. JSON with a top-level `"chart"` key

```json
{ "chart": "pie", "title": "Browser share",
  "data": { "Chrome": 65, "Safari": 19, "Edge": 9, "Firefox": 7 } }
```

```json
{ "chart": "column", "categories": ["Q1","Q2"],
  "series": [ { "name": "2021", "values": [10, 20] },
              { "name": "2022", "color": "#5B8FF9", "values": [12, 18] } ] }
```

```json
{ "chart": "sankey",
  "links": [ { "source": "Coal", "target": "Electricity", "value": 25 } ] }
```

`data` may be an object (`{label: value}`), an array of numbers, or an
array of `{label, value}` objects.

### 3. YAML with a top-level `chart:` key (a tolerant subset)

```yaml
chart: radar
title: Skills
categories: [speed, power, range, control, stamina]
series:
  - name: Alice
    values: [3, 5, 2, 4, 5]
  - name: Bob
    color: "#F6BD16"
    values: [4, 2, 5, 3, 2]
```

Numeric value lists use the inline `[ ... ]` form. `data:` may be a block
map (`label: value`); `links:` is a block sequence of `source/target/value`
maps (also accepts the inline `- { source: A, target: B, value: 5 }` form).

## Using it

High-level one-shot (`ychart.h`):

```c
struct yetty_ychart_buffer_result r =
    yetty_ychart_render_data(input, len);          /* auto-detect everything */
/* … or with control over kind / size / a real text measurer: */
r = yetty_ychart_render_data_full(input, len, path, YETTY_YCHART_KIND_PIE,
                                   &opts, measure_fn, measure_userdata);
/* r.value is an owned ydraw buffer; free with yetty_ydraw_drawable_list_destroy */
```

Or compose the layers directly: build a `struct yetty_ychart_chart`
(`chart-ir.h`), or parse into one (`data-parser.h`), then call
`yetty_ychart_render` (`renderer.h`).

## Consumers

- **CLI** — `tools/ychart` emits a DCS `YDRAW_BIN` envelope (or, with
  `-o`, a raw serialized buffer). `ychart --type pie data.csv`.
- **ycat** — `.chart` / `.ychart` files, or any CSV/JSON/YAML carrying a
  chart marker, are detected and drawn inline (handler `handler-chart.c`,
  gated on `YETTY_ENABLE_FEATURE_YCHART`).

## Layout of the module

| file | role |
|------|------|
| `chart-ir.c` | IR lifecycle, builders, palette, kind names |
| `data-detect.c` | format sniff, `#ychart` directive, detect→parse dispatch |
| `csv-parser.c` / `json-parser.c` / `yaml-parser.c` | data → IR |
| `render-common.c` | render dispatch + shared shape/text/legend helpers |
| `renderer-cartesian.c` | bar / column / line / area / scatter |
| `renderer-polar.c` | pie / donut / radar |
| `renderer-hier.c` | treemap (squarified) / sankey |
| `ychart.c` | high-level data → ydraw buffer glue |

The only hand-written headers are the four public ones in
`include/yetty/ychart/`; `render-state.h` is the private shared header for
the renderer translation units.
