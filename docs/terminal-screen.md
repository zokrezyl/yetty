# Terminal Screen Design

## Overview

TerminalScreen is the core rendering component of yetty. It manages a layered rendering
pipeline where each layer composites on top of the cached output of the layer below,
enabling fast scrolling by only re-rendering layers that changed.

## Layer Architecture

```
Layer 3: Static YDraw     — screen-fixed SDF/MSDF overlay (dialogs, HUD)
Layer 2: Cards              — independent SDF/MSDF sub-grids, scroll with terminal
Layer 1: Scrolling YDraw   — SDF/MSDF primitives, 1-to-1 with text grid, scroll together
Layer 0: Text Grid          — standard terminal text (TextCell buffer)
```

Each layer N is rendered on top of the cached texture of layer N-1. When only layer N
changes, layers 0..N-1 are already a cached texture — only N and above are re-rendered.

## Scrolling Model

### The Problem with Current Approach

Current canvas.cpp `scrollLines()` iterates ALL remaining lines and ALL their primitives
to decrement `gridOffset.row` — O(lines * prims_per_line) on every scroll event.

### Solution: Scroll Counter

A global `scrollCounter` (uint32) lives in the shared spatial structure. It increments
on every scroll. Primitives store the counter value at insertion time as their
`insertScroll` value.

```
On scroll:
  scrollCounter += numLines
  pop numLines from front of deque
  done — O(1), no iteration

On shader render:
  effectiveRow = prim.insertScroll - scrollCounter + prim.localRow
```

The shader subtracts to get the actual screen position. Zero CPU work on scroll
beyond the deque pop.

The `scrollCounter` is uploaded as part of the per-frame uniform data. Primitives
carry their `insertScroll` as word 0 (replaces the current `gridOffset` packed value).

## Text Grid Memory Layout

The text grid is a **flat contiguous `vector<TextCell>`** sized to `rows * cols`.
This is critical for performance:

- **GPU upload**: `getCellData()` returns a raw pointer — zero-copy to GPU buffer
- **Scrolling**: single `memmove` of `(rows-1) * cols * sizeof(TextCell)` bytes,
  hardware-optimized, cache-friendly
- **Cell access**: `buffer[row * cols + col]` — no indirection

A `vector<vector<TextCell>>` (row-per-vector) would scatter rows across heap,
require `rows` separate memcpy calls to linearize for GPU upload, and destroy
cache locality. The flat buffer is strictly better for the framebuffer path.

```cpp
// Visible grid — flat, contiguous, GPU-ready
vector<TextCell> _primaryBuffer;   // rows * cols
vector<TextCell> _altBuffer;       // alternate screen

// Scroll = memmove + clear last row
void scroll(int lines) {
    size_t lineBytes = cols * sizeof(TextCell);
    memmove(buf.data(), buf.data() + lines * cols, (rows - lines) * lineBytes);
    memset(buf.data() + (rows - lines) * cols, 0, lines * lineBytes);
}
```

### Scrollback

Lines pushed off the top go to a `deque<ScrollbackLine>` for copy-mode and
scroll-back viewing. Scrollback lines are compressed/trimmed (trailing spaces
removed). When scrolled back, a `viewBuffer` is composed from scrollback +
visible lines for GPU upload.

## YDraw Spatial Structure

Separate from the text grid but sharing the same row/col coordinate system
and scroll counter. The ydraw spatial data is a `deque<PrimLine>` where each
line holds primitives and per-cell references.

### Primitive Storage

Primitives are stored at the last (bottom-most) row they overlap. When that row
scrolls off, the primitive is destroyed. Grid cells in rows above hold `PrimRef`
entries pointing down to the storage row via relative offset.

```
struct PrimRef {
  uint16_t rowsAhead;   // relative offset to storage row
  uint16_t primIndex;   // index within storage row's prim list
};

struct PrimLine {
  vector<vector<float>> prims;    // primitives whose base is this line
  vector<vector<PrimRef>> cells;  // per-column prim references
};

deque<PrimLine> _primLines;       // parallel to text grid rows
```

Cards follow the same rule — anchored to their last overlapping row.

### Scroll Synchronization

Both the flat text buffer and the prim deque scroll together, driven by
the same scroll counter. The text buffer uses `memmove`, the prim deque
uses `pop_front`. Both are O(1) per scroll event (no primitive iteration).

## Data Structure Separation

YDraw data structures are independent of rendering mode. The same structures serve:

| Usage             | Grid size      | Scrolls? | Lifecycle              |
|-------------------|---------------|----------|------------------------|
| Scrolling ydraw  | terminal grid | yes      | rows scroll off        |
| Cards             | sub-grid NxM  | yes      | anchored to last row   |
| Static ydraw     | terminal grid | no       | explicit clear/replace |
| YDraw card       | sub-grid NxM  | yes      | anchored to last row   |

### YDrawBuffer (input)

Pure data container for SDF primitives, text spans, images, font blobs.
This is the input format — users build a YDrawBuffer and submit it.
No rendering logic, no GPU awareness.

### SpatialGrid (storage + spatial indexing)

Manages the deque of rows, primitive storage, grid cell references, scroll counter.
Shared between text and ydraw — single scroll mechanism.

Reusable for any grid size (full terminal or card sub-grid).

### Renderer (GPU lifecycle)

Sits on top of SpatialGrid. Handles:
- GPU buffer allocation (declare/allocate/write)
- Packed grid generation for GPU upload
- Metadata management
- Font/glyph processing

Same renderer code works for scrolling, static, and card modes — it just reads
from a SpatialGrid and writes to GPU buffers.

## TextCell

```cpp
struct TextCell {
    uint32_t glyph;                    // UTF codepoint or glyph index
    uint8_t fgR, fgG, fgB, alpha;     // Foreground RGBA
    uint8_t bgR, bgG, bgB, style;     // Background RGB + style byte
};
static_assert(sizeof(TextCell) == 12);
```

The primary buffer is `vector<TextCell>` sized to rows * cols. This is the GPU-friendly
representation — close to what the shader consumes.

## File Decomposition

Avoid a monolithic terminal-screen.cpp. Each concern gets its own file:

```
include/yetty/term/
  text-cell.hpp              — TextCell struct
  spatial-grid.hpp           — SpatialGrid (rows, prims, scroll counter, grid refs)
  terminal-screen.hpp        — TerminalScreen interface (FactoryObject)

src/yetty/term/
  spatial-grid.cpp           — SpatialGrid implementation
  terminal-screen.cpp        — TerminalScreenImpl (orchestrates layers, compositing)

src/yetty/ydraw/
  ydraw-buffer.h            — YDrawBuffer (pure data input, unchanged)
  canvas.cpp                 — replaced by SpatialGrid
  painter.cpp                — rendering logic, uses SpatialGrid
```

## GPU Data Flow

```
TextCell buffer ──────────────────────────> Layer 0 texture
                                                │
SpatialGrid (scrolling ydraw) ──> Renderer ──> Layer 1 on cached Layer 0
                                                │
Cards (sub-grid SpatialGrids) ──> Renderer ──> Layer 2 on cached Layer 1
                                                │
SpatialGrid (static ydraw) ──> Renderer ──> Layer 3 on cached Layer 2
                                                │
                                           Final frame
```

## Layer Rendering Architecture

### WebGPU Objects Per Layer

Each layer render step uses these WebGPU objects:

| Object | Purpose |
|--------|---------|
| `WGPUCommandEncoder` | Records all GPU commands for the frame |
| `WGPURenderPassEncoder` | Records draw commands within a render pass |
| `WGPUTextureView` | Render target (intermediate texture or final surface) |
| `WGPURenderPipeline` | Shader + render state |
| `WGPUBindGroup` | Resources bound to shader (textures, buffers, samplers) |
| `WGPUCommandBuffer` | Finalized commands submitted to queue |

### Render Step Flow

For each layer N:

```cpp
// 1. Create command encoder (once per frame, shared across layers)
WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

// 2. Determine render target
WGPUTextureView targetView;
if (isLastLayer) {
    // Render directly to surface
    WGPUSurfaceTexture surfaceTex;
    wgpuSurfaceGetCurrentTexture(surface, &surfaceTex);
    targetView = wgpuTextureCreateView(surfaceTex.texture, nullptr);
} else {
    // Render to intermediate texture (for compositing)
    targetView = layerTextureView[N];
}

// 3. Begin render pass
WGPURenderPassColorAttachment colorAttachment = {
    .view = targetView,
    .loadOp = (N == 0) ? WGPULoadOp_Clear : WGPULoadOp_Load,
    .storeOp = WGPUStoreOp_Store,
};
WGPURenderPassDescriptor passDesc = {
    .colorAttachmentCount = 1,
    .colorAttachments = &colorAttachment,
};
WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

// 4. Bind resources and draw
wgpuRenderPassEncoderSetPipeline(pass, pipeline);
wgpuRenderPassEncoderSetBindGroup(pass, 0, sharedBindGroup, 0, nullptr);
wgpuRenderPassEncoderSetBindGroup(pass, 1, layerBindGroup, 0, nullptr);
wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, size);
wgpuRenderPassEncoderDraw(pass, vertexCount, instanceCount, 0, 0);

// 5. End render pass
wgpuRenderPassEncoderEnd(pass);

// 6. Submit (once after all layers)
WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
wgpuQueueSubmit(queue, 1, &cmdBuffer);
```

### Layer Compositing

Each layer (except layer 0) samples the previous layer's texture:

```
Layer N render pass:
  Input:  Layer N-1 texture (sampled in fragment shader)
  Output: Layer N texture (or surface if final)

  Fragment shader blends:
    layerN_content + sample(layerN-1_texture)
```

The `loadOp` determines compositing behavior:
- `WGPULoadOp_Clear` — layer 0, start fresh
- `WGPULoadOp_Load` — layers 1+, preserve previous content for blending

### Cache Invalidation

Each layer maintains a dirty flag:
- Layer 0 dirty: terminal content changed (scroll, new text)
- Layer 1 dirty: ydraw primitives changed
- Layer 2 dirty: card content changed
- Layer 3 dirty: static overlay changed

When layer N is dirty, layers N through final must re-render.
When layer N is clean, reuse cached texture — skip render pass entirely.

### Simple Mode (Direct to Surface)

For basic terminal rendering without overlays, skip intermediate textures:

```cpp
// Layer 0 renders directly to surface
WGPUSurfaceTexture surfaceTex;
wgpuSurfaceGetCurrentTexture(surface, &surfaceTex);
targetView = wgpuTextureCreateView(surfaceTex.texture, nullptr);

// Single render pass, no compositing
WGPURenderPassColorAttachment colorAttachment = {
    .view = targetView,
    .loadOp = WGPULoadOp_Clear,
    .storeOp = WGPUStoreOp_Store,
};
```

This avoids texture allocation and compositing overhead when only text grid is needed.

### GpuResourceSet Integration

Resources (font atlases, cell buffers) are packaged as `GpuResourceSet`:

```cpp
struct GpuResourceSet {
    bool shared;                  // bind group 0 or 1
    std::string name;
    WGPUTextureView texture;      // optional
    std::string textureWgslType;
    WGPUSampler sampler;          // optional
    WGPUBuffer buffer;            // optional
    size_t bufferSize;
    std::string bufferWgslType;
    bool bufferReadonly;
};
```

ShaderManager collects GpuResourceSets and:
1. Generates WGSL binding declarations
2. Creates bind group layouts
3. Creates bind groups

Fonts provide CPU atlas data only. The render step creates GPU resources
(texture, sampler, buffer) from CPU data and populates GpuResourceSet.

### Render Target Management

Intermediate textures for layer caching:

```cpp
struct RenderTarget {
    WGPUTexture texture;
    WGPUTextureView view;
    uint32_t width, height;
};

// Create on init or resize
WGPUTextureDescriptor desc = {
    .size = {width, height, 1},
    .format = surfaceFormat,
    .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
};
target.texture = wgpuDeviceCreateTexture(device, &desc);
target.view = wgpuTextureCreateView(target.texture, nullptr);
```

`WGPUTextureUsage_RenderAttachment` — can render to it
`WGPUTextureUsage_TextureBinding` — can sample from it in next layer

## Scroll Counter Detail

```
insertScroll stored per-primitive (word 0):
  bits [15:0]  = insertCol (cursor column at insertion)
  bits [31:16] = insertScrollCounter (scrollCounter value at insertion)

Per-frame uniform:
  scrollCounter (uint32) — current global scroll position

Shader computation:
  screenRow = prim.insertScrollCounter - uniforms.scrollCounter + prim.localRow
  screenCol = prim.insertCol + prim.localCol
```

When `screenRow < 0` the primitive is above the viewport (already scrolled past but
not yet popped from deque). When the storage row scrolls off the deque, the primitive
and all its refs are destroyed.

## Separation of State and Rendering

TerminalScreen manages terminal state only. Rendering is delegated to RenderableLayer subclasses.

### TerminalScreen (State Machine)

Manages:
- vterm state, cells, scrollback, cursor
- YDraw primitives, cards, static overlays

Exposes state via methods:
```cpp
const TextCell* getCellData() const;
uint32_t getCols() const;
uint32_t getRows() const;
int getCursorRow() const;
int getCursorCol() const;
// ... etc for ydraw, cards
```

Does NOT render. Assembles layer chain and triggers render.

### TerminalScreenRenderContext

Passed by const reference, stored by value (copy). Created each frame by TerminalScreen.

```cpp
struct TerminalScreenRenderContext {
    WGPUCommandEncoder encoder;   // created each frame
    WGPUQueue queue;
    RenderTarget target;          // configured at init
    uint32_t width;
    uint32_t height;
};
```

### RenderableLayer (Base Class)

Abstract base for all rendering layers. Handles composition with previous layer.

```cpp
class RenderableLayer {
public:
    RenderableLayer(RenderableLayer* previousLayer, TerminalScreen* terminalScreen);
    virtual ~RenderableLayer() = default;

    virtual void render(const TerminalScreenRenderContext& terminalScreenRenderContext) = 0;
    virtual bool isDirty() const = 0;

protected:
    RenderableLayer* _previousLayer;      // nullptr for layer 0
    TerminalScreen* _terminalScreen;      // for state access
    RenderTarget _cacheTexture;           // optional, for cached mode
};
```

### Layer Subclasses

Each layer knows what state to request from TerminalScreen:

```cpp
class TextGridLayer : public RenderableLayer {
    void render(const TerminalScreenRenderContext& terminalScreenRenderContext) override {
        const TextCell* cells = _terminalScreen->getCellData();
        uint32_t cols = _terminalScreen->getCols();
        uint32_t rows = _terminalScreen->getRows();
        // ... render text grid
    }
};

class ScrollingYDrawLayer : public RenderableLayer {
    void render(const TerminalScreenRenderContext& terminalScreenRenderContext) override {
        if (_previousLayer) {
            _previousLayer->render(terminalScreenRenderContext);
        }
        // ... render ydraw primitives on top
    }
};

class CardsLayer : public RenderableLayer {
    // Internally, each card is its own RenderableLayer
    // Cards are cached individually, re-rendered only when dirty
};

class StaticYDrawLayer : public RenderableLayer {
    // Static overlay, screen-fixed position
};
```

### Layer Assembly

TerminalScreen assembles the layer chain at init:

```cpp
// In TerminalScreen::init()
_textGridLayer = new TextGridLayer(nullptr, this);
_scrollingYDrawLayer = new ScrollingYDrawLayer(_textGridLayer, this);
_cardsLayer = new CardsLayer(_scrollingYDrawLayer, this);
_staticYDrawLayer = new StaticYDrawLayer(_cardsLayer, this);

_topLayer = _staticYDrawLayer;
```

### Render Trigger

TerminalScreen::render() is triggered by event loop (no arguments):

```cpp
void TerminalScreen::render() {
    // Create encoder for this frame
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(_device, &encoderDesc);

    // Build context
    TerminalScreenRenderContext terminalScreenRenderContext;
    terminalScreenRenderContext.encoder = encoder;
    terminalScreenRenderContext.queue = _queue;
    terminalScreenRenderContext.target = _renderTarget;
    terminalScreenRenderContext.width = _width;
    terminalScreenRenderContext.height = _height;

    // Render from top layer (recursively renders layers below)
    _topLayer->render(terminalScreenRenderContext);

    // Finish and submit
    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(_queue, 1, &commandBuffer);
}
```

### Pipeline Configuration

Configurable per-layer: render to cache texture or directly to target.

**Cached mode**: Layer renders to own texture, passes as input to next layer.
**Direct mode**: Layer renders directly to surface, blending with previous content.

Configuration set at init from config, not changed per-frame.

## Implementation Order

1. `TextCell` in `text-cell.hpp` (rename from GridCell)
2. `SpatialGrid` — unified row structure with scroll counter, replaces Canvas
3. `TerminalScreen` interface — layer orchestration
4. Adapt Painter to use SpatialGrid instead of Canvas
5. Card integration with SpatialGrid
6. Layer caching (render-to-texture compositing)
