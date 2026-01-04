#include "markdown.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <yetty/font-manager.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace yetty {

//-----------------------------------------------------------------------------
// MarkdownPlugin
//-----------------------------------------------------------------------------

MarkdownPlugin::~MarkdownPlugin() { (void)dispose(); }

Result<PluginPtr> MarkdownPlugin::create(YettyPtr engine) noexcept {
    auto p = PluginPtr(new MarkdownPlugin(std::move(engine)));
    if (auto res = static_cast<MarkdownPlugin*>(p.get())->init(); !res) {
        return Err<PluginPtr>("Failed to init MarkdownPlugin", res);
    }
    return Ok(p);
}

Result<void> MarkdownPlugin::init() noexcept {
    // Verify engine has a FontManager
    if (!engine_ || !engine_->fontManager()) {
        return Err<void>("MarkdownPlugin: engine has no FontManager");
    }
    _initialized = true;
    return Ok();
}

Result<void> MarkdownPlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose MarkdownPlugin", res);
    }
    _initialized = false;
    return Ok();
}

FontManager* MarkdownPlugin::getFontManager() {
    return engine_ ? engine_->fontManager().get() : nullptr;
}

Result<PluginLayerPtr> MarkdownPlugin::createLayer(const std::string& payload) {
    auto layer = std::make_shared<MarkdownLayer>(this);
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to init MarkdownLayer", result);
    }
    return Ok<PluginLayerPtr>(layer);
}

//-----------------------------------------------------------------------------
// MarkdownLayer
//-----------------------------------------------------------------------------

MarkdownLayer::MarkdownLayer(MarkdownPlugin* plugin)
    : plugin_(plugin) {}

MarkdownLayer::~MarkdownLayer() { (void)dispose(); }

Result<void> MarkdownLayer::init(const std::string& payload) {
    if (payload.empty()) {
        return Err<void>("MarkdownLayer: empty payload");
    }

    _payload = payload;
    (void)dispose();

    std::string content;

    // Check if payload is inline content or file path
    if (payload.substr(0, 7) == "inline:") {
        content = payload.substr(7);
    } else {
        // Try to load from file
        std::ifstream file(payload);
        if (!file) {
            return Err<void>("Failed to open markdown file: " + payload);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        content = buffer.str();
    }

    parseMarkdown(content);
    std::cout << "MarkdownLayer: parsed " << parsedLines_.size() << " lines" << std::endl;
    return Ok();
}

Result<void> MarkdownLayer::dispose() {
    if (richText_) {
        richText_->dispose();
        richText_.reset();
    }
    initialized_ = false;
    failed_ = false;
    return Ok();
}

//-----------------------------------------------------------------------------
// Markdown Parser
//-----------------------------------------------------------------------------

void MarkdownLayer::parseMarkdown(const std::string& content) {
    parsedLines_.clear();

    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        ParsedLine textLine;

        // Check for headers
        int headerLevel = 0;
        size_t i = 0;
        while (i < line.size() && line[i] == '#') {
            headerLevel++;
            i++;
        }
        if (headerLevel > 0 && i < line.size() && line[i] == ' ') {
            line = line.substr(i + 1);
            textLine.scale = 1.0f + (0.15f * (7 - std::min(headerLevel, 6)));
        }

        // Check for bullet list
        bool isBullet = false;
        if (line.size() >= 2 && (line[0] == '-' || line[0] == '*') && line[1] == ' ') {
            isBullet = true;
            textLine.indent = 20.0f;
            line = line.substr(2);
        }

        // Parse inline styles
        size_t pos = 0;
        while (pos < line.size()) {
            ParsedSpan span;

            // Check for code
            if (line[pos] == '`') {
                size_t end = line.find('`', pos + 1);
                if (end != std::string::npos) {
                    span.text = line.substr(pos + 1, end - pos - 1);
                    span.isCode = true;
                    span.style = Font::Regular;
                    textLine.spans.push_back(span);
                    pos = end + 1;
                    continue;
                }
            }

            // Check for bold+italic (***text***)
            if (pos + 2 < line.size() && line.substr(pos, 3) == "***") {
                size_t end = line.find("***", pos + 3);
                if (end != std::string::npos) {
                    span.text = line.substr(pos + 3, end - pos - 3);
                    span.style = Font::BoldItalic;
                    textLine.spans.push_back(span);
                    pos = end + 3;
                    continue;
                }
            }

            // Check for bold (**text**)
            if (pos + 1 < line.size() && line.substr(pos, 2) == "**") {
                size_t end = line.find("**", pos + 2);
                if (end != std::string::npos) {
                    span.text = line.substr(pos + 2, end - pos - 2);
                    span.style = Font::Bold;
                    textLine.spans.push_back(span);
                    pos = end + 2;
                    continue;
                }
            }

            // Check for italic (*text*)
            if (line[pos] == '*') {
                size_t end = line.find('*', pos + 1);
                if (end != std::string::npos) {
                    span.text = line.substr(pos + 1, end - pos - 1);
                    span.style = Font::Italic;
                    textLine.spans.push_back(span);
                    pos = end + 1;
                    continue;
                }
            }

            // Regular text - find next special character
            size_t next = line.find_first_of("*`", pos);
            if (next == std::string::npos) next = line.size();
            if (next > pos) {
                span.text = line.substr(pos, next - pos);
                span.style = (headerLevel > 0) ? Font::Bold : Font::Regular;
                textLine.spans.push_back(span);
            }
            pos = next;
        }

        // Add bullet if needed
        if (isBullet && !textLine.spans.empty()) {
            ParsedSpan bullet;
            bullet.text = "\xE2\x80\xA2 ";  // Unicode bullet
            bullet.style = Font::Regular;
            bullet.isBullet = true;
            textLine.spans.insert(textLine.spans.begin(), bullet);
        }

        // Add empty span for empty lines (paragraph break)
        if (textLine.spans.empty()) {
            ParsedSpan empty;
            empty.text = "";
            empty.style = Font::Regular;
            textLine.spans.push_back(empty);
        }

        parsedLines_.push_back(textLine);
    }
}

//-----------------------------------------------------------------------------
// Build RichText spans from parsed markdown
//-----------------------------------------------------------------------------

void MarkdownLayer::buildRichTextSpans(float fontSize, float maxWidth) {
    if (!richText_) {
        spdlog::warn("MarkdownLayer::buildRichTextSpans: richText_ is null!");
        return;
    }

    spdlog::debug("MarkdownLayer::buildRichTextSpans: {} lines, fontSize={}, maxWidth={}",
                  parsedLines_.size(), fontSize, maxWidth);

    richText_->clear();

    float cursorY = 0.0f;
    float lineHeight = fontSize * 1.4f;
    int spanCount = 0;

    for (const auto& line : parsedLines_) {
        float cursorX = line.indent;
        float scale = line.scale;
        float scaledLineHeight = lineHeight * scale;
        float scaledSize = fontSize * scale;

        for (const auto& pspan : line.spans) {
            TextSpan span;
            span.text = pspan.text;
            span.x = cursorX;
            span.y = cursorY;
            span.size = scaledSize;
            span.style = pspan.style;
            span.wrap = true;
            span.maxWidth = maxWidth - cursorX;
            span.lineHeight = scaledLineHeight;

            // Set colors based on style
            if (pspan.isCode) {
                span.color = glm::vec4(0.6f, 0.8f, 0.6f, 1.0f);  // Green for code
            } else if (pspan.style == Font::Bold || pspan.style == Font::BoldItalic) {
                span.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);  // Bright white for bold
            } else {
                span.color = glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);  // Slightly dimmer for regular
            }

            richText_->addSpan(span);
            spanCount++;

            // Advance cursor (approximate - RichText will do proper layout)
            // This is just for inline spans on the same line
            cursorX += pspan.text.length() * scaledSize * 0.5f;
        }

        cursorY += scaledLineHeight;
    }

    spdlog::info("MarkdownLayer::buildRichTextSpans: added {} spans", spanCount);
    richText_->setNeedsLayout();
}

//-----------------------------------------------------------------------------
// Render
//-----------------------------------------------------------------------------

Result<void> MarkdownLayer::render(WebGPUContext& ctx) {
    // Legacy render - not used, prefer renderToPass
    if (failed_ || !_visible) return Ok();

    const auto& rc = _render_context;

    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _width_cells * rc.cellWidth;
    float pixelH = _height_cells * rc.cellHeight;

    if (_position_mode == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

    if (rc.termRows > 0) {
        float screenPixelHeight = rc.termRows * rc.cellHeight;
        if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
            return Ok();
        }
    }

    // Create RichText if needed
    if (!richText_) {
        auto fontMgr = plugin_ ? plugin_->getFontManager() : nullptr;
        if (!fontMgr) {
            failed_ = true;
            return Err<void>("No FontManager available for markdown rendering");
        }

        auto result = RichText::create(&ctx, rc.targetFormat, fontMgr);
        if (!result) {
            failed_ = true;
            return Err<void>("Failed to create RichText", result);
        }
        richText_ = *result;
        richText_->setDefaultFontFamily("monospace");
        initialized_ = true;
    }

    if (lastLayoutWidth_ != pixelW) {
        buildRichTextSpans(baseSize_, pixelW);
        lastLayoutWidth_ = pixelW;
    }

    return richText_->render(ctx, rc.targetView, rc.screenWidth, rc.screenHeight,
                              pixelX, pixelY, pixelW, pixelH);
}

bool MarkdownLayer::renderToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    if (failed_ || !_visible) return false;

    const auto& rc = _render_context;

    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _width_cells * rc.cellWidth;
    float pixelH = _height_cells * rc.cellHeight;

    if (_position_mode == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

    if (rc.termRows > 0) {
        float screenPixelHeight = rc.termRows * rc.cellHeight;
        if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
            return false;
        }
    }

    // Create RichText if needed
    if (!richText_) {
        auto fontMgr = plugin_ ? plugin_->getFontManager() : nullptr;
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
        richText_->setDefaultFontFamily("monospace");
        initialized_ = true;
    }

    if (lastLayoutWidth_ != pixelW) {
        buildRichTextSpans(baseSize_, pixelW);
        lastLayoutWidth_ = pixelW;
    }

    // Use batched render
    return richText_->renderToPass(pass, ctx, rc.screenWidth, rc.screenHeight,
                                    pixelX, pixelY, pixelW, pixelH);
}

//-----------------------------------------------------------------------------
// Mouse Scroll
//-----------------------------------------------------------------------------

bool MarkdownLayer::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)xoffset;
    (void)mods;

    if (!richText_) return false;

    float scrollAmount = yoffset * baseSize_ * 3.0f;
    richText_->scroll(-scrollAmount);

    // Clamp scroll
    float maxScroll = std::max(0.0f, richText_->getContentHeight() - static_cast<float>(_pixel_height));
    if (richText_->getScrollOffset() > maxScroll) {
        richText_->setScrollOffset(maxScroll);
    }

    return true;
}

} // namespace yetty

extern "C" {
    const char* name() { return "markdown"; }
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine) { return yetty::MarkdownPlugin::create(std::move(engine)); }
}
