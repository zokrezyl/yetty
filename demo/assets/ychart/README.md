# ychart demo assets

Sample chart documents in the three formats `ychart` reads — CSV/TSV (with a
`#ychart` directive), JSON (top-level `"chart"` key), and a YAML subset
(top-level `chart:` key). See `src/yetty/ychart/README.md` for the data model
and the full directive / key reference.

All of them render through `ycat <file>`: the `.chart` extension routes
straight to the chart handler, while a plain `.csv` / `.json` / `.yaml` is
content-sniffed (claimed only when it carries a chart marker, so ordinary data
files are never hijacked). The same content sniff works when piped through
stdin.

## Assets by chart kind

| file                     | kind          | covers                                                    |
|--------------------------|---------------|-----------------------------------------------------------|
| `revenue.chart`          | column        | single series, value-axis label, `.chart` extension route |
| `regions.csv`            | column        | grouped multi-series, header row, content-sniffed `.csv`  |
| `regions-stacked.chart`  | column        | `stacked=on` series                                       |
| `languages.chart`        | bar           | horizontal bars, `values=on`                              |
| `signups.csv`            | line          | one series with markers                                   |
| `traffic.chart`          | area          | line with translucent fill to the baseline               |
| `measurements.json`      | scatter       | explicit `{x, y}` points                                  |
| `browsers.json`          | pie           | percentage slices from a `data` object                    |
| `storage.json`           | donut         | annular slices + centre total, `data` array of objects    |
| `skills.yaml`            | radar         | two series over six axes, per-series `color`              |
| `disk-usage.yaml`        | treemap       | squarified weighted cells from a `data` block map         |
| `energy.json`            | sankey        | weighted flow bands between longest-path-layered nodes     |

## Running

From inside a yetty terminal:

```sh
./build-desktop-ytrace-release/yetty -e demo/scripts/ychart/basic.sh
./build-desktop-ytrace-release/yetty -e demo/scripts/ychart/gallery.sh
./build-desktop-ytrace-release/yetty -e demo/scripts/ychart/formats.sh
```

Set `DEMO_PAUSE=1` (seconds) to step through the charts, or `YCAT=/path/to/ycat`
to point at a different build.

Outside yetty you can still render any asset to an OSC envelope or a raw
serialized buffer with the standalone CLI:

```sh
./build-desktop-ytrace-release/tools/ychart/ychart demo/assets/ychart/browsers.json
./build-desktop-ytrace-release/tools/ychart/ychart --type donut demo/assets/ychart/browsers.json
```
