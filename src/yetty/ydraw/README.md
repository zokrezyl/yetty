# ydraw

Pure C implementation of ydraw for FFI support with other programming languages.

## Example: Circle Rendering with Scrolling

**INPUT (from user via OSC command):**
- cursor is at visible row 5
- cy = 30 (relative to cursor row top)
- radius = 25
- cell_height = 20
- row0_absolute = 0 (no scrolling yet)

---

**CPU (C) - Primitive Creation:**

**STEP 1: Compute AABB in relative coords (as if cursor at row 0)**
```
aabb_min_y_rel = cy - radius = 30 - 25 = 5
aabb_max_y_rel = cy + radius = 30 + 25 = 55

aabb_min_row_rel = floor(5 / 20) = 0
aabb_max_row_rel = floor(55 / 20) = 2
```

**STEP 2: Convert to actual rows**
```
aabb_min_row = cursor_row + aabb_min_row_rel = 5 + 0 = 5
aabb_max_row = cursor_row + aabb_max_row_rel = 5 + 2 = 7

storage_row = aabb_max_row = 7  (lowest row, for deletion tracking)
```

**STEP 3: Get rolling_row at insertion**
```
rolling_row = row0_absolute + cursor_row = 0 + 5 = 5
```

**STEP 4: Register primitive in spatial grid**
```
// Primitive DATA stored in storage_row=7's line (for deletion tracking)
// Grid cells in rows 5, 6, 7 get drawable_ref pointing to storage_row=7
grid[row=5].add(drawable_ref{lines_ahead=2, drawable_index=0})
grid[row=6].add(drawable_ref{lines_ahead=1, drawable_index=0})
grid[row=7].add(drawable_ref{lines_ahead=0, drawable_index=0})
```

**STEP 5: Serialize to GPU buffer**
```
[rolling_row=5][type][z][fill][stroke][stroke_w][cx][cy=30][radius=25]
```

**IMPORTANT: Geometry coordinates (cy=30) are stored exactly as user specified. NO transformation. The shader adjusts the TEST POSITION, not the primitive coordinates.**

---

**GPU (WGSL shader) - Fragment shader per pixel:**

**STEP 6: Render pixel at screen Y=130 (circle center)**
```wgsl
pixel_pos_y = 130              // screen pixel Y coordinate
rolling_row = 5                // from buffer[drawable_offset + 0]
row_origin = 0                 // uniform (rolling_row_0)

y_offset = (rolling_row - row_origin) * cell_height = (5 - 0) * 20 = 100
drawable_scene_pos_y = pixel_pos_y - y_offset = 130 - 100 = 30

cy = 30                        // from primitive buffer (stored as-is)
distance = |drawable_scene_pos_y - cy| - radius = |30 - 30| - 25 = -25 < 0 → INSIDE
```

**STEP 7: Render pixel at screen Y=110 (inside AABB, row 5 area)**
```wgsl
pixel_pos_y = 110
y_offset = 100                 // same primitive, same y_offset
drawable_scene_pos_y = 110 - 100 = 10

distance = |10 - 30| - 25 = 20 - 25 = -5 < 0 → INSIDE
```

---

## Key Concept: No Coordinate Transformation

Geometry coordinates are stored **exactly as user specified**. The shader handles scrolling by adjusting the **test position**, not the primitive coordinates:

```wgsl
y_offset = (rolling_row - row_origin) * cell_height
drawable_scene_pos_y = pixel_pos_y - y_offset
sd_circle(drawable_scene_pos, vec2(cx, cy), radius)  // cy used as-is
```

This means:
- CPU does NOT transform cy
- Shader computes y_offset from rolling_row (at insertion)
- Shader adjusts test position to match primitive's coordinate system
- SDF evaluates distance from adjusted test position to stored coordinates

---

## Rolling Offset Optimization

In scrolling mode (terminal), lines scroll off the top. Naive approach updates every primitive's row on scroll - O(n) where n is primitive count.

### The Optimization

1. **Rolling row counter** - A `uint32_t` that only increments, never decrements. Starts at 0, increases by 1 for each new line since terminal start.

2. **Primitive stores rolling_row at creation** - First word of primitive data:
   ```
   [0] rolling_row
   ```
   Where `rolling_row` = row0_absolute + cursor_row (rolling row at insertion).

3. **Geometry stored as-is** - No coordinate transformation. cy, etc. stored exactly as user specified.

4. **Row origin uniform** - Single `uint32_t` passed to GPU: rolling row of top visible line.

5. **GPU adjusts test position:**
   ```wgsl
   let rolling_row = storage_buffer[drawable_offset + 0u];
   let y_offset = f32(i32(rolling_row) - i32(row_origin)) * cell_height;
   let drawable_scene_pos_y = pixel_pos_y - y_offset;
   // SDF evaluates: sd_xxx(drawable_scene_pos, stored_coords)
   ```

### Scroll Complexity

- **O(1) scroll** - Just increment `row_origin` uniform by number of scrolled lines
- **Zero primitive updates** - All primitives keep their original rolling_row and coordinates
- Pop lines from front of line storage (for memory, scroll-back TBD)

### Uniform

| Name | Type | Description |
|------|------|-------------|
| `row_origin` | `u32` | Rolling row of top visible line |

---

## Buffer Layout

Each primitive is serialized to a GPU buffer as consecutive 32-bit words:

```
[rolling_row][type][z_order][fill_color][stroke_color][stroke_width][geometry...]
```

Where:
- `rolling_row` = rolling row at insertion (for y_offset calculation)
- `type` = primitive type for shader dispatch
- `z_order` = rendering order
- `fill_color` = packed RGBA
- `stroke_color` = packed RGBA
- `stroke_width` = f32
- `geometry` = primitive-specific SDF parameters (stored as-is, no transformation)

---

## Shader Access

Buffer is `array<u32>` in WGSL. Shader reads at fixed offsets:

```
[0] rolling_row   - u32, rolling row at insertion
[1] type          - u32, primitive type for dispatch
[2] z_order       - u32, rendering order
[3] fill_color    - u32, packed RGBA
[4] stroke_color  - u32, packed RGBA
[5] stroke_width  - f32 (bitcast)
[6+] geometry     - primitive-specific args (stored as-is)
```

Shader:
1. Reads `rolling_row` from offset 0
2. Computes `y_offset = (rolling_row - row_origin) * cell_height` (signed arithmetic)
3. Adjusts **test position**: `drawable_scene_pos_y = pixel_pos_y - y_offset`
4. Dispatches based on type
5. Extracts geometry at known offsets (used as-is, no transformation)
6. Calls `sd_xxx(drawable_scene_pos, ...)` - SDF evaluates adjusted test position against stored coords

---

## Canvas

The canvas manages a spatial grid for GPU culling:

- Lines stored in deque (front = top, back = bottom)
- Each line contains primitives whose base (bottom edge) is on that line
- Supports scrolling mode (cursor-relative) and static mode (absolute coords)

### Spatial Grid

Each grid cell stores an array of `drawable_ref`:

```c
struct yetty_ydraw_scrolling_canvas_drawable_ref {
    uint16_t lines_ahead;  // relative offset to base line (0 = same line)
    uint16_t drawable_index;   // index within base line's prims array
};
```

- `lines_ahead`: how many lines below to find the base line (where primitive data lives)
- `drawable_index`: index into that base line's `prims` array

Primitives are stored in their **base line** (storage_row = lowest row of AABB). Grid cells in rows above reference down to the base line.

When a line scrolls off, its primitives are deleted and all refs pointing to it become invalid.

### Rolling Row Tracking

Canvas tracks:
- `next_rolling_row` - next rolling row to assign to new lines (increments)
- `row0_absolute` - rolling row of top visible line (for uniform)

On scroll:
1. Pop N lines from front
2. `row0_absolute += N`
3. Done

---

## Common Structs

### struct yetty_ydraw_type

Primitive type identifier for shader dispatch:

```c
struct yetty_ydraw_type {
    uint32_t type;
};
```

### struct yetty_ydraw_attrs

Rendering attributes (not used by SDF functions):

```c
struct yetty_ydraw_attrs {
    uint32_t z_order;
};
```

### struct yetty_ydraw_style

Common rendering parameters for all primitives (used after SDF evaluation):

```c
struct yetty_ydraw_style {
    uint32_t fill_color;
    uint32_t stroke_color;
    float stroke_width;
};
```

---

## Geometry Structs

Only contain SDF-specific parameters. **Coordinates are stored exactly as user specified - no transformation.**

Examples:

```c
struct yetty_ydraw_circle {
    float cx;
    float cy;
    float r;
};

struct yetty_ydraw_box {
    float cx;
    float cy;
    float hw;
    float hh;
    float round;
};
```

---

## Complex Primitives

Composites (yplot, nested ydraw, images, video) require more than simple SDF evaluation. They have their own rendering logic and may contain nested content.

### Types

| Type ID | Name | Description |
|---------|------|-------------|
| 0x80000001 | FONT | Font definition (atlas + metrics) |
| 0x80000002 | TEXT_DRAWABLE_LIST | Text with font reference |
| 0x80000003 | YPLOT | Function plot with yfsvm bytecode |
| 0x80000004+ | Reserved | Future: images, video, nested ydraw |

### Storage (Simplified vs Simple Primitives)

Unlike simple primitives which register in ALL overlapping grid cells, composites use simplified storage:

**Simple primitives:**
```
grid[row=5].add(drawable_ref{lines_ahead=2, drawable_index=0})
grid[row=6].add(drawable_ref{lines_ahead=1, drawable_index=0})
grid[row=7].add(drawable_ref{lines_ahead=0, drawable_index=0})
```

**Composites:**
```
line[7].figures.add(ptr)  // Only last overlapping line
```

Why simplified:
- Complex prims render via own pipelines, not via grid dispatch
- Last line reference sufficient for lifetime management
- When last line scrolls out → prim out of scope → release

### Direct Layer Rendering

Composites render directly to the ydraw layer texture at their viewport positions:

```
ydraw_layer_texture (render target)
┌─────────────────────────────────────────────────────┐
│                                                     │
│    ┌──────────┐                                     │
│    │  yplot0  │  ← viewport set to prim bounds     │
│    │          │    own pipeline, LoadOp=Load       │
│    └──────────┘                                     │
│                    ┌──────────┐                     │
│                    │  yplot1  │                     │
│                    └──────────┘                     │
│         ┌─────────────────┐                         │
│         │     image0      │                         │
│         └─────────────────┘                         │
└─────────────────────────────────────────────────────┘
         ↓
    Blend with text_layer (existing blender)
         ↓
    Screen
```

**Flow:**
1. `layer->render(render_target)` called by terminal
2. Render simple prims to render_target (LoadOp=Clear, full-screen quad, SDF shader)
3. For each composite: `instance->render(render_target, x, y)`
   - LoadOp=Load (preserves simple prims and previous complex prims)
   - Viewport set to instance's screen bounds (x, y, width, height)
   - Instance uses its factory's pre-compiled pipeline
   - Fragment shader fills the viewport region
4. Layer texture blended with other layers (text_layer, etc.)
5. Result → screen

**No atlas needed** - each primitive renders directly to its viewport region on the layer texture.

### Complex Prim Lifecycle

**Creation:**
- Instance created when primitive added to canvas
- Factory's pipeline already compiled (at registration time)

**Rendering:**
- Instance dirty flag checked
- If dirty: render to layer texture at viewport, clear dirty
- If clean: skip (previous frame's pixels preserved if layer not dirty)

**Release:**
- Instance attached to last overlapping line
- Line scrolls out → instance destroyed

### Recursive Composition (Future)

Composites may contain nested ydraw (recursive). Each nesting level would render to its parent's render target at its viewport bounds.

### Render Target Integration

The render target abstraction handles both local GPU and remote rendering
(via `yrdawn`):

**Local GPU:**
```
layer->render(render_target)
  ├── simple prims → full-screen pass
  └── complex prims → viewport passes (same target)
```

**Remote (`yrdawn`):**
```
Serialize primitive data → send to remote client/server
Remote renders using same approach, result composited as a yrdawn figure
```

### Wire Format (FAM - Flexible Array Member)

Composites in buffer use FAM format:

```
[type: u32][payload_size: u32][data: u8[payload_size]]
```

Type IDs use upper half of u32 range (≥ 0x80000000) to distinguish from simple SDF types (0-255).

### Drawable-list dispatch

Composite rendering follows drawable-list dispatch:

**Intrinsic (shared per type):**
- Shader code
- Uniforms (constants)

**Extrinsic (per instance):**
- Bounds, colors, content data
- Viewport position (computed at render time from bounds + scroll offset)

Type registration provides intrinsic parts. Instance data stored in buffer.

---

## Primitive Ops

Two-level ops structure for primitives:

**Base ops (all primitives - SDF and complex):**
- `size` - bytes in buffer, for iteration
- `aabb` - bounding box

**Complex prim ops (extends base):**
- `size` - same as base
- `aabb` - same as base
- `render` - render to render_target at viewport

SDF primitives use base ops only. Composites use extended ops with render.

---

## Complex Primitive Factory Pattern

```
ABSTRACT FACTORY
│
└── create(buffer_data) → instance
        └── reads type from buffer_data
        └── dispatches to concrete factory
        └── returns instance

CONCRETE FACTORY (per type, e.g. yplot)
│
├── binder (owns resource set + pre-compiled pipeline)
│     ├── resource_set (structure for buffers, textures, uniforms)
│     └── pipeline (compiled ONCE at factory creation, never recompiled)
│
└── create_instance(buffer_data) → instance

INSTANCE (stored in grid)
│
├── buffer_data (knows its size)
├── factory (back-pointer)
│
└── render(render_target, x, y)
```

### Abstract Factory

The abstract factory is a registry that maps primitive type IDs to concrete factories. It provides a single entry point for creating instances from buffer data.

When you call `create(buffer_data)`, the abstract factory reads the type from the beginning of the buffer, looks up the corresponding concrete factory, and delegates instance creation to it. The caller does not need to know which concrete factory handles which type.

### Concrete Factory

Each primitive type (yplot, image, video, etc.) has its own concrete factory. The concrete factory:

- Owns a **binder** that contains:
  - The resource set (defines structure for buffers, textures, uniforms)
  - The pre-compiled pipeline (compiled ONCE at factory creation using existing gpu-resource-binder, then stored permanently)
- Knows how to create instances from buffer data specific to its type.

The binder is created when the factory is registered (device/queue available). The pipeline is compiled once and held warm for the entire terminal lifetime. No recompilation ever happens.

### Instance

An instance represents a single occurrence of a composite. Instances are stored in the canvas grid. Each instance:

- Holds a copy of its buffer data, which contains all primitive-specific values including its size.
- Has a back-pointer to its concrete factory, giving access to the factory's binder.
- Provides a render method that takes a render target and x,y coordinates.

### Data Flow

**Adding primitives:**

1. Call `abstract_factory->create(buffer_data)`
2. Store the returned instance in the grid

The abstract factory handles all type dispatch internally.

**Rendering:**

1. Canvas calls `instance->render(render_target, x, y)`
2. The x,y coordinates are provided by the canvas to handle scrolling
3. The instance knows its own size from its buffer data
4. The instance uses its factory back-pointer to access the factory's binder
5. The instance binds its data to the binder's resource set:
   - **Buffers** (e.g., bytecode, vertex data)
   - **Textures** (if any, e.g., images)
   - **Uniforms** (e.g., bounds, colors, flags)
6. The binder uploads the data to GPU (no shader recompilation)
7. The binder renders with pre-compiled pipeline to the render target

### Key Point: No Recompilation

The factory's binder owns the pre-compiled pipeline. This pipeline is compiled ONCE when the factory is registered (at startup when device/queue become available). During rendering, only instance data (buffers, textures, uniforms) is bound and uploaded. The shader is never reparsed or recompiled.

---

## Direct Layer Rendering

Composites render directly to the layer texture at their viewport positions.

**Render flow:**
```
for each complex prim instance:
    if dirty:
        instance->render(render_target, x, y)
        # sets viewport to instance bounds
        # uses factory's pre-compiled pipeline
        # LoadOp=Load preserves existing pixels
        clear dirty
```

**Why direct rendering:**
- No separate texture per instance
- No compositing pass
- Each instance just renders to its viewport region
- Simple multi-pass to same render target

---

## Shader Libraries

Reusable shader code (like yfsvm) is structured as libraries.

**Directory structure:**
```
shaders/
  lib/
    yfsvm.wgsl      # library - pure functions
  yplot.wgsl        # main shader, uses yfsvm
```

**Library characteristics:**
- Pure functions, no `@group/@binding` declarations
- Take buffer pointers as parameters (not globals)
- Reusable across different primitives

**Example library function:**
```
fn yfsvm_execute(
    buf: ptr<storage, array<u32>, read>,
    offset: u32, ...
) -> f32 {
    let val = (*buf)[offset];
    ...
}
```

**Main shader calls library:**
```
let y = yfsvm_execute(&storage_buffer, bc_offset, ...);
```

**GPU resource set specifies libraries:**
- `libraries[]` field lists library names
- Binder concatenates: bindings → libraries → main shader

---

## Code Generation

Composites require **only two files** - everything else is generated.

**User provides:**
1. `<name>.yaml` - schema (uniforms, buffers, type_id, shader libraries)
2. `<name>.wgsl` - shader (rendering logic)

**Generator produces EVERYTHING else:**
- C header: type ID, offset constants, uniforms struct, serialization API
- C source: serialization implementation
- C factory: create/destroy, compile_pipeline, get_pipeline, get_shared_rs
- C instance: create/destroy, render (binds data to binder, calls render)
- Resource set initialization (from YAML uniforms/buffers definition)
- WGSL: accessor functions with calculated offsets

**Zero type-specific C code needed.** The factory pattern is 100% generic - every composite does the same thing:
1. Factory creates binder with resource set + compiled pipeline
2. Instance binds its data (uniforms, buffer, texture) to binder's RS
3. Binder renders

**Schema location:** `src/yetty/<module>/<module>.yaml`

**Schema defines:**
- `name` - primitive name
- `type_id` - unique ID (0x80000000+)
- `libraries` - shader dependencies (concatenated before main shader)
- `uniforms` - fixed-size data (name, type, count)
- `buffer` - variable-size buffer data
- `texture` - texture input (if needed)

**Standard binding layout (always the same):**
- 1 uniform block
- 1 buffer (single `array<u32>`)
- 1 texture (single texture)

If a primitive needs multiple logical buffers or textures, they are **packed into the single buffer/texture**. The YAML uniforms section describes offsets/names to access sub-parts within the single buffer or texture regions within the single texture.

**Serialization format:**
```
[type][payload_size][uniforms...][buffer_lengths...][buffer_data...]
```

Runtime binds bytes to pipeline - no interpretation. Only constructor (sender) and shader know semantics.

---

## Status

The composite code generator described above has shipped. The generator
and the abstract/concrete factory split live in `ydraw-core`, `ydraw-factory`,
and `ydraw-yaml`; complex figures (yplot, yimage, yvideo, …) are generated from a
per-module `model.yaml` + `.wgsl` pair.

**Done:**
- Type registry with result-based error handling
- Abstract factory pattern (maps type_id → concrete factory)
- Code generator: serialization, factory + instance boilerplate, resource-set
  initialization, and WGSL accessor functions from YAML

**Related modules:**
- `ydraw-core` — serialized primitive buffer, draw list, drawable-list registry
- `ydraw-factory` — figure factory for composites
- `ydraw-yaml` — YAML-driven figure construction
- `yfigure` — the figure/container model the compositor uses (see
  [Layered Rendering](../../../docs/layered-rendering.md))
