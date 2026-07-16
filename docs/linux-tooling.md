# Yetty visual Linux tooling

## Purpose

Yetty should ship a family of visually rich but genuinely useful Linux tools
that demonstrate the value of its infrastructure: GPU-rendered figures,
interactive GUI surfaces, rich content in terminal scrollback, yclass/FFI
bindings, and ordinary shell composability.

The goal is not to reproduce desktop applications inside a terminal. Each tool
should preserve familiar CLI workflows while adding views that text interfaces
cannot express well: plots, treemaps, timelines, graphs, maps, rich previews,
and coordinated interactive panels.

`ytop` is the first member of this family. The following tools form the
proposed next portfolio.

## Product principles

### Build on proven command-line workflows

Prefer domains where mature Linux commands and data formats already prove
demand. Yetty can reuse those backends or consume their output instead of
reimplementing unrelated systems infrastructure.

### Make Yetty's advantage visible

Every selected tool should exercise several shared Yetty capabilities. A tool
that would be equally effective as a conventional TUI is not a strong showcase.

### Support three consumption modes

Where appropriate, every tool should offer:

1. A full interactive application in a Yetty pane.
2. A static or interactive figure embedded in terminal scrollback.
3. Structured or textual output suitable for scripts and pipelines.

The same core data model and renderer should serve all three modes.

### Stay keyboard-first and pipeline-friendly

Mouse interaction should enrich navigation, selection, zooming, and brushing,
but important operations must remain accessible from the keyboard. Inputs
should include files, standard input, and conventional CLI arguments.

### Reuse shared visual primitives

The portfolio should deliberately share tables, filters, plots, trees,
timelines, graphs, and streaming models. Shipping several tools from the same
components is itself evidence for the Yetty architecture.

### Model tool objects as yclass classes

Each tool's core objects, and the shared components below, should be authored
as yclass classes rather than hand-written C function families. Codegen then
emits the dispatch glue, the canonical `model.yaml`, the RPC skeletons, and the
FFI bindings and host-language skeletons. This is what lets any tool be
scripted, proxied over RPC, and bound from other languages without extra work —
a direct, demonstrable expression of the FFI-first value the portfolio exists to
prove. Plain C remains the exception only for thin CLI entry points and leaf
helpers with no object or method surface.

## Proposed tools

### `ynet` — network capture and observability

`ynet` is the flagship network tool: a Wireshark-like packet and flow analyzer
designed around coordinated visual views.

The `ynet` name was, until recently, held by an unrelated WebAssembly module —
the lwIP-over-WebSocket TCP/IP stack (consumed by `ytransport`'s
`lwip-transport`, which dials outbound TCP for telnet and ssh). That module has
now been renamed to `src/yetty/ywasmnet` and the move is committed on the
working branch; `src/yetty/ynet` no longer exists, so the name is free for this
application. What remains before the new `ynet` target takes the name is
verifying the WebAssembly build wiring after the rename and closing the tracking
issue, [GitHub issue #621](https://github.com/zokrezyl/yetty/issues/621).

Initial features:

- Capture live traffic through `libpcap`.
- Open and inspect PCAP and PCAPNG files.
- Select a capture interface and configure BPF capture filters.
- Pause, resume, and save a capture.
- Search packets and apply display filters.
- Show a sortable, virtualized packet table.
- Show an expandable protocol-detail tree.
- Show synchronized hexadecimal and ASCII packet data.
- Summarize protocols, endpoints, conversations, packet rate, and bandwidth.
- Show flows with per-flow traffic sparklines.
- Visualize TCP and TLS handshakes as sequence diagrams.
- Reconstruct and follow TCP streams.
- Explain capture permissions and Linux capabilities when live capture is not
  available.

Showcase features:

- Interactive host and conversation topology graph.
- Remote-endpoint map using the existing `ymap` renderer.
- Cross-view selection: selecting a host, edge, map point, packet, or flow
  filters and highlights every relevant view.
- Inline scrollback summaries for a capture, endpoint, or selected flow.

The first release should not attempt Wireshark-level protocol coverage. Start
with Ethernet, IPv4/IPv6, TCP, UDP, ICMP, DNS, and essential TLS metadata.
Additional dissectors can be added incrementally. An optional adapter may
consume `tshark` JSON for broader dissection without making it a mandatory
runtime dependency.

### `ydu` — visual disk-usage explorer

`ydu` combines an `ncdu`-style filesystem browser with a zoomable treemap. It
is the recommended fast, high-visibility project while `ynet`'s capture and
dissection backend is being developed.

Features:

- Scan a directory, mount, or filesystem incrementally.
- Display a nested treemap sized by disk usage.
- Synchronize the treemap with a directory tree and sortable file table.
- Color entries by file type, age, owner, or hierarchy depth.
- Sort by apparent size, allocated size, inode count, or modification time.
- Handle hard links correctly and make mount boundaries visible.
- Show scan progress and support cancellation.
- Drill down, navigate back, reveal paths, and copy paths.
- Support configurable exclusion patterns.
- Save scan results and compare two scans.
- Emit a selected directory treemap into scrollback.

Deletion, duplicate removal, and other destructive operations are deliberately
deferred. They introduce substantial safety requirements without improving the
initial infrastructure demonstration.

### `ylog` — structured log explorer

`ylog` is a high-volume streaming log viewer. Journald is its first structured
adapter, but the application should remain useful for files and pipelines.

Features:

- Read standard input, log files, and `journalctl` output.
- Follow live streams while allowing the visible view to be paused.
- Parse plain text, journald JSON, and generic JSON Lines.
- Display a virtualized log table and expandable structured fields.
- Search and filter by time, severity, source, service, and arbitrary fields.
- Show severity histograms and an activity heatmap over time.
- Group and count events by service, executable, host, or selected field.
- Brush a time range in a chart to filter the table.
- Bookmark events and preserve surrounding context.
- Export a selected incident as Markdown with embedded figures.

Container and Kubernetes log sources should later be implemented as adapters
to this tool instead of separate log-viewing implementations.

### `yperf` — performance and tracing workbench

`yperf` productizes the existing `yflame` renderer and connects it to common
Linux profiling workflows.

Initial features:

- Import folded stack samples.
- Record or import profiles from Linux `perf`.
- Display static and live flame graphs.
- Support flame and icicle orientations.
- Provide hover details, zoom, search, focus, and reset.
- Synchronize a top-symbol table with the flame graph.
- Filter by process, thread, module, and symbol.
- Show a CPU/sample timeline.
- Compare two profiles with differential coloring.
- Emit a profile or focused subtree as a scrollback figure.

Later adapters may visualize `strace`, `bpftrace`, scheduler events, off-CPU
stacks, and syscall latency. The initial implementation should not attempt to
become a new eBPF framework.

### `ydata` — piped-data explorer

`ydata` turns common structured data into an interactive table and coordinated
charts. It should be useful as the visual continuation of a shell pipeline.

Initial features:

- Read CSV, TSV, JSON Lines, and standard input.
- Infer column types and allow explicit overrides.
- Display a sortable, filterable, virtualized table.
- Summarize null count, distinct count, range, and quantiles per column.
- Generate histograms for numeric columns.
- Generate bar charts for categorical columns.
- Generate scatter plots from selected numeric columns.
- Detect timestamps and generate time-series plots.
- Brush chart regions to filter or select table rows.
- Choose columns and chart types interactively.
- Export filtered data and emit selected charts into scrollback.

DuckDB is a valuable later adapter for Parquet, SQL, aggregation, and data sets
larger than memory. It should not be required by the first version.

### `ygit` — repository history explorer

`ygit` focuses on the parts of Git that benefit most from rich visualization.
It is initially a read-only history and inspection application, not a complete
Git client.

Features:

- Show repository status and branch overview.
- Display an interactive commit DAG with branches and tags.
- Search and filter commits.
- Inspect commit metadata and changed files.
- Display syntax-highlighted unified and side-by-side diffs.
- Browse the history of a file.
- Show blame information with an age heatmap.
- Visualize branch divergence.
- Render Markdown, SVG, images, and PDFs from selected revisions.
- Copy the equivalent Git commands for visible operations.
- Emit commit graphs and diff summaries into scrollback.

Staging, rebasing, conflict resolution, pushing, pulling, and credential
handling are deferred. Read-only exploration delivers the distinctive visual
value with much lower risk.

## Shared infrastructure deliverables

The tools should not independently invent near-identical UI components. The
following reusable pieces are part of the tooling initiative:

- Virtualized, sortable, filterable table.
- Hierarchical tree and property inspector.
- Search/filter bar with composable predicates and saved filters.
- Time-series, histogram, and heatmap views.
- Zoomable treemap.
- Interactive node-and-edge graph.
- Timeline and sequence-diagram view.
- Cross-view selection, highlighting, and filtering model.
- Incremental streaming model with bounded buffering, pause, and resume.
- Background task progress and cancellation.
- Common loading, empty, error, and disconnected states.
- Snapshot/export path for terminal scrollback.
- Consistent application chrome, theme, shortcuts, and status reporting.

Likely existing building blocks include `ygui`, `ygrid`, `yplot`, `ychart`,
`yflame`, `ymap`, `ydraw`, `yimage`, `ypdf`, `ysvg`, and the `yfigure`/yclass
transport path. New primitives should remain general enough for more than one
tool whenever practical.

## Suggested delivery order

1. Seed the shared components through `ydu` rather than a speculative framework.
   Its treemap, synchronized tree and file table, scan progress, cancellation,
   and scrollback export are the first concrete implementations of the shared
   table, streaming/progress, selection, and snapshot foundations. Generalize
   each into a reusable primitive when a second tool needs it — do not design
   the shared layer before a second consumer exists.
2. Build `ynet` as the flagship network observability application. Its capture
   and dissection backend can begin in parallel with `ydu`.
3. Build `ylog` to validate high-volume streaming and coordinated time filters,
   promoting the streaming and time-series views to shared status.
4. Build `yperf` around the existing `yflame` implementation.
5. Build `ydata` to generalize table-and-chart exploration for pipelines.
6. Build `ygit` after the shared graph and rich-inspection components mature.
Complete and close the prerequisite `ynet` → `ywasmnet` module rename
(issue #621) before introducing the new `ynet` target or public symbols.

The shared-component catalog below is the *target* set, harvested by refactoring
from real tools as consumers appear — not a framework to be built up front.

## Deferred candidates

The following ideas remain worthwhile but should not expand the first tooling
initiative:

- `yfiles`: rich previews are compelling, but a polished file manager brings
  substantial filesystem-operation and interaction scope.
- `ydb`: database drivers, credentials, and dialects are costly; first validate
  the interaction model through `ydata` and an optional DuckDB adapter.
- `ydock`/`yk8s`: build later from shared log, table, graph, and metrics
  infrastructure.
- `ygpu`: useful but narrow and vendor-specific.
- `yhttp`: valuable, but it proves HTTP client functionality more strongly than
  Yetty's Linux systems infrastructure.
- `ymap`, `ymesh`, and audio visualization: these already exist and should be
  reused or extended rather than proposed as new tools.

## Portfolio story

Together, the selected tools form a coherent visual Linux workbench:

- `ytop`: what is consuming the machine's resources?
- `ynet`: what is communicating over the network?
- `ylog`: what did the system and applications report?
- `yperf`: where is execution time being spent?
- `ydu`: where did storage capacity go?
- `ydata`: what does this pipeline's data contain?
- `ygit`: how did this codebase change?
The important demonstration is not any one screenshot. It is that all these
tools can share one terminal-native runtime, common interactive figures, and
ordinary Linux pipelines without giving up visual density.
