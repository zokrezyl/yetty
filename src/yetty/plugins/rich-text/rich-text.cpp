#include "rich-text.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <ytrace/ytrace.hpp>
#include <yaml-cpp/yaml.h>
#include <ytrace/ytrace.hpp>
#include <algorithm>
#include <cmath>

namespace yetty::plugins {

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
    _fontManager = fontManager;  // Store for widget use
    yfunc();
    yinfo("payload size={} x={} y={} w={} h={}", payload.size(), x, y, widthCells, heightCells);
    return RichText::create(factory, fontManager, loop, x, y, widthCells, heightCells, pluginArgs, payload, this);
}

//-----------------------------------------------------------------------------
// RichText
//-----------------------------------------------------------------------------

RichText::~RichText() {
    (void)dispose();
}

Result<void> RichText::init() {
    if (_payload.empty()) {
        return Err<void>("RichText: empty payload");
    }

    return parseYAML(_payload);
}

Result<void> RichText::dispose() {
    if (_richText) {
        _richText->dispose();
        _richText.reset();
    }
    _initialized = false;
    _failed = false;
    return Ok();
}

//-----------------------------------------------------------------------------
// YAML Parsing
//-----------------------------------------------------------------------------

Result<void> RichText::parseYAML(const std::string& yaml) {
    try {
        YAML::Node root = YAML::Load(yaml);

        // Get font name (optional) - if not specified, will use FontManager's default
        _fontName = "";
        if (root["font-name"]) {
            _fontName = root["font-name"].as<std::string>();
        }

        // Parse spans
        if (!root["spans"] || !root["spans"].IsSequence()) {
            return Err<void>("RichText: YAML must have 'spans' array");
        }

        // Clear pending spans (RichText will be created in render() when we have ctx)
        _pendingSpans.clear();

        float cursorY = 0;
        float lastLineHeight = 20.0f;

        for (const auto& spanNode : root["spans"]) {
            TextSpan span;

            // Required: text
            if (!spanNode["text"]) {
                ywarn("RichText: span missing 'text', skipping");
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

            _pendingSpans.push_back(span);

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

        yinfo("RichText: parsed {} spans from YAML", root["spans"].size());
        return Ok();

    } catch (const YAML::Exception& e) {
        return Err<void>(std::string("YAML parse error: ") + e.what());
    }
}

//-----------------------------------------------------------------------------
// Render
//-----------------------------------------------------------------------------

void RichText::prepareFrame(WebGPUContext& ctx, bool on) {
    (void)ctx;
    (void)on;
    // RichText renders directly to render pass - no texture preparation needed
}

Result<void> RichText::render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on) {
    if (!on || _failed || !_visible) return Ok();

    yinfo("RichText::render _pixelX={} _pixelY={} _pixelWidth={} _pixelHeight={}",
          _pixelX, _pixelY, _pixelWidth, _pixelHeight);

    // Initialize RichText GPU resources if needed
    if (!_richText) {
        auto fontMgr = _plugin->getFontManager();
        if (!fontMgr) {
            _failed = true;
            return Err<void>("RichText: no font manager");
        }

        auto result = yetty::RichText::create(&ctx, ctx.getSurfaceFormat(), fontMgr);
        if (!result) {
            _failed = true;
            return Err<void>("RichText: failed to create RichText", result);
        }
        _richText = *result;
        _richText->setDefaultFontFamily(_fontName);

        for (const auto& span : _pendingSpans) {
            _richText->addSpan(span);
        }
        _pendingSpans.clear();
        _initialized = true;
    }

    return _richText->render(pass, ctx, ctx.getSurfaceWidth(), ctx.getSurfaceHeight(),
                             _pixelX, _pixelY, static_cast<float>(_pixelWidth), static_cast<float>(_pixelHeight));
}

//-----------------------------------------------------------------------------
// Mouse Scroll
//-----------------------------------------------------------------------------

bool RichText::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)xoffset;
    (void)mods;

    if (!_richText) return false;

    float scrollAmount = yoffset * 40.0f;
    _richText->scroll(-scrollAmount);

    // Clamp scroll
    float maxScroll = std::max(0.0f, _richText->getContentHeight() - static_cast<float>(_pixelHeight));
    if (_richText->getScrollOffset() > maxScroll) {
        _richText->setScrollOffset(maxScroll);
    }

    return true;
}

} // namespace yetty::plugins

//-----------------------------------------------------------------------------
// Plugin exports
//-----------------------------------------------------------------------------

extern "C" {

const char* name() {
    return "rich-text";
}

yetty::Result<yetty::PluginPtr> create() {
    return yetty::plugins::RichTextPlugin::create();
}

}
