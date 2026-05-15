# YPaint Design Document

## Overview

YPaint is a 2D vector graphics overlay system using Signed Distance Fields (SDF) for GPU-accelerated rendering. It supports both static overlays and scrolling terminal-style graphics.

## Architecture

### Component Responsibilities

```
┌─────────────────────────────────────────────────────────────────┐
│                         Painter                                  │
│  - Owns Canvas (shared_ptr)                                     │
│  - Font management (MsdfAtlas, FontManager)                     │
│  - AABB computation for primitives                              │
│  - GPU buffer lifecycle (declare/allocate/write)                │
│  - Text measurement and glyph rendering                         │
│  - Exposes GPU offsets: gpuPrimitiveOffset, gpuGridOffset,      │
│    gpuGlyphOffset for shader binding                            │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ owns
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                         Canvas                                   │
│  - Spatial grid representation (deque of GridLines)             │
│  - Primitive storage with pre-computed AABBs                    │
│  - Grid cell management (PrimRef for cross-line references)     │
│  - Scrolling support (line removal, cursor tracking)            │
│  - Packed GPU format generation (gridStaging, primStaging)      │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ registers with
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    GpuMemoryManager                              │
│  - Coordinates buffer allocation via GpuBufferManager           │
│  - Manages texture resources via GpuTextureManager              │
│  - Same infrastructure used by cards                            │
│  - Painters are registered like cards for buffer management     │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ manages
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      GPU Buffers                                 │
│  - cardStorage buffer: shared by cards and painters             │
│  - Primitive data, Grid lookup, Glyph data                      │
│  - Same bind group structure as cards                           │
└─────────────────────────────────────────────────────────────────┘
```

### Canvas Internal Structure

```cpp
// Reference to a primitive in another line
struct PrimRef {
  uint16_t linesAhead; // relative offset to base line (0 = same line)
  uint16_t primIndex;  // index within base line's prims vector
};

// A single grid cell - contains references to overlapping primitives
struct GridCell {
  std::vector<PrimRef> refs;
};

// A single row/line in the grid
struct GridLine {
  // Primitives whose BASE (bottom-most line they touch) is this line
  std::vector<std::vector<float>> prims;
  // Grid cells for this line's Y range (indexed by X column)
  std::vector<GridCell> cells;
};

// Lines stored in deque for efficient scrolling
std::deque<GridLine> _lines;
```

### Data Flow: Demo Script to Shader

```
circles.sh
    │
    ├── YAML content base64-encoded
    │
    └── OSC command: \033]666674;--yaml;{base64}\033\\  (scrolling layer)
           │         \033]666675;--yaml;{base64}\033\\  (overlay layer)
           │
           ▼
    Terminal OSC Parser (handleYpaintOSC)
           │
           ▼
    Painter.addYpaintBuffer()
           │
           ├── Compute AABBs for each primitive
           ├── Call Canvas.addPrimitive(data, aabb)
           └── Canvas positions primitive in grid
           │
           ▼
    GpuMemoryManager (same as cards)
           │
           ├── Painter.declareBufferNeeds()
           ├── GpuBufferManager allocates in cardStorage
           └── Painter.writeBuffers() to allocated regions
           │
           ▼
    GPU Upload to cardStorage buffer
           │
           ├── Primitive data at painter->gpuPrimitiveOffset()
           ├── Grid data at painter->gpuGridOffset()
           └── Glyph data at painter->gpuGlyphOffset()
           │
           ▼
    Shader: 0x0004-ydraw.wgsl
           │
           ├── Read from cardStorage using painter offsets
           ├── Lookup cell from pixel position
           ├── Iterate primitives in cell
           ├── Call evaluateSDF(primOffset, p)
           └── Blend colors based on distance
```

## GPUScreen Integration

### GpuMemoryManager Architecture

Both painters use GpuMemoryManager for buffer management, exactly like cards do:

```
┌─────────────────────────────────────────────────────────────────┐
│                        GPUScreen                                 │
│                                                                  │
│  ┌──────────────────┐    ┌──────────────────┐                   │
│  │ Scrolling Painter│    │  Overlay Painter │                   │
│  │   (OSC 666674)   │    │   (OSC 666675)   │                   │
│  └────────┬─────────┘    └────────┬─────────┘                   │
│           │                       │                              │
│           │ registered with       │ registered with              │
│           ▼                       ▼                              │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │              GpuMemoryManager (shared)                   │    │
│  │  - Same instance used by cards                          │    │
│  │  - GpuBufferManager: cardStorage buffer allocation      │    │
│  │  - GpuTextureManager: font atlas, etc.                  │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

Each painter gets its own slot in the GpuMemoryManager, with separate buffer
regions for primitives, grid, and glyphs. The shader accesses painter data
using the same cardStorage buffer and bind group as cards.

### Render Layer Structure

GPUScreen has THREE render layers:

```
┌─────────────────────────────────────────────────────────────────┐
│  Layer 2: Overlay Painter (OSC 666675)                          │
│  - Fixed position overlays (HUD, tooltips, status)              │
│  - Does NOT scroll with terminal content                        │
│  - Rendered LAST (on top of everything)                         │
│  - Absolute screen coordinates                                  │
└─────────────────────────────────────────────────────────────────┘
                              ↑
┌─────────────────────────────────────────────────────────────────┐
│  Layer 1: Scrolling Painter (OSC 666674)                        │
│  - Graphics that scroll WITH terminal content                   │
│  - Cursor-relative positioning (like terminal text)             │
│  - SYNCHRONIZED with vterm scrolling                            │
└─────────────────────────────────────────────────────────────────┘
                              ↑
┌─────────────────────────────────────────────────────────────────┐
│  Layer 0: Terminal Glyph Grid                                    │
│  - Standard terminal text rendering                             │
│  - Character cells with attributes                              │
│  - Rendered FIRST (bottom layer)                                │
└─────────────────────────────────────────────────────────────────┘
```

### OSC Command Codes

Each layer has its own OSC code:

| Layer | OSC Code | Purpose |
|-------|----------|---------|
| Scrolling | `666674` | Graphics that scroll with terminal content |
| Overlay | `666675` | Fixed position overlays (HUD, tooltips) |

Note: 666667-669 are used for shader effects, 666670 for GPU stats,
666671 for FPS control, 666673 for ScreenDrawLayer (deprecated).

```bash
# Draw to scrolling layer (always appends - content accumulates and scrolls off)
printf '\033]666674;--yaml;%s\033\\' "$base64_data"

# Draw to overlay layer (replaces previous content by default)
printf '\033]666675;--yaml;%s\033\\' "$base64_data"

# Draw to overlay layer (appends to existing content)
printf '\033]666675;--append;--yaml;%s\033\\' "$base64_data"

# Clear scrolling layer
printf '\033]666674;--clear\033\\'

# Clear overlay layer
printf '\033]666675;--clear\033\\'
```

**Note:** Scrolling mode is ALWAYS append - content accumulates like terminal text
and only disappears when it scrolls off the top. Overlay mode defaults to replace
but supports `--append` to accumulate content.

### Why GpuMemoryManager?

Using the same GpuMemoryManager for both cards and painters provides:

1. **No code duplication**: Buffer allocation, bind group creation, and GPU
   upload logic is shared with cards
2. **Single cardStorage buffer**: Painters and cards share the same GPU buffer,
   reducing memory overhead and simplifying binding
3. **Consistent architecture**: Painters are just another type of GPU resource,
   managed the same way as cards
4. **Simpler rendering**: Same shader infrastructure, same bind groups, just
   different offsets into cardStorage

### CRITICAL: Bidirectional Scrolling Synchronization

The scrolling painter and vterm must stay **perfectly synchronized**. This is
bidirectional - either side can be the source of truth depending on the event:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    BIDIRECTIONAL SYNC MODEL                             │
│                                                                         │
│   ┌─────────────┐                           ┌─────────────┐             │
│   │   Canvas    │ ←───── sync ─────────────→│   VTerm     │             │
│   │  (Painter)  │                           │             │             │
│   └─────────────┘                           └─────────────┘             │
│         │                                         │                     │
│         │ Graphics added (OSC 666674)             │ Escape sequences    │
│         │ Canvas is SOURCE OF TRUTH               │ VTerm is SOURCE     │
│         │ VTerm syncs TO canvas                   │ Canvas syncs TO it  │
│         ▼                                         ▼                     │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 1. Adding Graphics: Canvas is Source of Truth

When `addYpaintBuffer()` is called, the **Canvas scrolls first** (if needed),
then returns the number of lines consumed. GPUScreen uses this result to
scroll vterm to match:

```
┌──────────────────────────────────────────────────────────────────┐
│  YPaintBuffer arrives via OSC 666674                             │
│                     │                                            │
│                     ▼                                            │
│  GPUScreen calls: scrollingPainter->addYpaintBuffer(buffer)      │
│                     │                                            │
│                     ▼                                            │
│  Canvas processes buffer:                                        │
│    - Computes AABB, determines lines needed                      │
│    - If doesn't fit, Canvas SCROLLS INTERNALLY                   │
│    - Stores primitives at cursor position                        │
│    - Returns: uint32_t linesConsumed                             │
│                     │                                            │
│  *** Canvas is SOURCE OF TRUTH at this moment ***                │
│                     │                                            │
│                     ▼                                            │
│  GPUScreen scrolls vterm to MATCH canvas state:                  │
│    - Inject N newlines, OR                                       │
│    - Use vterm API to force scroll                               │
│                     │                                            │
│  VTerm now matches Canvas                                        │
└──────────────────────────────────────────────────────────────────┘
```

**Why?** The graphic occupies terminal lines. VTerm must know about this
space so subsequent text output doesn't OVERLAP the graphic.

```cpp
void GPUScreenImpl::handleScrollingPaint(YPaintBuffer::Ptr buffer) {
    // 1. Add to painter - Canvas is SOURCE OF TRUTH
    //    Canvas may scroll internally, returns lines consumed
    auto result = _scrollingPainter->addYpaintBuffer(buffer);
    if (!result) return;

    uint32_t linesConsumed = *result;

    // 2. Scroll vterm to MATCH canvas state
    //    Option A: Inject newlines
    for (uint32_t i = 0; i < linesConsumed; i++) {
        vterm_keyboard_unichar(_vterm, '\n', VTERM_MOD_NONE);
    }

    //    Option B: Force scroll via vterm API (preferred if available)
    // vterm_screen_scroll(_vtermScreen, linesConsumed);
}
```

#### 2. Escape Sequences: VTerm is Source of Truth

When escape sequences cause vterm to scroll (cursor movement, newlines, etc.),
vterm is the source of truth and the Canvas must sync TO it:

```
┌──────────────────────────────────────────────────────────────────┐
│  VTerm receives escape sequence that causes scrolling            │
│  (e.g., cursor down past bottom, newline at bottom row)          │
│                     │                                            │
│  *** VTerm is SOURCE OF TRUTH ***                                │
│                     │                                            │
│                     ▼                                            │
│  VTerm callback notifies GPUScreen of scroll event               │
│                     │                                            │
│                     ▼                                            │
│  GPUScreen calls: scrollingPainter->scrollLines(numLines)        │
│                     │                                            │
│  Canvas scrolls to MATCH vterm state                             │
│  Graphics scroll UP in sync with terminal text                   │
└──────────────────────────────────────────────────────────────────┘
```

```cpp
// VTerm scroll callback - vterm is source of truth here
int GPUScreenImpl::onVTermScroll(int numLines) {
    // Scroll the painter to MATCH vterm
    if (_scrollingPainter && numLines > 0) {
        _scrollingPainter->scrollLines(static_cast<uint16_t>(numLines));
    }
    return 0;  // allow scroll
}
```

#### 3. Overflow Handling

If the new YPaintBuffer doesn't fit at the current cursor position, the
Canvas handles the scroll internally and returns the total lines consumed.
VTerm then syncs to match:

```
┌──────────────────────────────────────────────────────────────────┐
│  Before: Cursor at row 23 (near bottom), graphic needs 10 lines  │
│                                                                  │
│  Terminal viewport (25 lines):                                   │
│  ┌────────────────────────┐                                      │
│  │ line 0: text           │                                      │
│  │ line 1: text           │                                      │
│  │ ...                    │                                      │
│  │ line 22: text          │                                      │
│  │ line 23: CURSOR ←      │  Only 2 lines available!             │
│  │ line 24: text          │                                      │
│  └────────────────────────┘                                      │
│                                                                  │
│  Canvas.addYpaintBuffer():                                       │
│    - Needs 10 lines, only 2 available                            │
│    - Canvas scrolls 8 lines internally                           │
│    - Stores graphic at new position                              │
│    - Returns 10 (total lines consumed)                           │
│                                                                  │
│  GPUScreen scrolls vterm by 10 lines to match                    │
│  (Some text and top of graphic may scroll off)                   │
└──────────────────────────────────────────────────────────────────┘
```

```cpp
void GPUScreenImpl::handleScrollingPaint(YPaintBuffer::Ptr buffer) {
    // Canvas handles everything - scrolling if needed, positioning
    // Returns total lines consumed (including any internal scrolling)
    auto result = _scrollingPainter->addYpaintBuffer(buffer);
    if (!result) return;

    uint32_t linesConsumed = *result;

    // Scroll vterm to match canvas state
    scrollVTerm(linesConsumed);
}

### GPUScreenImpl Changes

Two `Painter` instances are registered with the shared `GpuMemoryManager`:

```cpp
class GPUScreenImpl : public GPUScreen {
    // ... existing members ...

    GpuMemoryManager::Ptr _cardManager;  // Shared GPU resource manager
    Painter::Ptr _scrollingPainter;      // Layer 1: synced with vterm
    Painter::Ptr _overlayPainter;        // Layer 2: fixed position
};

void GPUScreenImpl::initPainters() {
    // Both painters register with the same GpuMemoryManager
    // They get their own slots/buffers, just like cards do
    _scrollingPainter = Painter::create(_cardManager, ...);
    _overlayPainter = Painter::create(_cardManager, ...);
}
```

### Buffer Management

Painters follow the same lifecycle as cards:

```cpp
// 1. Declare buffer needs (called when content changes)
painter->declareBufferNeeds();

// 2. GpuBufferManager allocates space in cardStorage
//    (handled automatically by GpuMemoryManager)

// 3. Write buffer data to allocated regions
painter->writeBuffers();

// 4. Shader reads from cardStorage using painter's offsets:
//    - painter->gpuPrimitiveOffset()
//    - painter->gpuGridOffset()
//    - painter->gpuGlyphOffset()
```

### Render Order

Painters are rendered using the same shader and bind groups as cards:

```cpp
void GPUScreenImpl::render(WGPURenderPassEncoder pass) {
    // All rendering uses the shared cardStorage buffer
    // Painters and cards share the same bind group structure

    // 1. Render terminal text (layer 0)
    renderGlyphGrid(pass);

    // 2. Render scrolling graphics (layer 1)
    //    Uses cardStorage[scrollingPainter->gpuPrimitiveOffset()...]
    if (_scrollingPainter && _scrollingPainter->hasContent()) {
        renderYpaintLayer(pass, _scrollingPainter);
    }

    // 3. Render overlay graphics (layer 2)
    //    Uses cardStorage[overlayPainter->gpuPrimitiveOffset()...]
    if (_overlayPainter && _overlayPainter->hasContent()) {
        renderYpaintLayer(pass, _overlayPainter);
    }
}

## Primitive Buffer Layout

### GPU Layout (per primitive)

```
Word 0: gridOffset (u16 x | u16 y << 16) - grid cell offset for scrolling
Word 1: type (u32) - primitive type ID
Word 2: layer (u32) - render layer
Word 3+: geometry parameters (type-specific)
Word N-4: fillColor (u32)
Word N-3: strokeColor (u32)
Word N-2: strokeWidth (f32)
Word N-1: round (f32)
```

### Primitive Size Lookup Map

For O(1) jumping to the next primitive during iteration:

| Type | ID | Size (words) | Notes |
|------|-----|--------------|-------|
| Circle | 1 | 9 | Fixed |
| Box | 2 | 10 | Fixed |
| RoundedBox | 3 | 11 | Fixed |
| OrientedBox | 4 | 13 | Fixed |
| Segment | 5 | 11 | Fixed |
| Rhombus | 6 | 11 | Fixed |
| Trapezoid | 7 | 12 | Fixed |
| Parallelogram | 8 | 12 | Fixed |
| EquilateralTriangle | 9 | 10 | Fixed |
| IsoscelesTriangle | 10 | 11 | Fixed |
| Triangle | 11 | 13 | Fixed |
| UnevenCapsule | 12 | 12 | Fixed |
| Pentagon | 13 | 10 | Fixed |
| Hexagon | 14 | 11 | Fixed |
| Octagon | 15 | 10 | Fixed |
| Hexagram | 16 | 10 | Fixed |
| Star5 | 17 | 11 | Fixed |
| Star | 18 | 12 | Fixed |
| Pie | 19 | 12 | Fixed |
| CutDisk | 20 | 11 | Fixed |
| Arc | 21 | 13 | Fixed |
| Ring | 22 | 12 | Fixed |
| Horseshoe | 23 | 14 | Fixed |
| Vesica | 24 | 11 | Fixed |
| Moon | 25 | 12 | Fixed |
| CircleCross | 26 | 10 | Fixed |
| SimpleEgg | 27 | 11 | Fixed |
| Heart | 28 | 10 | Fixed |
| Cross | 29 | 12 | Fixed |
| RoundedX | 30 | 11 | Fixed |
| Ellipse | 31 | 11 | Fixed |
| Parabola | 32 | 10 | Fixed |
| ParabolaSegment | 33 | 11 | Fixed |
| Bezier | 34 | 15 | Fixed |
| BlobCross | 35 | 10 | Fixed |
| Tunnel | 36 | 11 | Fixed |
| Stairs | 37 | 12 | Fixed |
| QuadraticCircle | 38 | 10 | Fixed |
| Hyperbola | 39 | 12 | Fixed |
| CoolS | 40 | 10 | Fixed |
| CircleWave | 41 | 12 | Fixed |
| Plot | 128 | 7 + N*2 | Variable: N = numPoints |
| Polygon | 130 | 5 + N*2 | Variable: N = numVertices |
| PolygonGroup | 131 | varies | Variable: depends on groups |

## Scrolling Mode

### Concept

In scrolling mode, primitives are positioned relative to a cursor (like terminal text).
Each primitive has TWO important row values:

1. **gridOffset** (word 0): The cursor row where drawing STARTS - used by shader for
   coordinate transformation
2. **Storage row** (primMaxRow): The bottom row of the primitive's AABB - where the
   primitive is stored in the deque (determines when it gets deleted on scroll)

### Example: Adding a 5-Line Shape at Cursor (5, 10)

```
Primitive local AABB: rows 0-4 (5 lines tall), cols 0-3 (4 cells wide)
Cursor position: (col=5, row=10)

Grid refs placed at: rows 10-14, cols based on AABB X
Primitive STORED at: row 14 (primMaxRow = cursorRow + localMaxRow)
gridOffset value: (5, 10) - cursor position where drawing STARTS
```

When shader queries at (scenePos.x=100, scenePos.y=200), cellSize=(20,20):
- pAdj = (100, 200) - (5*20, 10*20) = (0, 0) (local origin)

When shader queries at (scenePos.x=160, scenePos.y=280):
- pAdj = (160, 280) - (100, 200) = (60, 80) (local bottom-right area)

### Grid Offset Usage in Shader

```wgsl
// Read grid offset from primitive (word 0)
let gridOffsetPacked = bitcast<u32>(storage[primOffset + 0u]);
let gridOffsetX = f32(gridOffsetPacked & 0xFFFFu);
let gridOffsetY = f32(gridOffsetPacked >> 16u);

// Convert grid offset to pixel offset
let pixelOffset = vec2<f32>(gridOffsetX * cellSizeX, gridOffsetY * cellSizeY);

// Adjust p for primitive's position (transform to local coords)
let pAdj = p - pixelOffset;

// Now evaluate SDF with adjusted p (in primitive's local space)
return sdCircle(pAdj, center, radius);
```

### Scrolling Operation

When scrolling N lines:
1. Pop N lines from front of deque (primitives stored there are deleted)
2. Decrement gridOffset.y by N for all remaining primitives
3. Grid refs automatically adjust (deque indices shift)
4. gridOffset should NEVER go negative (primitives would be deleted before that)

```cpp
void scrollLines(uint16_t numLines) {
    // Pop lines - primitives stored there are auto-deleted
    for (uint16_t i = 0; i < numLines && !_lines.empty(); i++) {
        _lines.pop_front();
    }

    // Update gridOffset in remaining primitives
    for (auto &line : _lines) {
        for (auto &prim : line.prims) {
            uint32_t packed;
            std::memcpy(&packed, &prim[0], sizeof(uint32_t));
            uint16_t col = packed & 0xFFFF;
            uint16_t row = (packed >> 16) & 0xFFFF;
            row -= numLines;  // Decrement by scroll amount
            packed = col | (static_cast<uint32_t>(row) << 16);
            std::memcpy(&prim[0], &packed, sizeof(uint32_t));
        }
    }
}
```

## Grid Structure

### Cell Layout

```
Grid buffer layout:
[offset_0][offset_1]...[offset_N-1][packed_cells]

Where offset_i points to the packed data for cell i.

Packed cell data:
[count][entry_0][entry_1]...[entry_count-1]

Entry format:
- If high bit (0x80000000) is set: glyph index (entry & 0x7FFFFFFF)
- Otherwise: primitive word offset in primitive buffer
```

### Grid Lookup in Shader

```wgsl
fn getCellPrimitives(cellX: u32, cellY: u32) -> array<u32, MAX_PRIMS> {
    let cellIndex = cellY * gridWidth + cellX;
    let packedOffset = gridData[cellIndex];
    let count = gridData[packedOffset];

    var prims: array<u32, MAX_PRIMS>;
    for (var i = 0u; i < count && i < MAX_PRIMS; i++) {
        prims[i] = gridData[packedOffset + 1 + i];
    }
    return prims;
}
```

## SDF Functions

### First Argument `p`

All SDF functions take `p` as the first argument - the pixel position in scene coordinates:

```wgsl
fn sdCircle(p: vec2<f32>, center: vec2<f32>, radius: f32) -> f32 {
    return length(p - center) - radius;
}
```

### Position Adjustment for Scrolling

The gridOffset transforms scene coordinates to the primitive's local coordinate system.
gridOffset = cursorRow when primitive was added (where drawing STARTS).

```wgsl
// pixelOffset = gridOffset * cellSize
// pAdj = scenePos - pixelOffset
//
// Example: primitive added at cursor row 10, cellSize.y = 20
// - At scenePos.y = 200 (row 10): pAdj.y = 200 - 200 = 0 (local top)
// - At scenePos.y = 280 (row 14): pAdj.y = 280 - 200 = 80 (local bottom)
//
// The primitive's AABB in local coords (0 to 80) maps correctly to screen rows 10-14

let pAdj = p - pixelOffset;
let d = sdCircle(pAdj, center, radius);
```

## Metadata Layout (64 bytes)

```cpp
struct YPaintMetadata {
    uint32_t primitiveOffset;  // 0: offset to primitive buffer
    uint32_t primitiveCount;   // 4: number of primitives
    uint32_t gridOffset;       // 8: offset to grid buffer
    uint32_t gridWidth;        // 12: grid width in cells
    uint32_t gridHeight;       // 16: grid height in cells
    uint32_t cellSizeXY;       // 20: cellSizeX (f16) | cellSizeY (f16)
    uint32_t glyphOffset;      // 24: offset to glyph buffer
    uint32_t glyphCount;       // 28: number of glyphs
    uint32_t sceneMinX;        // 32: scene bounds (f32 as bits)
    uint32_t sceneMinY;        // 36
    uint32_t sceneMaxX;        // 40
    uint32_t sceneMaxY;        // 44
    uint32_t widthCells;       // 48: viewport width | panX (i16)
    uint32_t heightCells;      // 52: viewport height | panY (i16)
    uint32_t flags;            // 56: flags (u16) | zoom (f16)
    uint32_t bgColor;          // 60: background color (RGBA)
};
```

## Implementation Status

### Completed

- [x] Canvas class implementation (grid management, scrolling)
- [x] Painter refactoring to use Canvas
- [x] Primitive storage with gridOffset prepended
- [x] Grid staging generation
- [x] Primitive staging generation
- [x] Scene bounds-based grid dimensions
- [x] All 155 unit tests passing
- [x] GpuMemoryManager (renamed from CardManager) for unified GPU resource management
- [x] OSC 666674/666675 protocol for scrolling/overlay layers
- [x] Painter GPU offset getters (gpuPrimitiveOffset, gpuGridOffset, gpuGlyphOffset)

### In Progress

- [ ] Register painters with GpuMemoryManager (same as cards)
- [ ] Render painters using shared cardStorage buffer

### TODO

- [ ] Fix gen-ydraw-types.py to read ydraw-primitives.yaml
- [ ] Generate evaluateSDF from YAML
- [ ] Generate primitive size lookup map
- [ ] Delete hand-written evaluateSDF from distfunctions.wgsl
- [ ] Debug why demos don't work (trace OSC path)
- [ ] Unify or clearly separate ydraw/ydraw primitive IDs

## Known Issues

### 1. Broken Code Generator

`gen-ydraw-types.py` is a non-functional copy of `gen-ydraw-types.py`:
- Line 26 reads `ydraw-primitives.yaml` which doesn't exist in ydraw directory
- No generated files for ydraw primitives

### 2. Hand-Written evaluateSDF Misalignment

`distfunctions.wgsl` contains **hand-written** `evaluateSDF()` (line 886+) with hardcoded offsets:
```wgsl
fn evaluateSDF(primOffset: u32, p: vec2<f32>) -> f32 {
    let primType = bitcast<u32>(cardStorage[primOffset + 0u]);
    switch (primType) {
        case SDF_CIRCLE: {
            let center = vec2<f32>(cardStorage[primOffset + 2u], cardStorage[primOffset + 3u]);
            let radius = cardStorage[primOffset + 4u];
            return sdCircle(p, center, radius);
        }
        // ... more hardcoded offsets
    }
}
```

Meanwhile, `gen-ydraw-types.py` generates `evalSDF()` (different function!) with gridOffset support.

**Result:** Adding/changing fields in YAML causes buffer misalignment since `evaluateSDF` isn't updated.

### 3. Primitive ID Conflicts

| Primitive | ydraw ID | ydraw ID |
|-----------|----------|-----------|
| Circle    | 0        | 1         |
| Box       | 1        | 2         |
| Segment   | 2        | 5         |

Two incompatible ID schemes cause confusion.

## Discovered Problems

### Painter Buffer Issues

#### 1. Arbitrary Cell Size Auto-Computation

In `painter-buffer.inc` lines 103-106, the cell size is auto-computed with a **hardcoded magic number**:

```cpp
// Default to ~10x10 grid cells
_cellSizeX = std::max(1.0f, sceneW / 10.0f);
_cellSizeY = std::max(1.0f, sceneH / 10.0f);
```

This divides the scene by 10 to create "about 10 grid cells". This is completely arbitrary and
**has nothing to do with terminal cell size**. In scrolling mode, this creates a fundamental
coordinate system mismatch.

#### 2. Coordinate System Mismatch in Scrolling Mode

In `canvas.cpp` `addPrimitive()`, two incompatible coordinate systems are mixed:

```cpp
uint16_t localMinRow = static_cast<uint16_t>(
    std::max(0, static_cast<int32_t>(std::floor((aabbMinY - baseY) / _cellSizeY))));
uint16_t localMaxRow = ...;

// In scrolling mode, grid rows are offset by cursor position
uint16_t primMinRow = _scrollingMode ? (_cursorRow + localMinRow) : localMinRow;
uint16_t primMaxRow = _scrollingMode ? (_cursorRow + localMaxRow) : localMaxRow;
```

**Problem:** `_cursorRow` is in **terminal cell units** (e.g., row 3 = terminal line 3), while
`localMinRow/MaxRow` are computed using **ydraw cell size** (e.g., sceneHeight/10). These are
completely different coordinate systems being added together.

**Example with shapes.sh:**
- Scene bounds: `[0,0]-[1600,807]`
- YPaint cell size: 807/10 = ~80px (arbitrary!)
- Terminal cursor at row 3
- Circle at Y=90 with radius 40: AABB Y = 50-130

**Canvas stores** the circle at:
- localMinRow = floor((50-0)/80) = 0
- localMaxRow = floor((130-0)/80) = 1
- primMinRow = 3 + 0 = **3**
- primMaxRow = 3 + 1 = **4**

**Shader queries** at scene Y=90:
- cellY = floor((90-0)/80) = **1**

The shader looks up grid cell at row **1**, but the primitive is stored at rows **3-4**.
**They never find each other.** This is why only one shape (or none) displays.

#### 3. Required Fix

In scrolling mode, the ydraw cell size MUST equal the terminal cell size so that:
- `_cursorRow` (terminal rows) and `localMinRow` (ydraw grid rows) are in the same units
- Grid lookup in shader matches where primitives are stored in canvas

Alternatively, don't add `_cursorRow` to grid indices at all - let the gridOffset in the
primitive handle the terminal position translation in the shader.
