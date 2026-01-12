#pragma once

#include <yetty/font.h>
#include <yetty/font-manager.h>
#include <yetty/result.hpp>
#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <glm/glm.hpp>

namespace yetty {

class WebGPUContext;

//-----------------------------------------------------------------------------
// GlyphInstance - positioned glyph for GPU rendering (52 bytes)
//-----------------------------------------------------------------------------
struct GlyphInstance {
    float posX, posY;                          // Screen position
    float uvMinX, uvMinY, uvMaxX, uvMaxY;      // UV coordinates in atlas
    float sizeX, sizeY;                        // Glyph size in pixels
    float colorR, colorG, colorB, colorA;      // Color
    float scale;                               // Scale factor for pixelRange adjustment
};

//-----------------------------------------------------------------------------
// TextChar - a character with position and style (like PDFTextChar)
// Used internally after layout, or can be populated directly (e.g., from PDF)
//-----------------------------------------------------------------------------
struct TextChar {
    uint32_t codepoint;
    float x, y;                                // Position in document coordinates
    float size;                                // Font size
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    Font::Style style = Font::Regular;
    std::string fontFamily;                    // Font family (resolved via FontManager)
};

//-----------------------------------------------------------------------------
// TextSpan - a run of styled text (high-level input)
// Used by markdown parser, YAML input, etc.
//-----------------------------------------------------------------------------
struct TextSpan {
    std::string text;
    float x = 0, y = 0;                        // Position in document coordinates
    float size = 16.0f;                        // Font size in pixels
    Font::Style style = Font::Regular;         // Bold/Italic
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};  // RGBA
    std::string fontFamily;                    // Font family (resolved via FontManager)

    // Layout options
    bool wrap = false;                         // Word wrap enabled
    float maxWidth = 0;                        // Wrap width (if wrap=true)
    float lineHeight = 0;                      // Custom line height (0 = auto)
};

//-----------------------------------------------------------------------------
// RichText - styled text document with GPU rendering
// Modeled after PDFLayer - can be used by Markdown, PDF, YAML input, etc.
//-----------------------------------------------------------------------------
class RichText {
public:
    using Ptr = std::shared_ptr<RichText>;

    static Result<Ptr> create(WebGPUContext* ctx, WGPUTextureFormat targetFormat, FontManager* fontMgr) noexcept;

    ~RichText();

    // Non-copyable
    RichText(const RichText&) = delete;
    RichText& operator=(const RichText&) = delete;

    void dispose() noexcept;

    //-------------------------------------------------------------------------
    // Font management
    //-------------------------------------------------------------------------
    FontManager* getFontManager() const { return fontMgr_; }

    // Set default font family (used when TextChar/TextSpan has empty fontFamily)
    void setDefaultFontFamily(const std::string& family) { defaultFontFamily_ = family; }

    //-------------------------------------------------------------------------
    // Content building - two approaches:
    // 1. High-level: addSpan() for styled text runs
    // 2. Low-level: addChar() for pre-positioned characters (like PDF)
    //-------------------------------------------------------------------------
    void clear();

    // High-level: add styled text spans (will be laid out)
    void addSpan(const TextSpan& span);
    void addSpans(const std::vector<TextSpan>& spans);

    // Low-level: add pre-positioned characters (already laid out)
    void addChar(const TextChar& ch);
    void addChars(const std::vector<TextChar>& chars);

    //-------------------------------------------------------------------------
    // Layout - converts spans/chars to GPU-ready glyphs
    //-------------------------------------------------------------------------
    void layout(float viewWidth, float viewHeight);
    void setNeedsLayout() { layoutDirty_ = true; }

    //-------------------------------------------------------------------------
    // Rendering - renders into existing render pass
    //-------------------------------------------------------------------------
    Result<void> render(WGPURenderPassEncoder pass, WebGPUContext& ctx,
                      uint32_t screenWidth, uint32_t screenHeight,
                      float pixelX, float pixelY,
                      float pixelW, float pixelH);

    //-------------------------------------------------------------------------
    // Scrolling
    //-------------------------------------------------------------------------
    void setScrollOffset(float offset);
    float getScrollOffset() const { return scrollOffset_; }
    void scroll(float delta);  // Relative scroll

    // Content dimensions
    float getContentHeight() const { return contentHeight_; }
    float getContentWidth() const { return contentWidth_; }

    //-------------------------------------------------------------------------
    // Query
    //-------------------------------------------------------------------------
    uint32_t getGlyphCount() const { return glyphCount_; }
    bool isEmpty() const { return spans_.empty() && chars_.empty(); }

private:
    RichText(WebGPUContext* ctx, WGPUTextureFormat targetFormat, FontManager* fontMgr) noexcept;
    Result<void> init() noexcept;

    Result<void> createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat);
    Result<void> createBindGroup(WebGPUContext& ctx, Font* font);
    Result<void> uploadGlyphBuffer(WebGPUContext& ctx);

    void layoutSpans(float viewWidth, float viewHeight);
    void layoutChars(float viewWidth, float viewHeight);
    void buildGlyphInstances();

    // Resolve font for a character/span
    Font* resolveFont(const std::string& fontFamily, Font::Style style);

    // Decode UTF-8 codepoint from string
    static uint32_t decodeUTF8(const uint8_t*& ptr, const uint8_t* end);

    //-------------------------------------------------------------------------
    // Content
    //-------------------------------------------------------------------------
    FontManager* fontMgr_ = nullptr;
    std::string defaultFontFamily_;
    std::vector<TextSpan> spans_;              // High-level input
    std::vector<TextChar> chars_;              // Low-level/laid out characters

    //-------------------------------------------------------------------------
    // Font-grouped rendering
    //-------------------------------------------------------------------------
    struct FontGlyphBatch {
        Font* font = nullptr;
        std::vector<GlyphInstance> glyphs;
        WGPUBindGroup bindGroup = nullptr;     // Per-font bind group (for font atlas)
    };
    std::vector<FontGlyphBatch> fontBatches_;  // Grouped by font for rendering

    //-------------------------------------------------------------------------
    // Layout state
    //-------------------------------------------------------------------------
    float contentHeight_ = 0;
    float contentWidth_ = 0;
    float scrollOffset_ = 0;
    float lastViewWidth_ = 0;
    float lastViewHeight_ = 0;
    bool layoutDirty_ = true;
    bool useCharsDirectly_ = false;  // If true, skip span layout

    //-------------------------------------------------------------------------
    // GPU resources (shared across all fonts)
    //-------------------------------------------------------------------------
    WebGPUContext* _ctx = nullptr;
    WGPUTextureFormat targetFormat_ = WGPUTextureFormat_Undefined;
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUBuffer glyphBuffer_ = nullptr;         // Shared glyph buffer, re-uploaded per batch
    WGPUSampler sampler_ = nullptr;
    uint32_t glyphCount_ = 0;                  // Total glyph count across all batches
    uint32_t glyphBufferCapacity_ = 0;

    // Cache for bind groups per font (avoid recreating every frame)
    std::map<Font*, WGPUBindGroup> fontBindGroups_;
    // Track font resource versions to detect when bind groups need recreation
    std::map<Font*, uint32_t> fontResourceVersions_;

    static constexpr float pixelRange_ = 4.0f;
    bool initialized_ = false;
    bool gpuResourcesDirty_ = true;
};

} // namespace yetty
