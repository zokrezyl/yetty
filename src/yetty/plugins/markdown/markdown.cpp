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

namespace yetty {

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
    return MarkdownW::create(payload, this);
}

//-----------------------------------------------------------------------------
// MarkdownW
//-----------------------------------------------------------------------------

MarkdownW::~MarkdownW() { (void)dispose(); }

Result<void> MarkdownW::init() {
    if (_payload.empty()) {
        return Err<void>("MarkdownW: empty payload");
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
    std::cout << "MarkdownW: parsed " << parsedLines_.size() << " lines" << std::endl;
    return Ok();
}

Result<void> MarkdownW::dispose() {
    if (richText_) {
        richText_->dispose();
        richText_.reset();
    }
    _initialized = false;
    failed_ = false;
    return Ok();
}

//-----------------------------------------------------------------------------
// Markdown Parser
//-----------------------------------------------------------------------------

void MarkdownW::parseMarkdown(const std::string& content) {
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

void MarkdownW::buildRichTextSpans(float fontSize, float maxWidth) {
    if (!richText_) {
        ywarn("MarkdownW::buildRichTextSpans: richText_ is null!");
        return;
    }

    ydebug("MarkdownW::buildRichTextSpans: {} lines, fontSize={}, maxWidth={}",
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

    yinfo("MarkdownW::buildRichTextSpans: added {} spans", spanCount);
    richText_->setNeedsLayout();
}

//-----------------------------------------------------------------------------
// Render
//-----------------------------------------------------------------------------

Result<void> MarkdownW::render(WebGPUContext& ctx) {
    // Legacy render - not used, prefer render
    if (failed_ || !_visible) return Ok();

    const auto& rc = _renderCtx;

    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _widthCells * rc.cellWidth;
    float pixelH = _heightCells * rc.cellHeight;

    if (_positionMode == PositionMode::Relative && rc.scrollOffset > 0) {
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
        _initialized = true;
    }

    if (lastLayoutWidth_ != pixelW) {
        buildRichTextSpans(baseSize_, pixelW);
        lastLayoutWidth_ = pixelW;
    }

    return richText_->render(ctx, rc.targetView, rc.screenWidth, rc.screenHeight,
                              pixelX, pixelY, pixelW, pixelH);
}

bool MarkdownW::render(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    if (failed_ || !_visible) return false;

    const auto& rc = _renderCtx;

    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _widthCells * rc.cellWidth;
    float pixelH = _heightCells * rc.cellHeight;

    if (_positionMode == PositionMode::Relative && rc.scrollOffset > 0) {
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
        _initialized = true;
    }

    if (lastLayoutWidth_ != pixelW) {
        buildRichTextSpans(baseSize_, pixelW);
        lastLayoutWidth_ = pixelW;
    }

    // Use batched render
    return richText_->render(pass, ctx, rc.screenWidth, rc.screenHeight,
                                    pixelX, pixelY, pixelW, pixelH);
}

//-----------------------------------------------------------------------------
// Mouse Scroll
//-----------------------------------------------------------------------------

bool MarkdownW::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)xoffset;
    (void)mods;

    if (!richText_) return false;

    float scrollAmount = yoffset * baseSize_ * 3.0f;
    richText_->scroll(-scrollAmount);

    // Clamp scroll
    float maxScroll = std::max(0.0f, richText_->getContentHeight() - static_cast<float>(_pixelHeight));
    if (richText_->getScrollOffset() > maxScroll) {
        richText_->setScrollOffset(maxScroll);
    }

    return true;
}

} // namespace yetty

extern "C" {
    const char* name() { return "markdown"; }
    yetty::Result<yetty::PluginPtr> create() { return yetty::MarkdownPlugin::create(); }
}
