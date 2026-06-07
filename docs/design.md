# Yetty Design Overview

Yetty is a WebGPU-based terminal emulator targeting desktop (Linux, macOS, Windows), Android, and WebAssembly.

This document describes the core design decisions. Each section gives a brief rationale and references a dedicated doc for details.

## Language: C

Yetty's core is written in C, not C++.

**Why C:**
- **FFI-first** — every component exposes a plain C interface. Any language (Rust, Go, Python, Swift, Kotlin) can bind to it directly. No name mangling, no vtable ABI issues, no exception unwinding across boundaries.
- **Simplicity** — no hidden costs. No implicit constructors, destructors, copies, moves, exceptions, or template instantiations. What you read is what executes.
- **Portability** — C compilers exist everywhere. Cross-compilation to Android NDK, Emscripten, and exotic targets is straightforward.

C++ is used only where required by external libraries (Dawn/WebGPU headers).

See [C Coding Style](c-coding-style.md) for naming conventions, struct patterns, and memory allocation rules.

## Polymorphism

Yetty uses vtable-based polymorphism in plain C: an ops struct holds function pointers, the base struct holds a pointer to the ops, and subclasses embed the base as their first member.

```c
/* Interface */
struct yetty_font_font_ops {
    uint32_t (*get_glyph_index)(struct yetty_font_font *self, uint32_t codepoint);
    int (*is_dirty)(const struct yetty_font_font *self);
};

/* Base */
struct yetty_font_font {
    const struct yetty_font_font_ops *ops;
};

/* Implementation embeds base as first member */
struct raster_font {
    struct yetty_font_font base;
    FT_Library ft_library;
    /* ... */
};

/* Call through ops */
uint32_t idx = font->ops->get_glyph_index(font, codepoint);
```

Downcasting uses `container_of`. No `void *priv` pointers. No inheritance hierarchies.

See [C Coding Style](c-coding-style.md) for the full vtable and embedding patterns.

This hand-written pattern is used for simple, fixed interfaces (fonts, render
targets, PTYs). For the richer object model — classes with single inheritance,
mixins, cross-module overrides, and **location-transparent RPC** (the same call
works on a local object or a remote proxy) — yetty uses **yclass**, where the
vtable, dispatch, RPC skeletons, and a binding model are *generated* from C23
`[[clang::annotate(...)]]` annotations on the source. The figure modules
(`yfigure`, `ygui`, `ymgui`, `yrdawn`, `ygrid`, `yterm`) are built this way. See
[yclass](../src/yclass/README.md).

## Error Propagation

Yetty uses typed result unions — similar to Rust's `Result<T, E>` — encoded as C structs with a tagged union.

```c
YETTY_YRESULT_DECLARE(yetty_font_font, struct yetty_font_font *);

/* Returns struct yetty_font_font_result { int ok; union { value; error; }; } */
struct yetty_font_font_result res = yetty_font_raster_font_create(config, 10, 20);
if (YETTY_IS_ERR(res)) {
    yerror("font: %s", res.error.msg);
    return YETTY_ERR(yetty_ycore_void, res.error.msg);
}
struct yetty_font_font *font = res.value;
```

No exceptions. No errno. Errors propagate explicitly through return values.

See [Result Types](result.md) for the macro definitions and usage patterns.

## Layered Rendering

The terminal screen used to be a fixed stack of render layers. The current
model is one composite content layer (text grid, row-anchored ydraw content, and
the text-owned shader-glyph figure) topped by a root `yfigure` container that
composites rich-content figures by z-order. Each renderable component:

- Owns a `struct yetty_yrender_gpu_resource_set` describing its GPU needs
- Has a `dirty` flag — no re-render unless content changed
- Implements the relevant render or figure ops (`get_gpu_resource_set`, `resize`, `render`, `destroy`, and figure dirty/z-order state where applicable)

The terminal only renders when the content layer or root figure tree is dirty. After rendering, dirty flags are cleared.

See [Layered Rendering](layered-rendering.md) for the virtual layer order, direct-to-target rendering, compositor design, scrolling, and alt-screen state.

## GPU Resource Set Tree

Each layer's `gpu_resource_set` forms a tree. The text layer's resource set has the font's resource set as a child. The binder flattens this tree and packs everything into shared GPU resources:

- **Mega buffer** — all storage buffers from all resource sets packed into one `array<u32>` with generated offset constants (`text_grid_buffer_offset`, `raster_font_buffer_offset`)
- **Per-format atlas textures** — all textures shelf-packed into atlas textures (one per format: R8, RGBA8) with generated UV region constants
- **Uniform struct** — all uniforms from all resource sets packed into one WGSL struct with WGSL alignment rules
- **Generated WGSL** — binding declarations, offset constants, and region constants are generated and prepended to the merged shader code

This eliminates WebGPU binding count limits. Any number of buffers and textures across any number of resource sets map to a fixed number of GPU bindings.

The binder operates in two phases:
- `finalize()` — one-time: flatten tree, create GPU objects, compile pipeline
- `update()` — per-frame: upload only dirty buffers/textures. If any size or dimension changed, re-finalize

See [GPU Resource Binding](gpu-resource-binding.md) for the resource set structure, binder flow, and binding layout. See [Render Pipeline](render.md) for the dirty-driven upload and pipeline recompilation logic.

## Dirty-Flag Driven Pipeline

Nothing runs unless something changed:

- **Cell buffer** — rebuilt only when vterm fires a damage callback (PTY data arrived)
- **Font atlas** — marked dirty only when new glyphs are rasterized on demand
- **GPU upload** — binder uploads only buffers/textures whose dirty flag is set
- **Pipeline recompilation** — only when buffer sizes, texture dimensions, or shader code changes (detected via hash comparison)
- **Render frame** — skipped entirely if no layer is dirty

First render event before PTY data: skipped (no dirty layers). First actual render: finalize + upload everything. Subsequent frames: upload only what changed.

## Logging: ytrace

Switchable trace points with five levels: `ytrace`, `ydebug`, `yinfo`, `ywarn`, `yerror`.

Each trace point is a function-local static bool. When disabled, the cost is a single `if` check — no string formatting, no IO. Points can be toggled at runtime by level, file, or function name. `YTRACE_DEFAULT_ON=yes` enables all points.

Compile-time switches (`YTRACE_C_ENABLE_TRACE`, etc.) can remove levels entirely — the macros become `((void)0)`.

See [Tracing](../src/yetty/ytrace/README.md) for the macro implementation, control API, and build-time configuration.

## Font Abstraction

Fonts implement the `yetty_font_font` ops interface. The current backend is raster (FreeType).

Key design points:
- **On-demand rasterization** — glyphs are rasterized when first requested via `get_glyph_index_styled()`, not preloaded
- **Atlas growth** — the font atlas starts small and grows dynamically as new glyphs are needed, using a shelf-packing algorithm
- **Dirty propagation** — when the atlas changes (new glyphs, resize), the font marks its texture and buffer dirty so the binder uploads only what changed
- **Style support** — regular, bold, italic, bold-italic with automatic fallback

See [Font System](../src/yetty/yfont/README.md) for the glyph resolver, atlas packing, and UV coordinate scheme.

## Context Hierarchy

Contexts are cheap POD structs passed by value. Each level contains a copy of its parent context plus level-specific state:

```
AppContext          — config, platform factory, input pipe
  └── YettyContext  — + device, queue, surface format
       └── TerminalContext  — + event loop, PTY
```

No global state. Everything a component needs comes through its context.

See [Contexts](contexts.md) for the full hierarchy and ownership rules.

## Platform Abstraction

Platform-specific code is isolated behind ops interfaces:

- **PTY** — Unix fd on desktop, buffer on WebASM, ConPTY on Windows
- **Event loop** — libuv poll on Unix, emscripten_async on WebASM
- **Input pipe** — fd-based notification on Unix, callback on WebASM

The shared code (GLFW window, libuv loop) lives in `src/yetty/yplatform/shared/`. Platform dirs contain only what differs.

See [Platform](platform.md), [Platform PTY](platform-pty.md), and [Platform Pipe](platform-pipe.md) for the abstraction layers and per-platform implementations.
