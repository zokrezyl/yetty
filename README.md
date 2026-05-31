> **Note:** This is the repo of the new rewrite of yetty code. The old yetty code, is at https://github.com/zokrezyl/yetty-poc


<p align="center">
  <img src="docs/pres.gif" alt="Yetty demo — MSDF text, inline plots, ycat README, ygui/ymgui" width="900">
</p>

# Yetty

GPU-accelerated terminal with rich content. Pure C. Any language.

> **License:** Business Source License 1.1. Non-production use is free; production use requires a commercial license. See [LICENSE](LICENSE).

> **Status:** Early alpha — actively rewriting established concepts for efficiency.

## Vision

Terminals are stuck in the 1970s — text, maybe colors, that's it. Meanwhile, the rest of computing evolved to support rich graphics, animations, and interactive content.

**Yetty changes this.** A WebGPU-powered terminal where plots, images, videos, documents, and interactive widgets live alongside text — all scrolling together as one unified surface.

## Design Principles

- **Pure C, FFI-first** — no hidden costs, bind from Rust, Go, Python, Swift, Kotlin
- **Layered rendering** — text and graphics coexist, scroll together
- **Composable primitives** — simple (SDF shapes) and complex (figures)
- **Dirty-driven pipeline** — nothing runs unless something changed
- **GPU resource binding** — all buffers and textures packed into minimal GPU bindings

## Architecture

```
Terminal
  ├── text-layer        libvterm (VT100/xterm)
  ├── ydraw-layer       SDF primitives: circles, boxes, lines, glyphs
  └── figure container  the compositor — renders after the layers
        └── figures: yplot · yimage · yvideo · ygui · ymgui · yrdawn ·
                     ydiagram · ysvg · ypdf · yvnc · ...
```

A **figure** is a complex primitive that integrates with the terminal grid: it
scrolls with text, shares the GPU resource model, and can be nested (figures may
contain other figures). The terminal renders two real layers (text and ydraw)
plus a `yfigure` container that hosts the rich content.

> Yetty is built from ~70 small C modules. See the **[Architecture & Module
> Map](docs/architecture.md)** for the complete inventory and how the pieces
> connect.

## Rich Content Figures

Figures are complex primitives that integrate seamlessly with the terminal grid.
They scroll with text, share the GPU resource model, and can be nested.

| Figure | Description | Status |
|------|-------------|--------|
| **yplot** | GPU-accelerated charts and data visualization | ✅ Working |
| **yimage** | Inline images (PNG, JPEG, WebP) | ✅ Working |
| **ygui** | Interactive widgets (buttons, menus, tables, dialogs) | ✅ Working |
| **ymgui** | Compositor-side GUI figure | ✅ Working |
| **yvnc** / **ydvnc** | VNC client + desktop viewer | ✅ Working |
| **ydiagram** | Mermaid diagrams (parser + layout + render) | ✅ Working |
| **ysvg** | SVG (Tiny 1.2) rendering | ✅ Working |
| **ypdf** | PDF rendering (via pdfio) | ✅ Working |
| **ycat** | MIME-dispatched content viewer | ✅ Working |
| **yvideo** | Video playback (H.264) | 🚧 Beta |
| **ymarkdown** | Markdown rendering/editing (WYSIWYG) | 🚧 Porting |
| **yrich** | Documents, spreadsheets, presentations | 🚧 Porting |
| **ymesh** | 3D mesh rendering | 🚧 Early |
| **ythorvg** | SVG and Lottie animations (ThorVG) | 📋 Planned |

## Beyond the desktop

Yetty is more than a renderer — it speaks to remote machines and embeds a web
stack.

| Capability | Module(s) | Status |
|---|---|---|
| **SSH / Telnet** | yssh, ytelnet | ✅ Working |
| **Remote GPU rendering** | yrdawn (client + server over OSC) | ✅ Working |
| **RPC control plane** | yctl | ✅ Working |
| **Web rendering** | ylexbor (lexbor + QuickJS), ybrowser | 🚧 Early |
| **RISC-V VM console** | yqemu, embedded TinyEMU | 🚧 Early |

## Core Features

| Feature | Description |
|---------|-------------|
| **MSDF fonts** | Crisp, scalable text at any zoom level |
| **Raster fonts** | Color emoji and bitmap glyphs |
| **SDF primitives** | GPU-rendered shapes with anti-aliasing |
| **Tiling workspaces** | Multiple terminals with window management |
| **Rolling scroll** | O(1) scroll — primitives never update coordinates |
| **ytrace logging** | Switchable trace points, near-zero cost when off |

## Platforms

| Platform | Status |
|----------|--------|
| Linux | ✅ Working |
| macOS | ✅ Working |
| Android | ✅ Working |
| WebAssembly | ✅ Working |
| Windows | 🚧 In progress |
| iOS / tvOS | 🧪 Experimental |

## Building

Build targets are defined in the `Makefile`. List available targets with:

```bash
make
```

Common build commands:

```bash
# Desktop (Linux/macOS) - release build with tracing
make build-desktop-ytrace-release

# WebAssembly
make build-webasm-ytrace-release

# Android (ARM)
make build-android-ytrace-release

# Android emulator (x86_64)
make build-android_x86_64-ytrace-release
```

## Usage

```bash
# Run with default shell
./build-desktop-ytrace-release/yetty

# Run with specific command
./build-desktop-ytrace-release/yetty -e 'htop'
```

## Documentation

Start with the **[Architecture & Module Map](docs/architecture.md)** for the full
picture, then dive into a subsystem.

**Overview**
| Document | Description |
|----------|-------------|
| [Architecture & Module Map](docs/architecture.md) | The ~70 modules, grouped, with maturity |
| [Design Overview](docs/design.md) | Core decisions and rationale |
| [Contexts](docs/contexts.md) | Bootstrap chain and context structs |

**Rendering**
| Document | Description |
|----------|-------------|
| [Layered Rendering](docs/layered-rendering.md) | Layer stack, compositor, blender |
| [WebGPU Architecture](docs/webgpu-architecture.md) | WebGPU object ownership |
| [WebGPU Concepts](docs/webgpu.md) | WebGPU primer (C) |
| [GPU Resource Binding](docs/gpu-resource-binding.md) | Buffer packing and atlas textures |
| [Render Pipeline](docs/render.md) | Dirty-driven upload and recompilation |
| [ydraw](docs/ydraw.md) | Primitives, figures, and scrolling model |
| [Font System](docs/font.md) | Glyph rendering and atlas |
| [yfsvm](docs/yfsvm.md) | Shader expression VM |
| [Enhanced Plots](docs/plot-enhanced.md) | yplot internals |

**Terminal & platform**
| Document | Description |
|----------|-------------|
| [Terminal Screen](docs/terminal-screen.md) | Screen state and compositing |
| [Terminal Layers](docs/term-layers.md) | Scrolling, alt-screen, rolling rows |
| [Platform Abstraction](docs/platform.md) | PTY, event loop, per-OS layout |
| [Platform PTY](docs/platform-pty.md) | PTY backends |
| [Platform Pipe](docs/platform-pipe.md) | Cross-thread input pipe |
| [Coroutines](docs/coroutines.md) | yco / yevent concurrency |
| [yvnc](docs/yvnc.md) | VNC client/server |

**Codegen & bindings**
| Document | Description |
|----------|-------------|
| [yclass](docs/yclass.md) | Annotation-driven classes, RPC, and the binding model |
| [FFI Generation](docs/ffi-gen.md) | Per-language binding emitters |

**Conventions & tooling**
| Document | Description |
|----------|-------------|
| [C Coding Style](docs/c-coding-style.md) | Naming, structs, memory rules |
| [Result Types](docs/result.md) | Typed error propagation |
| [ytrace](docs/ytrace.md) | Logging and tracing |
| [Buck2](docs/buck2.md) | Buck2 build notes |

## Contributing

We're developing intensively and moving fast. Contributions welcome:

- Code and bug fixes
- Documentation improvements
- Testing (coverage is still limited)
- Ideas and feedback

Share suggestions on [GitHub Discussions](https://github.com/zokrezyl/yetty/discussions).

## Dependencies

### Core
- **libvterm** — VT100/xterm terminal emulation (vendored)
- **Dawn** — WebGPU implementation
- **FreeType** — Font rasterization
- **GLFW** — Cross-platform windowing
- **libuv** — Async I/O and event loop
- **libco** — Coroutines
- **brotli** — Bundled-asset compression

### Content & remote
- **pdfio** — PDF parsing (ypdf)
- **openh264** — H.264 video decode (yvideo)
- **lexbor** + **QuickJS** — HTML/CSS/JS web stack (ylexbor)
- **libssh2** — SSH backend (yssh)
- **LZ4** — Wire-stream compression (yface)
- **cdb** — Constant key/value database (ycdb)
- **TinyEMU** — RISC-V VM console (vendored)

### Optional / planned
- **ThorVG** — SVG and Lottie rendering (wired in build; renderer in progress)

Dependencies use permissive licenses (MIT, BSD, Zlib, Apache-2.0); the optional
NetSurf integration (`ynetsurf`) is GPL and off by default.


---

*Your terminal, unchained.*
