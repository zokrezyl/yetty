# Yetty Glossary

Project vocabulary, one term per entry, alphabetically sorted. Use these terms
consistently in code, comments, generated names, documentation, and discussion.
Entries that are just an alternate spelling point at the canonical term.

For the full module index see [Architecture & Module Map](architecture.md).
One standing rule cuts across many entries: keep wire/data/model vocabulary
(drawable list, primitive, resource, composite, figure) separate from
GPU/render vocabulary (renderer, render instance, GPU resource set, binder,
pipeline). A model object may store state for content, but rendering is an
orthogonal concern handled by renderer/registry code.

## annotation

A `[[clang::annotate("...")]]` marker on a hand-written C struct or function
that declares the yclass object model: `class@<domain>:<class>`, `parent@`,
`uses@`, `mixin@`, `local@`, `override@`, `expose`. Annotations are the single
source of truth that [codegen](#codegen) parses. An `override@` names only the
base slot being overridden (`override@<base_domain>:<base_class>:<method>`) —
the overriding class is contextual and never written. See
[yclass](../src/yetty/yclass/README.md).

## binder

The [yrender](#yrender) component that flattens a tree of
[GPU resource sets](#gpu-resource-set) into one storage buffer, per-format
atlas textures, and one packed uniform block, generating the WGSL
offset/region constants. This lifts WebGPU binding-count limits. See
[GPU Resource Binding](gpu-resource-binding.md) and
[yrender](../src/yetty/yrender/README.md).

## card

Deprecated name for [figure](#figure). It survives only as the OSC wire
keyword (`{card=plot; …}`) and in a few input-event type names. Do not use it
in new code, docs, or discussion.

## codegen

The yclass code generator (`src/yetty/yclass/gen/codegen.py`, run via
`make codegen`). It parses the [annotations](#annotation) in hand-written `.c`
sources and emits [model.yaml](#modelyaml), the `*.gen.{c,h}` glue
(accessors, [stubs](#stub), [skeletons](#skeleton), registration), and the
public api headers. Generated files are never hand-edited; to change them,
edit the annotated source and re-run codegen. It parses with the build's own
flags from `compile_commands.json`, so it requires a configured build. See
[yclass](../src/yetty/yclass/README.md).

## composite

A heavyweight non-primitive [ydraw](#ydraw) entry: plot, image, video,
mesh, shader-backed content. Composites occupy the top wire type-id tier and
are not lowered into [yscene](#yscene) primitives by the basic primitive path —
they carry their own payload and, eventually, a runtime scene object. The old
`raw_figure` / `complex_drawable` wire record names mean this. Do not call a
composite a "figure" (see [figure](#figure)).

## compositor

The root [yfigure](#yfigure) container of a terminal. It hosts
[figures](#figure) keyed by child id and renders them in z-order after the
[content layer](#content-layer). See
[yfigure](../src/yetty/yfigure/README.md) and
[Design Overview](design.md).

## content layer

The per-terminal layer that carries classic terminal content: the libvterm
text grid plus the scrolling ydraw canvas (and the shader-glyph figure).
Owned by [yterminal](#yterminal), implemented in
[yvterm](#yvterm); it renders before the [compositor](#compositor).

## Dawn

The WebGPU implementation yetty links against (through the C header). Dawn is
fetched at configure time — the `webgpu.h` the build sees comes from the
fetch, not from a committed copy. See [WebGPU](webgpu.md).

## DCS

Device Control String — the escape-sequence channel that carries binary
payloads (ydraw streams, figure records, RPC frames) from a child process
through the PTY into yetty. See [envelope](#envelope).

## dirty flag

The trigger of the dirty-driven pipeline: nothing re-renders or re-uploads
unless a dirty flag is set. Layers, figures, and resource sets all carry
dirty state; a clean frame costs almost nothing. See
[Design Overview](design.md).

## domain

The first segment of a yclass name, `class@<domain>:<class>` — normally the
module name (`yfigure`, `yscene`, `yterminal`, …). Domain plus class form the
qualified name (e.g. `yetty_yfigure_container`) that keys RPC name
resolution. See [yclass](../src/yetty/yclass/README.md).

## draw list

See [ydraw list](#ydraw-list).

## drawable list

The in-code spelling of [ydraw list](#ydraw-list):
`struct yetty_ydraw_drawable_list`. Use `drawable_list` in identifiers.

## envelope

The framed unit rich content travels in over the terminal byte stream:
an [OSC](#osc)/[DCS](#dcs) sequence wrapping an encoded (base64 or LZ4F)
payload. [yface](#yface) defines the semantic layer and codec;
[ywire](#ywire) frames, decodes, and dispatches envelopes to their handlers.

## figure

The rich-content unit hosted by the [compositor](#compositor): a yclass
object deriving from `yfigure:figure`, with rect, z, hidden/dirty state, and
input routing. Concrete figure [kinds](#kind) are renderer implementations
(yscene, ymgui, yrdawn, vterm). Never "card". Avoid "figure" for entries
inside a ydraw stream — those are [primitives](#primitive),
[composites](#composite), or [resources](#resource). See
[yfigure](../src/yetty/yfigure/README.md).

## figure container

The `yfigure:container` class — a [figure](#figure) that hosts child figures.
The root container of a terminal is the [compositor](#compositor).

## GPU resource set

`struct yetty_yrender_gpu_resource_set` — the bundle of buffers, textures,
uniforms, shader code, and child resource sets a renderer produces. The
[binder](#binder) flattens the resource-set tree into a single binding set.
See [GPU Resource Binding](gpu-resource-binding.md).

## kind

The registered name of a figure renderer implementation — not a content
type. A kind is identified by `yetty_yfigure_kind_token("<name>")` (a
header-inline hash; no central enum) and maps to a factory in the yfigure
registry. Content types live in the [ydraw](#ydraw) stream as wire type
words; producer content ships as [composite](#composite) records inside a
[yscene](#yscene) stream. New kinds are reserved for genuinely different
renderers. See [yfigure](../src/yetty/yfigure/README.md).

## libvterm

The vendored VT100/xterm emulation library (`src/libvterm-0.3.3`) backing
the text grid of the [content layer](#content-layer).

## model.yaml

The per-module canonical class model emitted by [codegen](#codegen)
(classes, slots, signatures, hierarchy) — the input for FFI bindings and RPC.
It is a pure codegen output and is never hand-edited.

## MSDF

Multi-channel signed distance field — the glyph representation used for
resolution-independent text rendering. Generated by the `ymsdf*` modules and
consumed through [yfont](#yfont). See [SDF](#sdf).

## OSC

Operating System Command — the escape-sequence channel (alongside
[DCS](#dcs)) used for yetty's rich-content [envelopes](#envelope). The
legacy `card=` keyword lives at this layer.

## primitive

A direct [yscene](#yscene) render atom: an SDF shape or a glyph — already in
the form the render staging path can place and render, needing no
materialization into a separate scene object. Fonts are
[resources](#resource), text runs are a
[text drawable list](#text-drawable-list), and heavyweight content is a
[composite](#composite) — none of those are primitives.

## producer

A process or module that emits [ydraw](#ydraw)/figure content into a
terminal: ychart, ydiagram, ysvg, ymarkdown, ymusic, ygui apps, the `tools/*`
CLIs. Producer-side code builds [drawable lists](#ydraw-list) and drives
figures over [yrpc](#yrpc); receiver-side code (yvterm, yscene) decodes and
renders them.

## resource

Reusable data declared or referenced by drawable-list entries — a font blob,
a font hash reference, future image/shader/cache declarations. Resources are
not visible by themselves; they are dependencies for later entries. FONT is
a font resource — not a primitive, not a drawable.

## Result type

The error-handling convention: every function that can fail returns a
`struct <name>_result` declared with `YETTY_YRESULT_DECLARE`, carrying either
the success value or a `yetty_ycore_error` with a heap-linked cause chain.
See [Result Types](result.md).

## rolling-row scroll

The O(1) scrolling model: anchored content stores the row it was created at,
and the shader subtracts the current top row — scrolling never rewrites
coordinates. Shared by the text grid, the ydraw canvas, and scrolling
figures.

## SDF

Signed distance field — the shape representation evaluated in the fragment
shader; the basis of yetty's vector rendering (shapes, glyphs). SDF paint
primitives form their own [ydraw](#ydraw) wire tier, handled by
[ysdf](../src/yetty/ysdf/README.md). See also [MSDF](#msdf).

## session

An [yrpc](#yrpc) session: one client's RPC channel to one terminal. The
session root is the terminal object; `yetty_yclass_rpc_connect()` returns it
as a typed proxy, and navigation proceeds through object-returning slots
(e.g. terminal → root [figure container](#figure-container)). Client-side
objects with a session set dispatch over the wire; without one they call
locally.

## skeleton

The server-side generated dispatch shim (a "skel") for one yclass method: it
decodes an RPC request, invokes the implementation, and encodes the
response. Counterpart of the [stub](#stub). See
[yclass](../src/yetty/yclass/README.md).

## stub

The client-side generated typed caller for a yclass method — same signature
as the local call. A stub checks the object's [session](#session): remote →
encode and ship over the wire; local → dispatch directly. Same call site
works in both cases. Counterpart of the [skeleton](#skeleton).

## text drawable list

The text-specific drawable-list entry (`TEXT_DRAWABLE_LIST` wire id):
compact text and layout data that resolves into an ordered list of glyph
[primitives](#primitive) after font/[resource](#resource) resolution. It is
neither a primitive, nor a resource, nor a composite. In identifiers:
`text_drawable_list`.

## TinyEMU

The vendored RISC-V VM (`src/tinyemu`) that powers the in-process/WebASM
console (not "JSLinux") and one of the PTY backends.

## ycat

The MIME-dispatched content viewer command: detects PDF, image, SVG,
Mermaid, video, or text and emits the matching figure/ydraw content. See
[ycat](../src/yetty/ycat/README.md).

## yclass

The annotation-driven class/object framework: classes are declared with
[annotations](#annotation) on hand-written C, and [codegen](#codegen) emits
the dispatch glue, [model.yaml](#modelyaml), RPC
[stubs](#stub)/[skeletons](#skeleton), and FFI binding input. New object
APIs use yclass by default; hand-rolled method families are the exception.
See [yclass](../src/yetty/yclass/README.md).

## ycore

The foundation module: [Result types](#result-type) and error chains, math,
buffers, utilities. Everything builds on it. See
[ycore](../src/yetty/ycore/README.md).

## yctl

The external control client and its TCP msgpack-RPC server, for driving a
running yetty from outside (keystrokes, mouse, resize, UI-tree dump):
`tools/yctl-client/yctl.py`, server enabled with `-r <port>`. Formerly named
"yrpc" — that name now means the internal object-RPC layer
([yrpc](#yrpc)); the two are different things. See
[yctl](../src/yetty/yctl/README.md).

## ydoc

The document editor — part of [yrich](#yrich), not its own module.

## ydraw

The 2D content model and wire format: serialized streams of
[primitives](#primitive), [composites](#composite), [resources](#resource),
and commands that scroll with terminal content. Implemented by the `ydraw-*`
module family — [ydraw-core](../src/yetty/ydraw-core/README.md) (drawable
list, type registry, streaming iterator, wire commands),
[ydraw-factory](../src/yetty/ydraw-factory/README.md),
[ydraw-yaml](../src/yetty/ydraw-yaml/README.md),
[ydraw-gen](../src/yetty/ydraw-gen/README.md).

## ydraw list

The ordered container/stream of [ydraw](#ydraw) entries — in code
`struct yetty_ydraw_drawable_list` (spoken: [drawable list](#drawable-list)).
It is the producer-side object used to emit ydraw content and may contain
direct primitives, composites, resources, commands, and text drawable lists.
Related identifiers follow the same base: `drawable_list_entry`,
`drawable_list_registry`, `drawable_list_entry_ops` — a bare "entry" type
does not appear in public naming. See
[ydraw-core](../src/yetty/ydraw-core/README.md).

## yetty

The project — a GPU-accelerated terminal in pure C that renders rich
content alongside text — and also the top-level application module
`src/yetty/yetty` that owns the terminal app instance. See
[yetty](../src/yetty/yetty/README.md).

## yface

The OSC/DCS semantic layer: [envelope](#envelope) definitions and codec
(base64 / LZ4F). See [yface](../src/yetty/yface/README.md).

## yfigure

The figure/container module: the `figure` base class, the `container`
([compositor](#compositor)), and the [kind](#kind) registry. See
[yfigure](../src/yetty/yfigure/README.md).

## yfont

Glyph atlas and font interface for GPU text rendering. Naming caveat: the
directory is `yfont` but the symbol prefix is `yetty_font_`. See
[yfont](../src/yetty/yfont/README.md).

## yframework

The generic GPU/event/RPC services layer: WebGPU adapter, device, queue,
allocator, MSDF generator, render target, and the optional VNC and RPC
servers. Sits between platform bootstrap and the application. See
[yframework](../src/yetty/yframework/README.md).

## ygui

The native widget toolkit (buttons, menus, tables, dialogs, …). ygui runs
client-side as a [producer](#producer) driving figures over the wire. See
[ygui](../src/yetty/ygui/README.md).

## ymgui

The compositor-side GUI figure [kind](#kind): renders an imgui-style draw
payload shipped over the wire. See
[ymgui](../src/yetty/ymgui/README.md).

## yplatform

The cross-platform layer: PTY, pipes, sockets, audio, clipboard, window
manager, paths, process, threading, time. All platform `#ifdef`s stay inside
it; everything else is platform-independent. See
[yplatform](../src/yetty/yplatform/README.md).

## yrdawn

The remote-WebGPU figure [kind](#kind): a remote (or WebAssembly) process
renders WebGPU content that yetty composites as a figure. The old name
"ymux" is dead. See [yrdawn](../src/yetty/yrdawn/README.md).

## yrender

The GPU pipeline module: resource-set [binder](#binder), allocator,
pipelines, render targets. See
[yrender](../src/yetty/yrender/README.md) and
[GPU Resource Binding](gpu-resource-binding.md).

## yrich

Documents, spreadsheets, and slides in one module: [ydoc](#ydoc),
[ysheet](#ysheet), and [yslides](#yslides) live here — they are not separate
modules. See [yrich](../src/yetty/yrich/README.md).

## yrpc

The in-app yclass RPC / remote-object layer: [sessions](#session),
[stubs](#stub), [skeletons](#skeleton), and object proxies that let the same
typed call site drive a local object or a remote one over the multiplexed
[ywire](#ywire) connection. Not the external control client — that is
[yctl](#yctl). See [yclass](../src/yetty/yclass/README.md).

## yscene

The general-purpose figure [kind](#kind) for [ydraw](#ydraw) content: the
retained scene graph — a dom of nodes (groups) holding verbatim wire
spans, derived into one paint-ordered leaf list, rendered as SDF
[primitives](#primitive), expanded glyphs, and [composites](#composite),
with update/delete/grouping addressable by node id. Also answers to the
legacy wire kind token "ygrid". See
[yscene](../src/yetty/yscene/README.md).

## ysdf

The SDF-primitive handler module: shape parsing and construction for the
SDF tier of the [ydraw](#ydraw) wire format. See
[ysdf](../src/yetty/ysdf/README.md).

## ysheet

The spreadsheet (also "yspreadsheet") — part of [yrich](#yrich), not its own
module.

## yslides

The slide deck — part of [yrich](#yrich), not its own module.

## yterminal

The terminal view module: each terminal owns one
[content layer](#content-layer) plus the root [compositor](#compositor), and
handles PTY, scrollback, selection, and input forwarding. The terminal object
(`yterminal:terminal`) is the root of an RPC [session](#session). See
[yterminal](../src/yetty/yterminal/README.md).

## ytrace

Switchable trace/log points with near-zero cost when off; enable all with
`YTRACE_DEFAULT_ON=yes`. The preferred debugging tool over gdb. See
[ytrace](../src/yetty/ytrace/README.md).

## yui

The application UI: tabs, tiled panes, workspaces, settings/debug windows.
See [yui](../src/yetty/yui/README.md).

## yview

The client-side emitter for a server-side scrollable figure — a
[producer](#producer) built on [yrpc](#yrpc). See
[yview](../src/yetty/yview/README.md).

## yvterm

Terminal content internals: the libvterm text grid, the scrolling ydraw
canvas, the shader-glyph figure, and the scroll/alt-screen cross-wiring
behind the [content layer](#content-layer). See
[yvterm](../src/yetty/yvterm/README.md).

## ywire

The wire state machine: [envelope](#envelope) framer, decode stack,
dispatcher, and the multiplexed connection/channel model (one connection per
process over the shared PTY; N clients open N dynamic channels). See
[ywire](../src/yetty/ywire/README.md).
