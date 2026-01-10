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
#define MCTX static_cast<fz_context*>(mupdfCtx_)
#define MDOC static_cast<fz_document*>(doc_)

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
    fzCtx_ = mctx;

    // Register document handlers
    fz_try(mctx) { fz_register_document_handlers(mctx); }
    fz_catch(mctx) {
        fz_drop_context(mctx);
        fzCtx_ = nullptr;
        return Err<void>("Failed to register MuPDF document handlers");
    }

    _initialized = true;
    yinfo("PDFPlugin initialized (using RichText for rendering)");
    return Ok();
}

Result<void> PDFPlugin::dispose() {
    if (fzCtx_) {
        fz_drop_context(static_cast<fz_context*>(fzCtx_));
        fzCtx_ = nullptr;
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
    (void)factory;
    (void)loop;
    (void)x;
    (void)y;
    (void)widthCells;
    (void)heightCells;
    (void)pluginArgs;
    _fontManager = fontManager;  // Store for widget use
    yfunc();
    yinfo("payload={}", payload);
    return Pdf::create(payload, this, fzCtx_);
}

//-----------------------------------------------------------------------------
// Pdf
//-----------------------------------------------------------------------------

Pdf::Pdf(const std::string& payload, PDFPlugin* plugin, void* ctx) noexcept
    : plugin_(plugin), mupdfCtx_(ctx) {
    _payload = payload;
}

Pdf::~Pdf() { (void)dispose(); }

Result<WidgetPtr> Pdf::create(const std::string& payload, PDFPlugin* plugin, void* ctx) noexcept {
    auto w = std::shared_ptr<Pdf>(new Pdf(payload, plugin, ctx));
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
    if (doc_) {
        fz_drop_document(MCTX, MDOC);
        doc_ = nullptr;
    }

    if (richText_) {
        richText_->dispose();
        richText_.reset();
    }

    pages_.clear();
    fontNameMap_.clear();
    _initialized = false;
    failed_ = false;
    return Ok();
}

//-----------------------------------------------------------------------------
// PDF Loading
//-----------------------------------------------------------------------------

Result<void> Pdf::loadPDF(const std::string& path) {
    if (!mupdfCtx_) {
        return Err<void>("MuPDF context not initialized");
    }

    fz_try(MCTX) { doc_ = fz_open_document(MCTX, path.c_str()); }
    fz_catch(MCTX) { return Err<void>("Failed to open PDF: " + path); }

    pageCount_ = fz_count_pages(MCTX, MDOC);
    if (pageCount_ <= 0) {
        return Err<void>("PDF has no pages");
    }

    yinfo("Pdf: loaded {} with {} pages", path, pageCount_);

    // Extract first page content
    return extractPageContent(0);
}

//-----------------------------------------------------------------------------
// Font Registration
//-----------------------------------------------------------------------------

std::string Pdf::registerFont(void* fzFont) {
    if (!fzFont) return "";

    // Check if already registered
    auto it = fontNameMap_.find(fzFont);
    if (it != fontNameMap_.end()) {
        return it->second;
    }

    fz_font* font = static_cast<fz_font*>(fzFont);
    std::string fontName = fz_font_name(MCTX, font);

    // Get embedded font data from MuPDF's fz_buffer
    // This avoids using MuPDF's FT_Face directly which has lock callback issues
    if (!font->buffer) {
        ywarn("Pdf: no font buffer for font '{}'", fontName);
        fontNameMap_[fzFont] = "";
        return "";
    }

    unsigned char* fontData = nullptr;
    size_t fontDataLen = fz_buffer_storage(MCTX, font->buffer, &fontData);
    if (!fontData || fontDataLen == 0) {
        ywarn("Pdf: no font data for font '{}'", fontName);
        fontNameMap_[fzFont] = "";
        return "";
    }

    // Store the font name and data for later atlas generation
    fontNameMap_[fzFont] = fontName;
    PendingFont pf;
    pf.data.assign(fontData, fontData + fontDataLen);
    pf.name = fontName;
    pendingFonts_[fzFont] = std::move(pf);

    ydebug("Pdf: collected font '{}' ({} bytes)", fontName, fontDataLen);
    return fontName;
}

Result<void> Pdf::generateFontAtlases() {
    auto fontMgr = plugin_->getFontManager();
    if (!fontMgr) {
        return Err<void>("No FontManager available");
    }

    for (auto& [fzFont, pendingFont] : pendingFonts_) {
        yinfo("Pdf: generating atlas for font '{}'", pendingFont.name);
        auto result = fontMgr->getFont(pendingFont.data.data(), pendingFont.data.size(),
                                        pendingFont.name, 32.0f);
        if (!result || !*result) {
            ywarn("Pdf: failed to generate atlas for font '{}': {}", pendingFont.name,
                         result ? "null font" : result.error().message());
            // Mark as failed - will use fallback
            fontNameMap_[fzFont] = "";
        }
    }

    pendingFonts_.clear();
    return Ok();
}

//-----------------------------------------------------------------------------
// Page Content Extraction
//-----------------------------------------------------------------------------

Result<void> Pdf::extractPageContent(int pageNum) {
    if (!doc_ || pageNum < 0 || pageNum >= pageCount_) {
        return Err<void>("Invalid page number");
    }

    currentPage_ = pageNum;
    pages_.clear();

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

        pages_.push_back(std::move(pdfPage));
        yinfo("Pdf: extracted {} characters from page {}",
                     pages_[0].chars.size(), pageNum);

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
    lastViewWidth_ = 0;
    lastViewHeight_ = 0;
    if (richText_) {
        richText_->clear();
    }

    return Ok();
}

//-----------------------------------------------------------------------------
// Build RichText Content
//-----------------------------------------------------------------------------

void Pdf::buildRichTextContent(float viewWidth) {
    if (!richText_ || pages_.empty()) return;

    richText_->clear();

    const auto& page = pages_[0];
    float pdfWidth = page.width;
    float pdfHeight = page.height;

    // Calculate scale to fit view width
    float scale = viewWidth / pdfWidth * zoom_;
    documentHeight_ = pdfHeight * scale;

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

        richText_->addChar(textChar);
    }

    richText_->setNeedsLayout();
    ydebug("Pdf: built RichText content with {} chars", page.chars.size());
}

//-----------------------------------------------------------------------------
// Render
//-----------------------------------------------------------------------------

Result<void> Pdf::render(WebGPUContext& ctx) {
    if (failed_) return Err<void>("Pdf already failed");
    if (!_visible) return Ok();

    // Get render context set by owner
    const auto& rc = _renderCtx;

    // Calculate pixel position from cell position
    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _widthCells * rc.cellWidth;
    float pixelH = _heightCells * rc.cellHeight;

    // For Relative layers, adjust position when viewing scrollback
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

    // Create RichText if needed
    if (!richText_) {
        auto fontMgr = plugin_->getFontManager();
        if (!fontMgr) {
            failed_ = true;
            return Err<void>("No FontManager available for PDF rendering");
        }

        auto result = RichText::create(&ctx, rc.targetFormat, fontMgr);
        if (!result) {
            failed_ = true;
            return Err<void>("Failed to create RichText", result);
        }
        richText_ = *result;

        // Pre-load a fallback font NOW (before rendering) to avoid MSDF generation during render loop
        // This is blocking but happens only once during initialization
        yinfo("Pdf: pre-loading fallback font...");
        richText_->setDefaultFontFamily("monospace");

        // Force the font to actually load now by requesting it from FontManager
        auto preloadResult = fontMgr->getFont("monospace", Font::Regular);
        if (!preloadResult || !*preloadResult) {
            // Try sans-serif as fallback
            preloadResult = fontMgr->getFont("sans-serif", Font::Regular);
        }
        if (preloadResult && *preloadResult) {
            yinfo("Pdf: fallback font pre-loaded successfully");
        } else {
            ywarn("Pdf: could not pre-load fallback font");
        }

        _initialized = true;
    }

    // Re-layout if view size changed
    if (lastViewWidth_ != pixelW || lastViewHeight_ != pixelH) {
        buildRichTextContent(pixelW);
        lastViewWidth_ = pixelW;
        lastViewHeight_ = pixelH;
    }

    // Apply scroll offset to RichText
    richText_->setScrollOffset(scrollOffset_);

    // Render
    return richText_->render(ctx, rc.targetView, rc.screenWidth, rc.screenHeight,
                              pixelX, pixelY, pixelW, pixelH);
}

bool Pdf::render(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    (void)pass;
    (void)ctx;
    // PDF widget doesn't render to pass directly - uses its own render method
    return true;
}

//-----------------------------------------------------------------------------
// Mouse Scroll
//-----------------------------------------------------------------------------

bool Pdf::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)xoffset;

    // Ctrl + scroll = zoom
    if (mods & 0x0002) {  // GLFW_MOD_CONTROL
        float zoomFactor = (yoffset > 0) ? 1.1f : 0.9f;
        zoom_ *= zoomFactor;
        zoom_ = std::clamp(zoom_, 0.1f, 10.0f);
        // Force re-layout
        lastViewWidth_ = 0;
        lastViewHeight_ = 0;
        ydebug("Pdf: zoom changed to {}", zoom_);
        return true;
    }

    // Regular scroll
    float scrollAmount = yoffset * 40.0f;
    scrollOffset_ -= scrollAmount;

    // Clamp scroll
    float maxScroll = std::max(0.0f, documentHeight_ - static_cast<float>(_pixelHeight));
    scrollOffset_ = std::clamp(scrollOffset_, 0.0f, maxScroll);

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
        if (currentPage_ > 0) {
            (void)extractPageContent(currentPage_ - 1);
            scrollOffset_ = 0;
            return true;
        }
    } else if (key == 267) {  // GLFW_KEY_PAGE_DOWN
        if (currentPage_ < pageCount_ - 1) {
            (void)extractPageContent(currentPage_ + 1);
            scrollOffset_ = 0;
            return true;
        }
    }

    // Zoom with +/-
    if (key == 61 || key == 334) {  // = or numpad +
        zoom_ *= 1.1f;
        lastViewWidth_ = 0;
        return true;
    } else if (key == 45 || key == 333) {  // - or numpad -
        zoom_ *= 0.9f;
        zoom_ = std::max(0.1f, zoom_);
        lastViewWidth_ = 0;
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
