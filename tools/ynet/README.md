# ynet — network capture and observability

> Part of the visual Linux tooling portfolio — epic
> [#623](https://github.com/zokrezyl/yetty/issues/623) · tool issue
> [#624](https://github.com/zokrezyl/yetty/issues/624).
> **Status: M1 (offline core) landed.** The `ynet:capture` yclass class, the
> libpcap offline reader, the Ethernet/IPv4/IPv6/TCP/UDP/ICMP/DNS dissectors,
> flow aggregation, and a headless `--dump` frontend are implemented and build
> under `yetty_ynet` + `tools/ynet`. Next: the interactive pane (packet table +
> protocol tree + hex/ASCII), then live libpcap capture + BPF (M2), then the
> coordinated graph / map / sequence views (M3). The feature sections below
> remain the tool's design target.
>
> Quick check: `python3 test/ynet/make-sample.py && ./ynet test/ynet/sample.pcap`.

## Purpose

`ynet` is the flagship network tool: a Wireshark-like packet and flow analyzer
designed around coordinated visual views. The value is not the packet list — it
is the set of synchronized views (topology graph, geo-map, flow sparklines,
sequence diagrams) that a plain TUI cannot express, all driven by one selection
model.

## Prerequisite — the `ywasmnet` rename

The `ynet` name was previously held by an unrelated WebAssembly module (the
lwIP-over-WebSocket TCP/IP stack, consumed by `ytransport`'s `lwip-transport`).
That module has been renamed to `src/yetty/ywasmnet` and the move is committed on
the branch. Before this new user-facing `ynet` target and its public symbols are
introduced, verify the WebAssembly build wiring after the rename and close
[GitHub issue #621](https://github.com/zokrezyl/yetty/issues/621).

## Consumption modes

1. **Interactive pane** — full capture/inspection application.
2. **Scrollback figure** — inline summary of a capture, endpoint, or flow.
3. **Structured output** — capture/flow summaries for scripts and pipelines.

## Features

### Initial

- Capture live traffic through `libpcap`.
- Open and inspect PCAP and PCAPNG files.
- Select a capture interface and configure BPF capture filters.
- Pause, resume, and save a capture.
- Search packets and apply display filters.
- Sortable, virtualized packet table.
- Expandable protocol-detail tree.
- Synchronized hexadecimal and ASCII packet data.
- Summarize protocols, endpoints, conversations, packet rate, and bandwidth.
- Flows with per-flow traffic sparklines.
- Visualize TCP and TLS handshakes as sequence diagrams.
- Reconstruct and follow TCP streams.
- Explain capture permissions / Linux capabilities when live capture is
  unavailable.

### Showcase

- Interactive host and conversation topology graph.
- Remote-endpoint map using the existing `ymap` renderer.
- Cross-view selection: selecting a host, edge, map point, packet, or flow
  filters and highlights every relevant view.
- Inline scrollback summaries for a capture, endpoint, or selected flow.

## Architecture

- Module `src/yetty/ynet`, authored as yclass classes: capture, packet, flow,
  conversation, and view objects. Thin application entry in `tools/ynet`.
- Composes shared primitives: virtualized table, filter bar, node-and-edge
  graph, timeline/sequence-diagram view, flow sparklines (`yplot`), and `ymap`.

## Scope

- The first release does **not** attempt Wireshark-level protocol coverage.
  Start with Ethernet, IPv4/IPv6, TCP, UDP, ICMP, DNS, and essential TLS
  metadata; add dissectors incrementally.
- An optional adapter may consume `tshark` JSON for broader dissection **without**
  making it a mandatory runtime dependency.
