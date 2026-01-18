#include "pdf.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <yetty/font-manager.h>
#include <ytrace/ytrace.hpp>
#include <algorithm>
#include <cmath>
#include <ytrace/ytrace.hpp>

// FreeType for font access
#include <ft2build.h>
#include FT_FREETYPE_H

// MuPDF headers
extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

// Helper macros for accessing typed MuPDF pointers from void*
#define MCTX static_cast<fz_context*>(_mupdfCtx)
#define MDOC static_cast<fz_document*>(_doc)

namespace yetty {

//-----------------------------------------------------------------------------
// PDFPlugin
//-----------------------------------------------------------------------------

PDFPlugin::~PDFPlugin() { (void)dispose(); }

Result<PluginPtr> PDFPlugin::create() noexcept {
    auto p = PluginPtr(new PDFPlugin());
    if (auto res = static_cast<PDFPlugin*>(p.get())->pluginInit(); !res) {
        return Err<PluginPtr>("Failed to init PDFPlugin", res);
    }
    return Ok(p);
}

Result<void> PDFPlugin::pluginInit() noexcept {
    // Create MuPDF context
    fz_context* mctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!mctx) {
        return Err<void>("Failed to create MuPDF context");
    }
    _fzCtx = mctx;

    // Register document handlers
    fz_try(mctx) { fz_register_document_handlers(mctx); }
    fz_catch(mctx) {
        fz_drop_context(mctx);
        _fzCtx = nullptr;
        return Err<void>("Failed to register MuPDF document handlers");
    }

    _initialized = true;
    yinfo("PDFPlugin initialized (using RichText for rendering)");
    return Ok();
}

Result<void> PDFPlugin::dispose() {
    if (_fzCtx) {
        fz_drop_context(static_cast<fz_context*>(_fzCtx));
        _fzCtx = nullptr;
    }

    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose PDFPlugin", res);
    }
    _initialized = false;
    return Ok();
}

FontManager* PDFPlugin::getFontManager() {
    return _fontManager;
}

Result<WidgetPtr> PDFPlugin::createWidget(
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
    yinfo("payload={} x={} y={} w={} h={}", payload, x, y, widthCells, heightCells);
    return Pdf::create(factory, fontManager, loop, x, y, widthCells, heightCells, pluginArgs, payload, this, _fzCtx);
}

//-----------------------------------------------------------------------------
// Pdf
//-----------------------------------------------------------------------------

Pdf::Pdf(const std::string& payload, PDFPlugin* plugin, void* ctx) noexcept
    : _plugin(plugin), _mupdfCtx(ctx) {
    _payload = payload;
}

Pdf::~Pdf() { (void)dispose(); }

Result<WidgetPtr> Pdf::create(
    WidgetFactory* factory,
    FontManager* fontManager,
    uv_loop_t* loop,
    int32_t x,
    int32_t y,
    uint32_t widthCells,
    uint32_t heightCells,
    const std::string& pluginArgs,
    const std::string& payload,
    PDFPlugin* plugin,
    void* ctx
) noexcept {
    (void)factory;
    (void)fontManager;
    (void)loop;
    (void)pluginArgs;
    auto w = std::shared_ptr<Pdf>(new Pdf(payload, plugin, ctx));
    w->_x = x;
    w->_y = y;
    w->_widthCells = widthCells;
    w->_heightCells = heightCells;
    if (auto res = w->init(); !res) {
        return Err<WidgetPtr>("Failed to init Pdf", res);
    }
    return Ok(std::static_pointer_cast<Widget>(w));
}

Result<void> Pdf::init() {
    if (_payload.empty()) {
        return Err<void>("Pdf: empty payload");
    }

    (void)dispose();

    return loadPDF(_payload);
}

Result<void> Pdf::dispose() {
    if (_doc) {
        fz_drop_document(MCTX, MDOC);
        _doc = nullptr;
    }

    if (_richText) {
        _richText->dispose();
        _richText.reset();
    }

    _pages.clear();
    _fontNameMap.clear();
    _initialized = false;
    _failed = false;
    return Ok();
}

//-----------------------------------------------------------------------------
// PDF Loading
//-----------------------------------------------------------------------------

Result<void> Pdf::loadPDF(const std::string& path) {
    if (!_mupdfCtx) {
        return Err<void>("MuPDF context not initialized");
    }

    fz_try(MCTX) { _doc = fz_open_document(MCTX, path.c_str()); }
    fz_catch(MCTX) { return Err<void>("Failed to open PDF: " + path); }

    _pageCount = fz_count_pages(MCTX, MDOC);
    if (_pageCount <= 0) {
        return Err<void>("PDF has no pages");
    }

    yinfo("Pdf: loaded {} with {} pages", path, _pageCount);

    // Extract first page content
    return extractPageContent(0);
}

//-----------------------------------------------------------------------------
// Font Registration
//-----------------------------------------------------------------------------

std::string Pdf::registerFont(void* fzFont) {
    if (!fzFont) return "";

    // Check if already registered
    auto it = _fontNameMap.find(fzFont);
    if (it != _fontNameMap.end()) {
        return it->second;
    }

    fz_font* font = static_cast<fz_font*>(fzFont);
    std::string fontName = fz_font_name(MCTX, font);

    // Get embedded font data from MuPDF's fz_buffer
    // This avoids using MuPDF's FT_Face directly which has lock callback issues
    if (!font->buffer) {
        ywarn("Pdf: no font buffer for font '{}'", fontName);
        _fontNameMap[fzFont] = "";
        return "";
    }

    unsigned char* fontData = nullptr;
    size_t fontDataLen = fz_buffer_storage(MCTX, font->buffer, &fontData);
    if (!fontData || fontDataLen == 0) {
        ywarn("Pdf: no font data for font '{}'", fontName);
        _fontNameMap[fzFont] = "";
        return "";
    }

    // Store the font name and data for later atlas generation
    _fontNameMap[fzFont] = fontName;
    PendingFont pf;
    pf.data.assign(fontData, fontData + fontDataLen);
    pf.name = fontName;
    _pendingFonts[fzFont] = std::move(pf);

    ydebug("Pdf: collected font '{}' ({} bytes)", fontName, fontDataLen);
    return fontName;
}

Result<void> Pdf::generateFontAtlases() {
    auto fontMgr = _plugin->getFontManager();
    if (!fontMgr) {
        return Err<void>("No FontManager available");
    }

    for (auto& [fzFont, pendingFont] : _pendingFonts) {
        yinfo("Pdf: generating atlas for font '{}'", pendingFont.name);
        auto result = fontMgr->getFont(pendingFont.data.data(), pendingFont.data.size(),
                                        pendingFont.name, 32.0f);
        if (!result || !*result) {
            ywarn("Pdf: failed to generate atlas for font '{}': {}", pendingFont.name,
                         result ? "null font" : result.error().message());
            // Mark as failed - will use fallback
            _fontNameMap[fzFont] = "";
        }
    }

    _pendingFonts.clear();
    return Ok();
}

//-----------------------------------------------------------------------------
// Page Content Extraction
//-----------------------------------------------------------------------------

Result<void> Pdf::extractPageContent(int pageNum) {
    if (!_doc || pageNum < 0 || pageNum >= _pageCount) {
        return Err<void>("Invalid page number");
    }

    _currentPage = pageNum;
    _pages.clear();

    fz_page* page = nullptr;
    fz_stext_page* textPage = nullptr;

    fz_try(MCTX) {
        page = fz_load_page(MCTX, MDOC, pageNum);
        fz_rect bounds = fz_bound_page(MCTX, page);

        ExtractedPage pdfPage;
        pdfPage.width = bounds.x1 - bounds.x0;
        pdfPage.height = bounds.y1 - bounds.y0;

        // Extract text using structured text
        fz_stext_options opts = {0};
        textPage = fz_new_stext_page_from_page(MCTX, page, &opts);

        for (fz_stext_block* block = textPage->first_block; block; block = block->next) {
            if (block->type != FZ_STEXT_BLOCK_TEXT) continue;

            for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
                for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
                    ExtractedChar textChar;
                    textChar.codepoint = ch->c;
                    textChar.x = ch->origin.x;
                    textChar.y = ch->origin.y;
                    textChar.size = ch->size;
                    textChar.color = 0xFF000000;  // Default black

                    // Register font and get family name
                    if (ch->font) {
                        textChar.fontFamily = registerFont(ch->font);
                        textChar.bold = fz_font_is_bold(MCTX, ch->font);
                        textChar.italic = fz_font_is_italic(MCTX, ch->font);
                    }

                    pdfPage.chars.push_back(textChar);
                }
            }
        }

        _pages.push_back(std::move(pdfPage));
        yinfo("Pdf: extracted {} characters from page {}",
                     _pages[0].chars.size(), pageNum);

        // Generate font atlases AFTER extraction but still inside fz_try
        // This matches pdf-old's approach
        if (auto res = generateFontAtlases(); !res) {
            ywarn("Pdf: font atlas generation failed: {}", res.error().message());
        }
    }
    fz_always(MCTX) {
        if (textPage) fz_drop_stext_page(MCTX, textPage);
        if (page) fz_drop_page(MCTX, page);
    }
    fz_catch(MCTX) { return Err<void>("Failed to extract page content"); }

    // Force re-layout
    _lastViewWidth = 0;
    _lastViewHeight = 0;
    if (_richText) {
        _richText->clear();
    }

    return Ok();
}

//-----------------------------------------------------------------------------
// Build RichText Content
//-----------------------------------------------------------------------------

void Pdf::buildRichTextContent(float viewWidth) {
    if (!_richText || _pages.empty()) return;

    _richText->clear();

    const auto& page = _pages[0];
    float pdfWidth = page.width;
    float pdfHeight = page.height;

    // Calculate scale to fit view width
    float scale = viewWidth / pdfWidth * _zoom;
    _documentHeight = pdfHeight * scale;

    for (const auto& ch : page.chars) {
        if (ch.codepoint == '\n' || ch.codepoint == '\r') continue;

        // Convert PDF coordinates to screen coordinates
        // PDF has Y-up (origin at bottom-left), screen has Y-down (origin at top-left)
        float screenX = ch.x * scale;
        float screenY = (pdfHeight - ch.y) * scale;  // Flip Y axis
        float fontSize = ch.size * scale;

        // Color conversion (ARGB to RGBA)
        float r = ((ch.color >> 16) & 0xFF) / 255.0f;
        float g = ((ch.color >> 8) & 0xFF) / 255.0f;
        float b = (ch.color & 0xFF) / 255.0f;
        float a = ((ch.color >> 24) & 0xFF) / 255.0f;

        // Default to white text for dark theme
        if (r < 0.1f && g < 0.1f && b < 0.1f) {
            r = g = b = 0.9f;
            a = 1.0f;
        }

        // Create TextChar for RichText
        TextChar textChar;
        textChar.codepoint = ch.codepoint;
        textChar.x = screenX;
        textChar.y = screenY;
        textChar.size = fontSize;
        textChar.color = glm::vec4(r, g, b, a);
        textChar.fontFamily = ch.fontFamily;

        // Determine style
        if (ch.bold && ch.italic) {
            textChar.style = Font::BoldItalic;
        } else if (ch.bold) {
            textChar.style = Font::Bold;
        } else if (ch.italic) {
            textChar.style = Font::Italic;
        } else {
            textChar.style = Font::Regular;
        }

        _richText->addChar(textChar);
    }

    _richText->setNeedsLayout();
    ydebug("Pdf: built RichText content with {} chars", page.chars.size());
}

//-----------------------------------------------------------------------------
// Render
//-----------------------------------------------------------------------------

void Pdf::prepareFrame(WebGPUContext& ctx, bool on) {
    (void)ctx;
    (void)on;
    // PDF renders directly to render pass - no texture preparation needed
}

Result<void> Pdf::render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on) {
    if (!on || _failed || !_visible) return Ok();

    // Create RichText if needed
    if (!_richText) {
        auto fontMgr = _plugin->getFontManager();
        if (!fontMgr) {
            _failed = true;
            return Err<void>("Pdf: no font manager");
        }

        auto result = RichText::create(&ctx, ctx.getSurfaceFormat(), fontMgr);
        if (!result) {
            _failed = true;
            return Err<void>("Pdf: failed to create RichText", result);
        }
        _richText = *result;
        _richText->setDefaultFontFamily("monospace");

        auto preloadResult = fontMgr->getFont("monospace", Font::Regular);
        if (!preloadResult || !*preloadResult) {
            preloadResult = fontMgr->getFont("sans-serif", Font::Regular);
        }
        _initialized = true;
    }

    // Re-layout if view size changed
    if (_lastViewWidth != _pixelWidth || _lastViewHeight != _pixelHeight) {
        buildRichTextContent(static_cast<float>(_pixelWidth));
        _lastViewWidth = _pixelWidth;
        _lastViewHeight = _pixelHeight;
    }

    _richText->setScrollOffset(_scrollOffset);

    return _richText->render(pass, ctx, ctx.getSurfaceWidth(), ctx.getSurfaceHeight(),
                             _pixelX, _pixelY, static_cast<float>(_pixelWidth), static_cast<float>(_pixelHeight));
}

//-----------------------------------------------------------------------------
// Mouse Scroll
//-----------------------------------------------------------------------------

bool Pdf::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)xoffset;

    // Ctrl + scroll = zoom
    if (mods & 0x0002) {  // GLFW_MOD_CONTROL
        float zoomFactor = (yoffset > 0) ? 1.1f : 0.9f;
        _zoom *= zoomFactor;
        _zoom = std::clamp(_zoom, 0.1f, 10.0f);
        // Force re-layout
        _lastViewWidth = 0;
        _lastViewHeight = 0;
        ydebug("Pdf: zoom changed to {}", _zoom);
        return true;
    }

    // Regular scroll
    float scrollAmount = yoffset * 40.0f;
    _scrollOffset -= scrollAmount;

    // Clamp scroll
    float maxScroll = std::max(0.0f, _documentHeight - static_cast<float>(_pixelHeight));
    _scrollOffset = std::clamp(_scrollOffset, 0.0f, maxScroll);

    return true;
}

//-----------------------------------------------------------------------------
// Keyboard (Page Navigation)
//-----------------------------------------------------------------------------

bool Pdf::onKey(int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;

    if (action != 1) return false;  // GLFW_PRESS

    // Page Up/Down for navigation
    if (key == 266) {  // GLFW_KEY_PAGE_UP
        if (_currentPage > 0) {
            (void)extractPageContent(_currentPage - 1);
            _scrollOffset = 0;
            return true;
        }
    } else if (key == 267) {  // GLFW_KEY_PAGE_DOWN
        if (_currentPage < _pageCount - 1) {
            (void)extractPageContent(_currentPage + 1);
            _scrollOffset = 0;
            return true;
        }
    }

    // Zoom with +/-
    if (key == 61 || key == 334) {  // = or numpad +
        _zoom *= 1.1f;
        _lastViewWidth = 0;
        return true;
    } else if (key == 45 || key == 333) {  // - or numpad -
        _zoom *= 0.9f;
        _zoom = std::max(0.1f, _zoom);
        _lastViewWidth = 0;
        return true;
    }

    return false;
}

} // namespace yetty

extern "C" {
const char* name() { return "pdf"; }
yetty::Result<yetty::PluginPtr> create() {
    return yetty::PDFPlugin::create();
}
}
