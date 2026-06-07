# Architecture & Module Map

Yetty is built from ~70 small C modules, each a directory under `src/yetty/`
(public headers under `include/yetty/`). This document is the index: what each
module is for, and roughly how mature it is. For the design decisions behind the
core, start with the [Design Overview](design.md).

**Maturity legend:** ✓ in use · ◐ partial / early · ○ stub / planned

---

## The big picture

```
                      main()  →  yetty_yinit_run()
                                       │
   platform bootstrap         ┌────────┴─────────┐   yinit: window, surface,
   (window, surface, config,  │      yinit       │   config, asset extraction,
    event pipes, OS loop)     └────────┬─────────┘   event pipes, OS event loop
                                       │
   GPU / event / RPC          ┌────────┴─────────┐   yframework: adapter, device,
   services                   │    yframework    │   queue, allocator, MSDF gen,
                              └────────┬─────────┘   render target, VNC + RPC srv
                                       │
   terminal application       ┌────────┴─────────┐   yetty → yui (tabs/panes) →
                              │       yetty       │   yterminal (terminals)
                              └────────┬─────────┘
                                       │
   per terminal:          content layer       +    root yfigure compositor
                              (text + ydraw)            (z-ordered figures)
                                      │                         │
                         libvterm, SDF canvas     ygui, ymgui, yrdawn, ygrid,
                                                  yplot, yimage, yvideo, …
```

The full startup/ownership chain and context structs are in
[Contexts](contexts.md); the GPU object ownership is in
[WebGPU Architecture](webgpu-architecture.md).

---

## Bootstrap & application shell

| Module | Purpose | |
|---|---|---|
| `yinit` | Platform bootstrap: paths, asset extraction, config, window, surface, event pipes, OS loop; runs an app worker | ✓ |
| `yframework` | Generic GPU/event/RPC services layer (adapter, device, queue, allocator, MSDF gen, render target, VNC + RPC servers) | ✓ |
| `yetty` | Top-level terminal application instance | ✓ |
| `ymain` | Thin `main()` wrapper that wires yinit → yframework → yetty | ✓ |
| `yui` / `yui-core` | App UI: tabs, tiled panes, workspaces, settings/debug windows; view object abstraction | ✓ |
| `yclient` | libuv event loop for client-side apps consuming the OSC/yface stream | ✓ |

## Terminal core

| Module | Purpose | |
|---|---|---|
| `yterminal` | Terminal view: owns one composite content layer plus the root `yfigure` compositor; handles PTY, scrollback, selection, input forwarding | ✓ |
| `yvterm` | Terminal content internals: libvterm text grid, scrolling ydraw canvas, shader-glyph figure, scroll/alt-screen cross-wiring | ✓ |
| `ytrace` | Switchable trace points, near-zero cost when off ([ytrace](../src/yetty/ytrace/README.md)) | ✓ |
| `ycore` | Result/error types, math, buffers, util — the foundation everything builds on ([result](result.md)) | ✓ |
| `yconfig` | YAML config parser with path-based key/value API | ✓ |
| `yevent` | Event-loop abstraction (pipes, timers, TCP, listeners) | ✓ |
| `yco` | Coroutine wrapper over libco ([coroutines](coroutines.md)) | ✓ |
| `ynotify` | Thread-safe user-facing notifications | ✓ |
| `ycdb` | Constant-database (cdb) key/value store | ✓ |
| `yclass` | Annotation-driven class/object runtime + RPC + binding model ([yclass](../src/yetty/yclass/README.md)) | ✓ |

## Rendering pipeline

| Module | Purpose | |
|---|---|---|
| `yrender` | GPU pipeline: resource-set binder, allocator, pipeline, render targets, blender ([render](render.md), [GPU binding](gpu-resource-binding.md)) | ✓ |
| `yrender-utils` | Screenshot, tile-diff helpers | ✓ |
| `ydraw` | Canvas + rolling-row scrolling primitive model ([ydraw](../src/yetty/ydraw/README.md)) | ✓ |
| `ydraw-core` | Serialized primitive buffer, draw list, drawable-list registry | ✓ |
| `ydraw-factory` | Figure factory for composites | ✓ |
| `ydraw-yaml` | YAML-driven figure construction | ✓ |
| `yfigure` | Figure/container model — the compositor that hosts rich content | ✓ |
| `ygrid` | Figure: spatial-bucketed batch of SDF primitives + glyphs | ✓ |
| `ysdf` | SDF primitive handler (shape parse + construction) | ✓ |
| `ywebgpu` | WebGPU request/limits/utils glue ([webgpu](webgpu.md)) | ✓ |

## Fonts & text

| Module | Purpose | |
|---|---|---|
| `yfont` | Glyph atlas + GPU resource binding font interface ([font](../src/yetty/yfont/README.md)) | ✓ |
| `ymsdf` | MSDF (multi-channel SDF) font generation | ◐ |
| `ymsdf-gen` | Standalone MSDF generator tool | ◐ |
| `ymsdf-wgsl` | GPU MSDF atlas generation shaders | ◐ |
| `yfsvm` | Shader-compilation VM: math-expression bytecode for the GPU ([yfsvm](../src/yetty/yfsvm/README.md)) | ✓ |
| `yexpr` | Expression parser feeding plots and yfsvm | ✓ |

## Rich content figures

| Module | Purpose | |
|---|---|---|
| `yplot` | GPU charts / data visualization ([plot-enhanced](../src/yetty/yplot/README.md)) | ✓ |
| `yimage` | Inline images (PNG/JPEG/WebP) | ✓ |
| `yvideo` | Video playback (H.264 + MP4 parser) | ◐ |
| `ygui` | Native widget toolkit (buttons, menus, tables, dialogs, …) | ✓ |
| `ymgui` | Compositor-side GUI figure (yclass-based) | ✓ |
| `ydiagram` | Mermaid diagram parser + Sugiyama layout + render | ✓ |
| `ysvg` | SVG (Tiny 1.2) renderer → ydraw primitives | ✓ |
| `ypdf` | PDF rendering via pdfio (MuPDF-validated tests) | ✓ |
| `ymarkdown` | Markdown rendering/editing | ◐ |
| `yrich` | Documents / spreadsheets / slides (`ydoc`, `ysheet`/`yspreadsheet`, `yslides`) | ◐ |
| `ycat` | MIME-dispatched content viewer (detects PDF/image/SVG/Mermaid/video/text) | ✓ |
| `ymesh` | 3D mesh loading/rendering (GLB) | ◐ |
| `ythorvg` | SVG + Lottie via ThorVG (C interface only, no impl yet) | ○ |
| `yecho` | Text/glyph/block parser → ydraw buffer (demo/utility) | ✓ |
| `ymaze`, `yjungle`, `yzoo` | Animated test scenes (maze solver, SDF jungle, control-point zoo) | ◐ |

## Web rendering

| Module | Purpose | |
|---|---|---|
| `ylexbor` | Permissive-license web stack (lexbor HTML/CSS + QuickJS JS) — the going-forward engine | ◐ |
| `ybrowser` | Earlier web renderer (libcss + QuickJS) | ◐ |
| `ynetsurf` | NetSurf engine integration shim (GPL) | ◐ |

## Remote, transport & RPC

| Module | Purpose | |
|---|---|---|
| `yface` | OSC/DCS stream semantic layer (base64 / LZ4F, envelope codec) | ✓ |
| `ywire` | Envelope framer + decode stack + dispatcher (the wire state machine) | ✓ |
| `ytransport` | Polymorphic byte-stream transport for PTYs | ✓ |
| `yssh` | SSH PTY backend (libssh2) | ✓ |
| `ytelnet` | Telnet PTY backend | ✓ |
| `yctl` | TCP RPC control server (terminal automation) | ✓ |
| `yrdawn` | Remote WebGPU canvas as a compositor figure (client + server) | ✓ |
| `yvnc` | VNC client (RFB 3.8), GPU-decoded frames | ✓ |
| `ydvnc` | Desktop VNC viewer integrated as a yui view | ✓ |

## Media codecs

| Module | Purpose | |
|---|---|---|
| `yvcodec` | H.264 video decode (openh264) | ◐ |
| `yacodec` | Audio codec dispatch | ◐ |
| `yaudio` | WAV reader + audio playback (for yvideo) | ◐ |

## Emulation

| Module | Purpose | |
|---|---|---|
| `yqemu` | QEMU RISC-V VM spawner (boots alpine, forwards host TCP → guest telnetd) | ◐ |
| `src/tinyemu` | Vendored TinyEMU RISC-V VM (lightweight in-process VM / WebASM console) | ✓ |

## Platform

| Module | Purpose | |
|---|---|---|
| `yplatform` | Cross-platform layer: PTY, pipes, sockets, audio, clipboard, window manager, paths, process, threading, time ([platform](platform.md)) | ✓ |
| `yncbin` | Embedded (incbin) asset management — brotli-compressed shaders/fonts/configs baked into the binary | ✓ |
| `src/libvterm-0.3.3` | Vendored libvterm (VT100/xterm emulation) | ✓ |

---

## How the pieces connect

- **Input → screen:** PTY bytes → `ywire` → raw text into libvterm or
  DCS/OSC envelopes into ydraw / yfigure handlers → content layer + root
  `yfigure` compositor → `yrender` → screen.
- **Rich content:** a child process emits DCS figure records; `ywire` dispatches
  them to the root `yfigure` container. The figure (yplot, yimage, ymgui,
  yrdawn, ygrid, …) composites by z-order after the terminal content layer.
- **Remote rendering:** `yrdawn` lets a remote (or WebAssembly) process render
  WebGPU content that yetty composites as a figure; `yctl` exposes an RPC control
  plane; `yvnc`/`ydvnc` bring remote desktops in as content.

See the [Design Overview](design.md) for the cross-cutting decisions (C,
vtables, result types, dirty-driven pipeline) that every module follows.
