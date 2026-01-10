# Rich Text Renderer Design

## Overview

This document describes the design for a unified `RichTextRenderer` component that can render styled text from multiple sources (Markdown, PDF, Plot annotations, etc.) using GPU-accelerated MSDF rendering.

## Current State Analysis

### Font Instantiation

```
main.cpp
    └── Font font;                    // Created on stack
    └── pluginMgr->setFont(&font);    // Passed to plugin manager
            └── plugin->setFont(font_);   // Propagated to each plugin
```

The `Font` class:
- Generates MSDF atlas from TTF files (Regular, Bold, Italic, BoldItalic variants)
- Manages WebGPU texture and sampler
- Provides glyph metrics lookup

### Current Text Rendering Implementations

| Component | Font Source | Atlas Generation | Glyph Buffer | Shader |
|-----------|-------------|------------------|--------------|--------|
| **TextRenderer** | Shared Font | At startup | Cell textures | Grid-based |
| **Markdown** | Shared Font | At startup | Storage buffer | Instanced quads |
| **PDF** | Embedded fonts | On-demand per PDF | Storage buffer | Instanced quads |

**Key observation**: Markdown and PDF have **nearly identical** rendering code:
- Same `GlyphInstance` struct (48 bytes: pos, uv, size, color)
- Same WGSL shader (vertex instancing + MSDF fragment)
- Same pipeline setup
- Same `wgpuRenderPassEncoderDraw(pass, 6, glyphCount_, 0, 0)`

## Proposed Architecture

### Core Components

```
src/yetty/renderer/
├── rich-text-renderer.h      # New: Unified text rendering
├── rich-text-renderer.cpp
├── font-manager.h            # New: Multi-font support (Phase 2)
├── font-manager.cpp
├── text-renderer.h           # Existing: Terminal grid renderer
└── text-renderer.cpp
```

### RichTextRenderer API

```cpp
namespace yetty {

// Glyph instance for GPU (48 bytes, matches existing)
struct RichGlyph {
    float posX, posY;
    float uvMinX, uvMinY, uvMaxX, uvMaxY;
    float sizeX, sizeY;
    float colorR, colorG, colorB, colorA;
};

// Text span with styling
struct TextSpan {
    std::string text;
    float x = 0, y = 0;           // Position in pixels
    float scale = 1.0f;           // Size multiplier
    Font::Style style = Font::Regular;
    float r = 1, g = 1, b = 1, a = 1;  // Color
    bool wrap = false;            // Word wrap enabled
    float maxWidth = 0;           // Wrap width (if wrap=true)
};

class RichTextRenderer {
public:
    RichTextRenderer();
    ~RichTextRenderer();

    // Initialize with WebGPU context
    Result<void> init(WebGPUContext& ctx, WGPUTextureFormat targetFormat);
    void dispose();

    // Set font atlas to use
    void setFont(Font* font);
    void setPixelRange(float range);

    // Clear all spans
    void clear();

    // Add text spans
    void addSpan(const TextSpan& span);
    void addSpans(const std::vector<TextSpan>& spans);

    // Layout and upload glyphs to GPU
    // Call after adding spans, before render()
    Result<void> prepare(WebGPUContext& ctx);

    // Render to target
    // viewport: NDC rect (x, y, w, h) where text is rendered
    // screenSize: full screen dimensions for coordinate conversion
    // scrollOffset: vertical scroll in pixels
    Result<void> render(WebGPUContext& ctx,
                        WGPUTextureView targetView,
                        const float viewport[4],
                        const float screenSize[2],
                        float scrollOffset = 0);

    // Query
    float getContentHeight() const;
    uint32_t getGlyphCount() const;

private:
    void layoutSpan(const TextSpan& span);

    Font* font_ = nullptr;
    float pixelRange_ = 4.0f;
    std::vector<RichGlyph> glyphs_;
    float contentHeight_ = 0;

    // GPU resources
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUBuffer glyphBuffer_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    bool gpuInitialized_ = false;
};

} // namespace yetty
```

### YAML Input Format (for plugins)

```yaml
# Optional defaults
defaults:
  size: 12
  color: [0.9, 0.9, 0.9, 1.0]

spans:
  - text: "Title"
    x: 10
    y: 5
    size: 24
    bold: true
    color: [1.0, 1.0, 1.0, 1.0]

  - text: "Subtitle"
    x: 10
    y: 35
    size: 16
    italic: true

  - text: "Body text that wraps..."
    x: 10
    y: 60
    wrap: true
    max_width: 400
```

## Migration Plan

### Phase 1: Extract RichTextRenderer

1. Create `rich-text-renderer.h/.cpp` with API above
2. Extract common code from Markdown plugin:
   - GlyphInstance struct
   - WGSL shader
   - Pipeline creation
   - Layout logic (UTF-8 decoding, glyph positioning)
3. **Do NOT modify Markdown/PDF yet** - just create the new component

### Phase 2: Integrate with Plugins

1. Refactor Markdown to use RichTextRenderer
2. Refactor PDF to use RichTextRenderer
3. Add RichTextRenderer to Plot plugin for annotations

### Phase 3: Multi-Font Support (Future)

Current limitation: `Font` class = one font family.

Options for multi-font:

**Option A: FontManager with Shared Atlas**
```cpp
class FontManager {
    // Pack multiple fonts into one atlas
    void addFont(const std::string& name, const std::string& path);
    Font* getFont(const std::string& name);
    WGPUTextureView getSharedAtlas();
};
```
- Pro: Single texture bind, efficient
- Con: Complex atlas packing, size limits

**Option B: Per-Font Atlases**
```cpp
// RichTextRenderer switches bind groups per font
void setActiveFont(const std::string& name);
```
- Pro: Simple, no atlas packing
- Con: More draw calls if mixing fonts

**Option C: Texture Array**
```cpp
// Array of font atlases, font index in glyph data
WGPUTexture fontAtlasArray;  // texture_2d_array
```
- Pro: Single bind group, GPU-side font selection
- Con: WebGPU texture array complexity

**Recommendation**: Start with Option A (shared atlas) since PDF plugin already does dynamic MSDF generation and atlas packing.

## PDF Integration Consideration

The PDF plugin currently:
1. Extracts embedded fonts via MuPDF → FreeType
2. Generates MSDF atlas on-the-fly
3. Renders with its own pipeline

To unify with RichTextRenderer:

**Option 1: PDF → Internal Representation → RichTextRenderer**
```
PDF (MuPDF) → PDFTextChar[] → TextSpan[] → RichTextRenderer
```
- PDF extracts text with positions, sizes, styles
- Convert to TextSpan array
- RichTextRenderer handles rendering
- **Requires**: RichTextRenderer to support per-glyph font switching OR pre-merge fonts into shared atlas

**Option 2: PDF keeps custom font handling**
- PDF continues generating its own MSDF atlases
- But uses RichTextRenderer for the actual GPU rendering
- Pass custom atlas texture to RichTextRenderer

**Recommendation**: Option 2 is simpler for Phase 1. PDF's font extraction is complex (embedded fonts vary wildly). Keep that logic, but share the rendering pipeline.

## Complexity Assessment

| Task | Difficulty | Notes |
|------|------------|-------|
| Extract RichTextRenderer from Markdown | Low | Copy + refactor |
| Integrate with Markdown | Low | Replace inline code with calls |
| Integrate with PDF | Medium | Font atlas handling differs |
| Add to Plot plugin | Low | Just use the API |
| Multi-font support | High | Atlas packing, font management |
| YAML parsing | Low | Use existing yaml-cpp |

## File Changes Summary

### New Files
- `src/yetty/renderer/rich-text-renderer.h`
- `src/yetty/renderer/rich-text-renderer.cpp`

### Modified Files (Phase 2)
- `src/yetty/plugins/markdown/markdown.cpp` - Use RichTextRenderer
- `src/yetty/plugins/pdf/pdf.cpp` - Use RichTextRenderer
- `src/yetty/plugins/plot/plot.cpp` - Add annotations
- `CMakeLists.txt` - Add new source file

### Future Files (Phase 3)
- `src/yetty/renderer/font-manager.h`
- `src/yetty/renderer/font-manager.cpp`

## Next Steps

1. **Approve this design** or suggest modifications
2. Implement `RichTextRenderer` as standalone component
3. Write unit test / demo plugin to verify it works
4. Integrate with existing plugins one by one
