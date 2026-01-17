#include "markdown.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <yetty/font-manager.h>
#include <ytrace/ytrace.hpp>
#include <ytrace/ytrace.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace yetty::plugins {

//-----------------------------------------------------------------------------
// MarkdownPlugin
//-----------------------------------------------------------------------------

MarkdownPlugin::~MarkdownPlugin() { (void)dispose(); }

Result<PluginPtr> MarkdownPlugin::create() noexcept {
    auto p = PluginPtr(new MarkdownPlugin());
    if (auto res = static_cast<MarkdownPlugin*>(p.get())->pluginInit(); !res) {
        return Err<PluginPtr>("Failed to init MarkdownPlugin", res);
    }
    return Ok(p);
}

Result<void> MarkdownPlugin::pluginInit() noexcept {
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
    return _fontManager;
}

Result<WidgetPtr> MarkdownPlugin::createWidget(
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
    return Markdown::create(factory, fontManager, loop, x, y, widthCells, heightCells, pluginArgs, payload, this);
}

//-----------------------------------------------------------------------------
// Markdown
//-----------------------------------------------------------------------------

Markdown::~Markdown() { (void)dispose(); }

Result<void> Markdown::init() {
    if (_payload.empty()) {
        return Err<void>("Markdown: empty payload");
    }

    (void)dispose();

    std::string content;

    // Check if payload is inline content or file path
    if (_payload.substr(0, 7) == "inline:") {
        content = _payload.substr(7);
    } else {
        // Try to load from file
        std::ifstream file(_payload);
        if (!file) {
            return Err<void>("Failed to open markdown file: " + _payload);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        content = buffer.str();
    }

    parseMarkdown(content);
    std::cout << "Markdown: parsed " << _parsedLines.size() << " lines" << std::endl;
    return Ok();
}

Result<void> Markdown::dispose() {
    if (_richText) {
        _richText->dispose();
        _richText.reset();
    }
    _initialized = false;
    _failed = false;
    return Ok();
}

//-----------------------------------------------------------------------------
// Markdown Parser
//-----------------------------------------------------------------------------

void Markdown::parseMarkdown(const std::string& content) {
    _parsedLines.clear();

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
                pos = next;
            } else {
                // Unmatched special char - treat as literal and skip
                span.text = std::string(1, line[pos]);
                span.style = Font::Regular;
                textLine.spans.push_back(span);
                pos++;
            }
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

        _parsedLines.push_back(textLine);
    }
}

//-----------------------------------------------------------------------------
// Build RichText spans from parsed markdown
//-----------------------------------------------------------------------------

void Markdown::buildRichTextSpans(float fontSize, float maxWidth) {
    if (!_richText) {
        ywarn("Markdown::buildRichTextSpans: _richText is null!");
        return;
    }

    ydebug("Markdown::buildRichTextSpans: {} lines, fontSize={}, maxWidth={}",
                  _parsedLines.size(), fontSize, maxWidth);

    _richText->clear();

    float cursorY = 0.0f;
    float lineHeight = fontSize * 1.4f;
    int spanCount = 0;

    for (const auto& line : _parsedLines) {
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

            _richText->addSpan(span);
            spanCount++;

            // Advance cursor (approximate - RichText will do proper layout)
            // This is just for inline spans on the same line
            cursorX += pspan.text.length() * scaledSize * 0.5f;
        }

        cursorY += scaledLineHeight;
    }

    yinfo("Markdown::buildRichTextSpans: added {} spans", spanCount);
    _richText->setNeedsLayout();
}

//-----------------------------------------------------------------------------
// Render
//-----------------------------------------------------------------------------

void Markdown::prepareFrame(WebGPUContext& ctx, bool on) {
    (void)ctx;
    (void)on;
    // Markdown renders directly to render pass - no texture preparation needed
}

Result<void> Markdown::render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on) {
    yinfo("Markdown::render called, on={} failed={} visible={} richText={}", on, _failed, _visible, _richText != nullptr);
    if (!on || _failed || !_visible) return Ok();

    const auto& rc = _renderCtx;

    // Use pre-computed pixel positions from Widget base class
    float pixelX = _pixelX;
    float pixelY = _pixelY;
    float pixelW = static_cast<float>(_pixelWidth);
    float pixelH = static_cast<float>(_pixelHeight);

    // Skip if off-screen
    if (rc.termRows > 0) {
        float screenPixelHeight = rc.termRows * rc.cellHeight;
        if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
            return Ok();  // Off-screen, not an error
        }
    }

    // Create RichText if needed
    if (!_richText) {
        auto fontMgr = _plugin ? _plugin->getFontManager() : nullptr;
        if (!fontMgr) {
            _failed = true;
            return Err<void>("Markdown: no font manager");
        }

        auto result = yetty::RichText::create(&ctx, rc.targetFormat, fontMgr);
        if (!result) {
            _failed = true;
            return Err<void>("Markdown: failed to create RichText", result);
        }
        _richText = *result;
        _richText->setDefaultFontFamily("monospace");
        _initialized = true;
    }

    if (_lastLayoutWidth != pixelW) {
        buildRichTextSpans(_baseSize, pixelW);
        _lastLayoutWidth = pixelW;
    }

    // Use batched render
    return _richText->render(pass, ctx, rc.screenWidth, rc.screenHeight,
                                    pixelX, pixelY, pixelW, pixelH);
}

//-----------------------------------------------------------------------------
// Mouse Scroll
//-----------------------------------------------------------------------------

bool Markdown::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)xoffset;
    (void)mods;

    if (!_richText) return false;

    float scrollAmount = yoffset * _baseSize * 3.0f;
    _richText->scroll(-scrollAmount);

    // Clamp scroll
    float maxScroll = std::max(0.0f, _richText->getContentHeight() - static_cast<float>(_pixelHeight));
    if (_richText->getScrollOffset() > maxScroll) {
        _richText->setScrollOffset(maxScroll);
    }

    return true;
}

} // namespace yetty::plugins

extern "C" {
    const char* name() { return "markdown"; }
    yetty::Result<yetty::PluginPtr> create() { return yetty::plugins::MarkdownPlugin::create(); }
}
