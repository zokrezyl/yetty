<p align="center">
  <img src="docs/pres.gif" alt="Yetty demo: terminal text, rich figures, plots, documents, and GUI surfaces" width="900">
</p>

# Yetty (https://yetty.dev)

Yetty is a GPU-accelerated terminal and rich-content runtime. It keeps normal
terminal workflows intact, but lets programs place plots, images, diagrams,
documents, video, GUI panels, remote desktops, and AI-generated figures directly
into the same scrolling surface as text.

Pure C. WebGPU-rendered. FFI-first. Built around small composable modules.

> **License:** Business Source License 1.1 for Yetty's own code. Non-production
> use is free; production use requires a commercial license. Bundled third-party
> components keep their own licenses. See [LICENSE](LICENSE) and
> [DEPENDENCIES.md](DEPENDENCIES.md).

> **Status:** Early alpha. Many core paths are usable, but APIs and internals are
> still changing quickly.

## Install

Linux and macOS:

```bash
curl -fsSL https://yetty.dev/install.sh | bash
```

Windows (PowerShell):

```powershell
irm https://yetty.dev/install.ps1 | iex
```

The script downloads the installer for your platform from the latest release
and runs it. The installer is a single self-contained executable that unpacks
everything it carries into the right per-OS locations. It comes in three
sizes, each with its own one-liner:

| Installer | Script | Contains | Download |
|-----------|--------|----------|---------:|
| `yinstall-min` | `install-min.sh` / `install-min.ps1` | `yetty` alone, its shaders and the raw TTF/OTF fonts, the default config, and `libyetty_ffi` for the language bindings. yetty builds the MSDF font atlases from the raw fonts on its first start (about a second on an old GPU). | ~55 MB |
| `yinstall` | `install.sh` / `install.ps1` | Every yetty executable: the terminal, the companion tools (`ycat`, `yplot`, `ygreeter`, `ydoc`, `ysheet`, `yslide`, …) and the demo programs, `libyetty_ffi`, plus shaders, raw fonts **and** the pre-generated MSDF font atlases, config, greeter and demo assets. | ~255 MB |
| `yinstall-max` | `install-max.sh` / `install-max.ps1` | Everything in `yinstall`, plus the RISC-V VM runtime (kernel, firmware, root filesystem) and the QEMU emulator behind `--temu` / `--qemu`. | ~380 MB |

```bash
curl -fsSL https://yetty.dev/install-min.sh | bash     # smallest: the terminal
curl -fsSL https://yetty.dev/install-max.sh | bash     # everything, VM included
```

The same script also takes the variant as an argument
(`curl -fsSL https://yetty.dev/install.sh | bash -s -- --max`) or from
`YETTY_VARIANT`; on Windows set `$env:YETTY_VARIANT` before the pipe. Every
installer is idempotent: run it again to repair an install, or with `--force`
to overwrite files that are already there. To install manually, grab the
archive for your platform and size from
[Releases](https://github.com/zokrezyl/yetty/releases/latest)
(`yetty-<platform>.tar.gz`, `-min`, `-max`; `.zip` on Windows) or see
[src/yetty/yinstall/README.md](src/yetty/yinstall/README.md).

## What Yetty Is

- **A terminal application**: VT/xterm terminal emulation, PTY backends, tabs,
  panes, tiled workspaces, scrollback, selection, and platform integration.
- **A figure compositor**: rich content is represented as nested `yfigure`
  objects with size, dirty state, GPU resources, and z-order.
- **A rich terminal protocol**: child processes can emit OSC/DCS envelopes that
  Yetty decodes into row-anchored figures.
- **A toolkit for terminal-native apps**: `ygui`, `ymgui`, `ydraw`, `ygrid`,
  `yplot`, `ycat`, and other modules let tools render more than text.
- **An AI-agent work surface**: `yai` and the Yetty MCP server let agents chat,
  run tools, and draw rich content directly inside the terminal.
- **A C library ecosystem**: modules are designed for explicit ownership,
  generated bindings, and language FFI rather than a single monolithic app.

## AI-Native Terminal

Yetty includes `yai`, a terminal-native AI shell under `tools/ai/yai`. It drives
headless agent CLIs such as Claude Code, Codex, and Gemini over JSONL. The chat
scrolls like normal terminal output, while a non-scrolling `ygui` HUD shows
state, token and cost data, activity, and controls.

Agents can also draw rich content through the Yetty MCP server at
`tools/mcp/poc/yetty_mcp.py`. The server exposes drawing tools backed by `ycat`
and related figure emitters, so an agent can render Markdown, Mermaid diagrams,
charts, plots, flame graphs, images, PDFs, SVG, LilyPond music, meshes, and other
content inline in the current Yetty session.

The result is a chat surface where text, tool output, rendered documents,
diagrams, charts, and interactive figures share one scrollback.

## Architecture

```text
main()
  -> yinit              platform bootstrap, config, assets, OS loop
  -> yframework         GPU/device/queue, allocator, events, RPC services
  -> yetty              terminal application
  -> yui/yterminal      tabs, panes, PTY, scrollback, input
      + content layer   libvterm text grid + row-anchored ydraw content
      + root yfigure    z-ordered rich-content compositor
```

The terminal content layer handles text and scrolling primitive content. The
root `yfigure` compositor hosts richer objects such as GUI surfaces, plots,
images, videos, PDFs, VNC desktops, remote WebGPU canvases, diagrams, and agent
figures.

For the full module inventory and maturity notes, start with
[docs/architecture.md](docs/architecture.md).

## Rich Content

Yetty's figure system currently covers:

| Area | Modules and tools |
|---|---|
| Primitive drawing | `ydraw`, `ydraw-list`, `ydraw-factory`, `ygrid`, `ysdf` |
| Charts and plots | `yplot`, `ychart`, `yflame`, `tools/yplot`, `tools/ychart`, `tools/yflame` |
| Documents and markup | `ycat`, `ymarkdown`, `ypdf`, `ysvg`, `yrich`, `tools/yless` |
| Diagrams and notation | `ydiagram`, `ycircuit`, `ymusic` |
| Media and 3D | `yimage`, `yvideo`, `ylottie`, `ymesh` |
| GUI surfaces | `ygui`, `ymgui`, `yguiapp`, `tools/ycompositor`, `tools/ygreeter`, `tools/yhello` |
| Remote content | `yrdawn`, `yvnc`, `ydvnc`, `yssh`, `ytelnet`, `yctl` |
| AI surfaces | `tools/ai/yai`, `tools/mcp/poc/yetty_mcp.py` |

Detailed status lives in [docs/architecture.md](docs/architecture.md), not in
this README, so the root overview stays stable as modules move.

## Quick Start

List build targets:

```bash
make
```

Build and run the desktop daily-driver configuration:

```bash
make build-desktop-ytrace-release
make run-desktop-ytrace-release
```

Run desktop tests for that configuration:

```bash
make test-desktop-ytrace-release
```

Build the FFI shared library:

```bash
make build-desktop-ffi-release
```

Serve the WebAssembly build:

```bash
make build-webasm-ytrace-release
make run-webasm-ytrace-release
```

Build outputs follow the pattern shown by `make help`, for example
`build-desktop-ytrace-release/yetty` and `build-webasm-ytrace-release/yetty.html`.

## Build Targets

Common target families:

| Target family | Purpose |
|---|---|
| `build-desktop-*` | Linux/macOS desktop builds |
| `run-desktop-*` | Run an existing desktop build |
| `test-desktop-*` | Run desktop test targets |
| `build-webasm-*` / `run-webasm-*` | WebAssembly build and local serve |
| `build-android-*` / `test-android-*` | Android APK builds and device/emulator runs |
| `build-windows-*` / `run-windows-*` | Windows MSVC builds |
| `build-ios-*`, `build-tvos-*` | Apple mobile and simulator builds |
| `build-linux-aarch64-*`, `build-linux-riscv-*` | Linux cross-builds |
| `build-desktop-ffi-release` | PIC build for `libyetty_ffi.so` |

Logging/build variants:

- `ytrace`: full tracing and logging; the normal development build.
- `yinfo`: reduced logging for release and performance testing.
- `debug`, `release`, `asan`: standard build-type variants where available.

## Developer Workflow

Generated `yclass` output is committed. Platform builds compile what is already
in the tree; they do not run code generation automatically.

```bash
# Regenerate yclass models, generated C, RPC stubs, and public headers
make codegen

# Generate FFI language bindings from the committed model.yaml files
make ffi

# Format C/H sources
make format-code
```

Use `make help` for the authoritative list of build, run, test, codegen, FFI,
and platform targets.

## Platforms

The build system has targets for:

| Platform | Notes |
|---|---|
| Linux | Primary desktop development platform |
| macOS | Desktop build target |
| Android | Device and x86_64 emulator APK targets |
| WebAssembly | Browser WebGPU build |
| Windows | MSVC build target |
| iOS / tvOS | Device and simulator build targets |
| Linux aarch64 / riscv64 | Cross-build targets; riscv64 is tools/demos oriented |

Platform quality varies by subsystem. See
[src/yetty/yplatform/README.md](src/yetty/yplatform/README.md)
and [docs/architecture.md](docs/architecture.md) for lower-level details.

## Design Principles

- **Pure C, FFI-first**: explicit ABI-friendly module boundaries and generated
  language bindings.
- **Figure-based composition**: rich content and terminal content share one
  composited surface.
- **Dirty-driven rendering**: uploads and redraws happen only when something
  changed.
- **GPU resource binding**: buffers and textures are packed into predictable
  WebGPU binding sets.
- **Small modules**: the codebase is split into focused libraries under
  `src/yetty/`, with tools and demos layered on top.
- **Protocol-friendly output**: terminal programs can produce rich figures by
  writing structured envelopes, while still behaving like normal CLIs.

## Documentation

Start here:

| Document | Description |
|---|---|
| [Architecture & Module Map](docs/architecture.md) | Current module inventory, ownership chain, and maturity notes |
| [Design Overview](docs/design.md) | Core decisions and rationale |
| [Contexts](docs/contexts.md) | Bootstrap chain and context structs |
| [Layered Rendering](docs/layered-rendering.md) | Terminal content layer, root figures, scrolling, and alt-screen behavior |
| [Render Pipeline](src/yetty/yrender/README.md) | Dirty-driven upload, compilation, and draw flow |
| [GPU Resource Binding](docs/gpu-resource-binding.md) | Buffer packing and atlas texture model |
| [Platform Abstraction](src/yetty/yplatform/README.md) | PTY, event loop, paths, windows, OS services |
| [FFI Generation](docs/ffi-gen.md) | Binding model and generated metadata |
| [C Coding Style](docs/c-coding-style.md) | Naming, structs, memory, and result rules |

Subsystem references:

Every module under `src/yetty/` carries its own `README.md`; common entry
points:

| Area | Document |
|---|---|
| Drawing primitives | [src/yetty/ydraw/README.md](src/yetty/ydraw/README.md) |
| Charts | [src/yetty/ychart/README.md](src/yetty/ychart/README.md) |
| Plots | [src/yetty/yplot/README.md](src/yetty/yplot/README.md) |
| Flame graphs | [src/yetty/yflame/README.md](src/yetty/yflame/README.md) |
| Fonts | [src/yetty/yfont/README.md](src/yetty/yfont/README.md) |
| Shader expression VM | [src/yetty/yfsvm/README.md](src/yetty/yfsvm/README.md) |
| Class/codegen system | [src/yetty/yclass/README.md](src/yetty/yclass/README.md) |
| VNC | [src/yetty/yvnc/README.md](src/yetty/yvnc/README.md) |
| Tracing | [src/yetty/ytrace/README.md](src/yetty/ytrace/README.md) |
| Terminal view | [src/yetty/yterminal/README.md](src/yetty/yterminal/README.md) |
| Wire protocol (OSC/DCS codes) | [src/yetty/ywire/README.md](src/yetty/ywire/README.md) |
| Coroutines | [src/yetty/yco/README.md](src/yetty/yco/README.md) |
| Installer | [src/yetty/yinstall/README.md](src/yetty/yinstall/README.md) |

## Dependencies

Yetty uses Dawn/WebGPU, libvterm, FreeType, GLFW, libuv, libco, brotli, pdfio,
OpenH264, QuickJS, lexbor, libssh2, LZ4, cdb, TinyEMU, and other libraries
depending on enabled features. Most bundled dependencies are permissively
licensed. Optional NetSurf integration is GPL and off by default.

The authoritative dependency and license list is
[DEPENDENCIES.md](DEPENDENCIES.md).

## Contributing

The project is moving quickly, but contributions are welcome:

- Bug fixes and focused code improvements
- Documentation updates
- Tests and reproducible issue reports
- Feedback on protocols, tools, bindings, and platform behavior

Use [GitHub Discussions](https://github.com/zokrezyl/yetty/discussions) for
larger design ideas.

---

Your terminal, unchained.
