# Font System

## Interface

Fonts implement the `yetty_font_font` ops interface:

```c
struct yetty_font_font_ops {
    void (*destroy)(struct yetty_font_font *self);
    enum yetty_font_render_method (*render_method)(const struct yetty_font_font *self);
    uint32_t (*get_glyph_index)(struct yetty_font_font *self, uint32_t codepoint);
    uint32_t (*get_glyph_index_styled)(struct yetty_font_font *self, uint32_t codepoint, enum yetty_font_style style);
    void (*set_cell_size)(struct yetty_font_font *self, float cell_width, float cell_height);
    int (*is_dirty)(const struct yetty_font_font *self);
    void (*clear_dirty)(struct yetty_font_font *self);
    struct yetty_yrender_gpu_resource_set_result (*get_gpu_resource_set)(const struct yetty_font_font *self);
};
```

## Raster Font (FreeType)

Current backend. Rasterizes glyphs into an R8 atlas texture.

### On-Demand Rasterization

Glyphs are not preloaded. When vterm resolves a codepoint via the glyph resolver callback, `get_glyph_index_styled()` checks if the glyph exists in the atlas. If not, it rasterizes it immediately and appends to the atlas.

### Atlas Layout

Shelf-packing algorithm. Each glyph slot is `cell_width + 2*padding` by `cell_height + 2*padding` pixels. Slots pack left-to-right in rows. When a row is full, start a new row. When the atlas is full, grow it.

```
+--+--+--+--+--+--+--+--+
|  |h |e |l |o | w|r |d |  ← row 0
+--+--+--+--+--+--+--+--+
|! |@ |# |  |  |  |  |  |  ← row 1
+--+--+--+--+--+--+--+--+
```

### Atlas Growth

When a glyph doesn't fit, the atlas grows: width extends by 8 glyph slots, height by 1 row. Existing pixel data is copied to the new buffer. All glyph UVs are rescaled to the new dimensions.

Growth sets `dirty = 1`, which propagates to the GPU resource set's texture and buffer dirty flags.

### Glyph UV Buffer

Parallel to the atlas texture. Each slot has a `{uv_x, uv_y}` pair (2 floats) pointing to the top-left corner of the glyph slot in atlas UV space (0-1 range). Slot 0 is reserved for space/empty with UV (-1, -1).

### Style Support

Four faces loaded from font files: Regular, Bold, Oblique, BoldOblique. If a styled face is unavailable, falls back to Regular.

### GPU Resource Set

The font provides a `yetty_yrender_gpu_resource_set` with:
- `textures[0]` — R8 atlas texture
- `buffers[0]` — glyph UV buffer (`array<f32>`, 2 floats per glyph)
- No uniforms, no shader code, no children

The text layer includes the font's resource set as a child in its own resource set tree.

## Headers

- `include/yetty/yfont/font.h` — interface (the type prefix is `yetty_font_`)
- `include/yetty/yfont/raster-font.h` — raster font creation
- `src/yetty/yfont/raster-font.c` — implementation
