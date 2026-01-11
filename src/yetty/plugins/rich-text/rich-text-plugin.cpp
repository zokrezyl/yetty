#include "rich-text-plugin.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <ytrace/ytrace.hpp>
#include <yaml-cpp/yaml.h>
#include <ytrace/ytrace.hpp>
#include <algorithm>
#include <cmath>

namespace yetty {

//-----------------------------------------------------------------------------
// RichTextPlugin
//-----------------------------------------------------------------------------

RichTextPlugin::~RichTextPlugin() {
    (void)dispose();
}

Result<PluginPtr> RichTextPlugin::create() noexcept {
    auto p = PluginPtr(new RichTextPlugin());
    if (auto res = static_cast<RichTextPlugin*>(p.get())->pluginInit(); !res) {
        return Err<PluginPtr>("Failed to init RichTextPlugin", res);
    }
    return Ok(p);
}

Result<void> RichTextPlugin::pluginInit() noexcept {
    _initialized = true;
    yinfo("RichTextPlugin initialized");
    return Ok();
}

Result<void> RichTextPlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose RichTextPlugin", res);
    }
    _initialized = false;
    return Ok();
}

FontManager* RichTextPlugin::getFontManager() {
    return _fontManager;
}

Result<WidgetPtr> RichTextPlugin::createWidget(
    const std::string& widgetName,
    WidgetFactory* factory,
    FontManager* fontManager,
    uv_loop_t* loop,
    int32_t x,
    int32_t y,
    uint32_t widthCells,
    uint32_t heightCells,
    const std::string& pluginArgs,
    const std::string& payload
) {
    (void)widgetName;
    (void)factory;
    (void)loop;
    (void)x;
    (void)y;
    (void)widthCells;
    (void)heightCells;
    (void)pluginArgs;
    _fontManager = fontManager;  // Store for widget use
    yfunc();
    yinfo("payload size={}", payload.size());
    return RichTextW::create(payload, this);
}

//-----------------------------------------------------------------------------
// RichTextW
//-----------------------------------------------------------------------------

RichTextW::~RichTextW() {
    (void)dispose();
}

Result<void> RichTextW::init() {
    if (_payload.empty()) {
        return Err<void>("RichTextW: empty payload");
    }

    return parseYAML(_payload);
}

Result<void> RichTextW::dispose() {
    if (richText_) {
        richText_->dispose();
        richText_.reset();
    }
    _initialized = false;
    failed_ = false;
    return Ok();
}

//-----------------------------------------------------------------------------
// YAML Parsing
//-----------------------------------------------------------------------------

Result<void> RichTextW::parseYAML(const std::string& yaml) {
    try {
        YAML::Node root = YAML::Load(yaml);

        // Get font name (optional) - if not specified, will use FontManager's default
        fontName_ = "";
        if (root["font-name"]) {
            fontName_ = root["font-name"].as<std::string>();
        }

        // Parse spans
        if (!root["spans"] || !root["spans"].IsSequence()) {
            return Err<void>("RichTextW: YAML must have 'spans' array");
        }

        // Clear pending spans (RichText will be created in render() when we have ctx)
        pendingSpans_.clear();

        float cursorY = 0;
        float lastLineHeight = 20.0f;

        for (const auto& spanNode : root["spans"]) {
            TextSpan span;

            // Required: text
            if (!spanNode["text"]) {
                ywarn("RichTextW: span missing 'text', skipping");
                continue;
            }
            span.text = spanNode["text"].as<std::string>();

            // Position
            if (spanNode["x"]) {
                span.x = spanNode["x"].as<float>();
            }
            if (spanNode["y"]) {
                span.y = spanNode["y"].as<float>();
                cursorY = span.y;
            } else {
                // Continue from previous line
                span.y = cursorY;
            }

            // Size
            span.size = 16.0f;
            if (spanNode["size"]) {
                span.size = spanNode["size"].as<float>();
            }

            // Style
            span.style = Font::Regular;
            if (spanNode["style"]) {
                std::string styleStr = spanNode["style"].as<std::string>();
                if (styleStr == "bold") {
                    span.style = Font::Bold;
                } else if (styleStr == "italic") {
                    span.style = Font::Italic;
                } else if (styleStr == "bolditalic" || styleStr == "bold-italic") {
                    span.style = Font::BoldItalic;
                }
            }

            // Color (array of 3 or 4 floats)
            span.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            if (spanNode["color"] && spanNode["color"].IsSequence()) {
                auto colorSeq = spanNode["color"];
                if (colorSeq.size() >= 3) {
                    span.color.r = colorSeq[0].as<float>();
                    span.color.g = colorSeq[1].as<float>();
                    span.color.b = colorSeq[2].as<float>();
                    if (colorSeq.size() >= 4) {
                        span.color.a = colorSeq[3].as<float>();
                    }
                }
            }

            // Font name (per-span override, uses exact font name)
            if (spanNode["font-name"]) {
                span.fontFamily = spanNode["font-name"].as<std::string>();
            }

            // Wrap options
            span.wrap = false;
            if (spanNode["wrap"]) {
                span.wrap = spanNode["wrap"].as<bool>();
            }

            span.maxWidth = 0;
            if (spanNode["max-width"]) {
                span.maxWidth = spanNode["max-width"].as<float>();
            }

            span.lineHeight = 0;
            if (spanNode["line-height"]) {
                span.lineHeight = spanNode["line-height"].as<float>();
                lastLineHeight = span.lineHeight;
            }

            pendingSpans_.push_back(span);

            // Update cursor for next span (if no explicit y)
            if (!spanNode["y"]) {
                // Check if text contains newlines to advance cursor
                size_t newlines = std::count(span.text.begin(), span.text.end(), '\n');
                if (newlines > 0) {
                    float lh = span.lineHeight > 0 ? span.lineHeight : span.size * 1.2f;
                    cursorY += newlines * lh;
                    lastLineHeight = lh;
                }
            }
        }

        yinfo("RichTextW: parsed {} spans from YAML", root["spans"].size());
        return Ok();

    } catch (const YAML::Exception& e) {
        return Err<void>(std::string("YAML parse error: ") + e.what());
    }
}

//-----------------------------------------------------------------------------
// Render
//-----------------------------------------------------------------------------

Result<void> RichTextW::render(WebGPUContext& ctx) {
    if (failed_) {
        return Err<void>("RichTextW already failed");
    }
    if (!_visible) return Ok();

    // Get render context set by owner
    const auto& rc = _renderCtx;

    // Calculate pixel position from cell position
    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _widthCells * rc.cellWidth;
    float pixelH = _heightCells * rc.cellHeight;

    // For Relative widgets, adjust position when viewing scrollback
    if (_positionMode == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

    // Skip if off-screen (not an error)
    if (rc.termRows > 0) {
        float screenPixelHeight = rc.termRows * rc.cellHeight;
        if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
            return Ok();
        }
    }

    // Initialize RichText GPU resources if needed
    if (!richText_) {
        auto fontMgr = plugin_->getFontManager();
        if (!fontMgr) {
            failed_ = true;
            return Err<void>("No FontManager available for RichText rendering");
        }

        auto result = RichText::create(&ctx, rc.targetFormat, fontMgr);
        if (!result) {
            failed_ = true;
            return Err<void>("Failed to create RichText", result);
        }
        richText_ = *result;

        // Set default font family
        richText_->setDefaultFontFamily(fontName_);

        // Add pending spans
        for (const auto& span : pendingSpans_) {
            richText_->addSpan(span);
        }
        pendingSpans_.clear();

        _initialized = true;
    }

    // Render
    return richText_->render(ctx, rc.targetView, rc.screenWidth, rc.screenHeight,
                              pixelX, pixelY, pixelW, pixelH);
}

bool RichTextW::render(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    if (failed_ || !_visible) return false;

    const auto& rc = _renderCtx;

    // Calculate pixel position from cell position
    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _widthCells * rc.cellWidth;
    float pixelH = _heightCells * rc.cellHeight;

    // For Relative widgets, adjust position when viewing scrollback
    if (_positionMode == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

    // Skip if off-screen
    if (rc.termRows > 0) {
        float screenPixelHeight = rc.termRows * rc.cellHeight;
        if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
            return false;
        }
    }

    // Initialize RichText GPU resources if needed
    if (!richText_) {
        auto fontMgr = plugin_->getFontManager();
        if (!fontMgr) {
            failed_ = true;
            return false;
        }

        auto result = RichText::create(&ctx, rc.targetFormat, fontMgr);
        if (!result) {
            failed_ = true;
            return false;
        }
        richText_ = *result;

        // Set default font family
        richText_->setDefaultFontFamily(fontName_);

        // Add pending spans
        for (const auto& span : pendingSpans_) {
            richText_->addSpan(span);
        }
        pendingSpans_.clear();

        _initialized = true;
    }

    // Use batched render
    return richText_->render(pass, ctx, rc.screenWidth, rc.screenHeight,
                                    pixelX, pixelY, pixelW, pixelH);
}

//-----------------------------------------------------------------------------
// Mouse Scroll
//-----------------------------------------------------------------------------

bool RichTextW::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)xoffset;
    (void)mods;

    if (!richText_) return false;

    float scrollAmount = yoffset * 40.0f;
    richText_->scroll(-scrollAmount);

    // Clamp scroll
    float maxScroll = std::max(0.0f, richText_->getContentHeight() - static_cast<float>(_pixelHeight));
    if (richText_->getScrollOffset() > maxScroll) {
        richText_->setScrollOffset(maxScroll);
    }

    return true;
}

} // namespace yetty

//-----------------------------------------------------------------------------
// Plugin exports
//-----------------------------------------------------------------------------

extern "C" {

const char* name() {
    return "rich-text";
}

yetty::Result<yetty::PluginPtr> create() {
    return yetty::RichTextPlugin::create();
}

}
