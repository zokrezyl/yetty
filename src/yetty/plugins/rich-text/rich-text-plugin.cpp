#include "rich-text-plugin.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace yetty {

//-----------------------------------------------------------------------------
// RichTextPlugin
//-----------------------------------------------------------------------------

RichTextPlugin::~RichTextPlugin() {
    (void)dispose();
}

Result<PluginPtr> RichTextPlugin::create(YettyPtr engine) noexcept {
    auto p = PluginPtr(new RichTextPlugin(std::move(engine)));
    if (auto res = static_cast<RichTextPlugin*>(p.get())->init(); !res) {
        return Err<PluginPtr>("Failed to init RichTextPlugin", res);
    }
    return Ok(p);
}

Result<void> RichTextPlugin::init() noexcept {
    // Verify engine has a FontManager
    if (!engine_ || !engine_->fontManager()) {
        return Err<void>("RichTextPlugin: engine has no FontManager");
    }

    _initialized = true;
    spdlog::info("RichTextPlugin initialized (using engine's FontManager)");
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
    return engine_ ? engine_->fontManager().get() : nullptr;
}

Result<PluginLayerPtr> RichTextPlugin::createLayer(const std::string& payload) {
    auto layer = std::make_shared<RichTextLayer>(this);
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to init RichTextLayer", result);
    }
    return Ok<PluginLayerPtr>(layer);
}

Result<void> RichTextPlugin::renderAll(WGPUTextureView targetView,
                                        WGPUTextureFormat targetFormat,
                                        uint32_t screenWidth,
                                        uint32_t screenHeight,
                                        float cellWidth, float cellHeight,
                                        int scrollOffset, uint32_t termRows,
                                        bool isAltScreen) {
    if (!engine_) return Err<void>("RichTextPlugin::renderAll: no engine");

    ScreenType currentScreen = isAltScreen ? ScreenType::Alternate : ScreenType::Main;

    for (auto& layerBase : _layers) {
        if (!layerBase->isVisible()) continue;
        if (layerBase->getScreenType() != currentScreen) continue;

        auto layer = std::static_pointer_cast<RichTextLayer>(layerBase);

        float pixelX = layer->getX() * cellWidth;
        float pixelY = layer->getY() * cellHeight;
        float pixelW = layer->getWidthCells() * cellWidth;
        float pixelH = layer->getHeightCells() * cellHeight;

        if (layer->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
            pixelY += scrollOffset * cellHeight;
        }

        if (termRows > 0) {
            float screenPixelHeight = termRows * cellHeight;
            if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
                continue;
            }
        }

        if (auto res = layer->render(*engine_->context(), targetView, targetFormat,
                                      screenWidth, screenHeight,
                                      pixelX, pixelY, pixelW, pixelH); !res) {
            return Err<void>("Failed to render RichTextLayer", res);
        }
    }
    return Ok();
}

//-----------------------------------------------------------------------------
// RichTextLayer
//-----------------------------------------------------------------------------

RichTextLayer::RichTextLayer(RichTextPlugin* plugin)
    : plugin_(plugin) {}

RichTextLayer::~RichTextLayer() {
    (void)dispose();
}

Result<void> RichTextLayer::init(const std::string& payload) {
    if (payload.empty()) {
        return Err<void>("RichTextLayer: empty payload");
    }

    _payload = payload;
    (void)dispose();

    return parseYAML(payload);
}

Result<void> RichTextLayer::dispose() {
    if (richText_) {
        richText_->dispose();
        richText_.reset();
    }
    initialized_ = false;
    failed_ = false;
    return Ok();
}

//-----------------------------------------------------------------------------
// YAML Parsing
//-----------------------------------------------------------------------------

Result<void> RichTextLayer::parseYAML(const std::string& yaml) {
    try {
        YAML::Node root = YAML::Load(yaml);

        // Get font name (optional) - if not specified, will use FontManager's default
        fontName_ = "";
        if (root["font-name"]) {
            fontName_ = root["font-name"].as<std::string>();
        }

        // Parse spans
        if (!root["spans"] || !root["spans"].IsSequence()) {
            return Err<void>("RichTextLayer: YAML must have 'spans' array");
        }

        // Clear pending spans (RichText will be created in render() when we have ctx)
        pendingSpans_.clear();

        float cursorY = 0;
        float lastLineHeight = 20.0f;

        for (const auto& spanNode : root["spans"]) {
            TextSpan span;

            // Required: text
            if (!spanNode["text"]) {
                spdlog::warn("RichTextLayer: span missing 'text', skipping");
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

        spdlog::info("RichTextLayer: parsed {} spans from YAML", root["spans"].size());
        return Ok();

    } catch (const YAML::Exception& e) {
        return Err<void>(std::string("YAML parse error: ") + e.what());
    }
}

//-----------------------------------------------------------------------------
// Render
//-----------------------------------------------------------------------------

Result<void> RichTextLayer::render(WebGPUContext& ctx,
                                    WGPUTextureView targetView,
                                    WGPUTextureFormat targetFormat,
                                    uint32_t screenWidth, uint32_t screenHeight,
                                    float pixelX, float pixelY,
                                    float pixelW, float pixelH) {
    if (failed_) {
        return Err<void>("RichTextLayer already failed");
    }

    // Initialize RichText GPU resources if needed
    if (!richText_) {
        auto fontMgr = plugin_->getFontManager();
        if (!fontMgr) {
            failed_ = true;
            return Err<void>("No FontManager available for RichText rendering");
        }

        auto result = RichText::create(&ctx, targetFormat, fontMgr);
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

        initialized_ = true;
    }

    // Render
    return richText_->render(ctx, targetView, screenWidth, screenHeight,
                              pixelX, pixelY, pixelW, pixelH);
}

//-----------------------------------------------------------------------------
// Mouse Scroll
//-----------------------------------------------------------------------------

bool RichTextLayer::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)xoffset;
    (void)mods;

    if (!richText_) return false;

    float scrollAmount = yoffset * 40.0f;
    richText_->scroll(-scrollAmount);

    // Clamp scroll
    float maxScroll = std::max(0.0f, richText_->getContentHeight() - static_cast<float>(_pixel_height));
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

yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine) {
    return yetty::RichTextPlugin::create(std::move(engine));
}

}
