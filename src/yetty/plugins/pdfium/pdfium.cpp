#include "pdfium.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <yetty/font-manager.h>
#include <ytrace/ytrace.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

// PDFium headers
#include <fpdfview.h>
#include <fpdf_text.h>
#include <fpdf_edit.h>

namespace yetty {

//-----------------------------------------------------------------------------
// PdfiumPlugin
//-----------------------------------------------------------------------------

PdfiumPlugin::~PdfiumPlugin() { (void)dispose(); }

Result<PluginPtr> PdfiumPlugin::create() noexcept {
    auto p = PluginPtr(new PdfiumPlugin());
    if (auto res = static_cast<PdfiumPlugin*>(p.get())->pluginInit(); !res) {
        return Err<PluginPtr>("Failed to init PdfiumPlugin", res);
    }
    return Ok(p);
}

Result<void> PdfiumPlugin::pluginInit() noexcept {
    // Initialize PDFium library
    FPDF_LIBRARY_CONFIG config;
    std::memset(&config, 0, sizeof(config));
    config.version = 2;
    config.m_pUserFontPaths = nullptr;
    config.m_pIsolate = nullptr;
    config.m_v8EmbedderSlot = 0;

    FPDF_InitLibraryWithConfig(&config);
    _libraryInitialized = true;

    _initialized = true;
    yinfo("PdfiumPlugin initialized (using RichText for rendering)");
    return Ok();
}

Result<void> PdfiumPlugin::dispose() {
    if (_libraryInitialized) {
        FPDF_DestroyLibrary();
        _libraryInitialized = false;
    }

    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose PdfiumPlugin", res);
    }
    _initialized = false;
    return Ok();
}

Result<WidgetPtr> PdfiumPlugin::createWidget(
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
    return PdfiumWidget::create(factory, fontManager, loop, x, y, widthCells, heightCells, pluginArgs, payload, this);
}

//-----------------------------------------------------------------------------
// PdfiumWidget
//-----------------------------------------------------------------------------

PdfiumWidget::PdfiumWidget(const std::string& payload, PdfiumPlugin* plugin) noexcept
    : _plugin(plugin) {
    _payload = payload;
}

PdfiumWidget::~PdfiumWidget() { (void)dispose(); }

Result<WidgetPtr> PdfiumWidget::create(
    WidgetFactory* factory,
    FontManager* fontManager,
    uv_loop_t* loop,
    int32_t x,
    int32_t y,
    uint32_t widthCells,
    uint32_t heightCells,
    const std::string& pluginArgs,
    const std::string& payload,
    PdfiumPlugin* plugin
) noexcept {
    (void)factory;
    (void)fontManager;
    (void)loop;
    (void)pluginArgs;
    auto w = std::shared_ptr<PdfiumWidget>(new PdfiumWidget(payload, plugin));
    w->_x = x;
    w->_y = y;
    w->_widthCells = widthCells;
    w->_heightCells = heightCells;
    if (auto res = w->init(); !res) {
        return Err<WidgetPtr>("Failed to init PdfiumWidget", res);
    }
    return Ok(std::static_pointer_cast<Widget>(w));
}

Result<void> PdfiumWidget::init() {
    if (_payload.empty()) {
        return Err<void>("PdfiumWidget: empty payload");
    }

    (void)dispose();

    return loadPDF(_payload);
}

Result<void> PdfiumWidget::dispose() {
    if (_doc) {
        FPDF_CloseDocument(static_cast<FPDF_DOCUMENT>(_doc));
        _doc = nullptr;
    }

    if (_richText) {
        _richText->dispose();
        _richText.reset();
    }

    _pages.clear();
    _fontNameMap.clear();
    _fontInfoToFamily.clear();
    _processedFonts.clear();
    _pendingFonts.clear();
    _initialized = false;
    _failed = false;
    return Ok();
}

//-----------------------------------------------------------------------------
// PDF Loading
//-----------------------------------------------------------------------------

Result<void> PdfiumWidget::loadPDF(const std::string& path) {
    _doc = FPDF_LoadDocument(path.c_str(), nullptr);
    if (!_doc) {
        unsigned long error = FPDF_GetLastError();
        return Err<void>("Failed to open PDF: " + path + " (error: " + std::to_string(error) + ")");
    }

    _pageCount = FPDF_GetPageCount(static_cast<FPDF_DOCUMENT>(_doc));
    if (_pageCount <= 0) {
        return Err<void>("PDF has no pages");
    }

    yinfo("PdfiumWidget: loaded {} with {} pages", path, _pageCount);

    // Extract first page content
    return extractPageContent(0);
}

//-----------------------------------------------------------------------------
// Font Registration - extract embedded font data and register with FontManager
//-----------------------------------------------------------------------------

std::string PdfiumWidget::registerFont(FPDF_FONT font) {
    if (!font) return "";

    // Check if we already processed this font
    auto it = _fontNameMap.find(font);
    if (it != _fontNameMap.end()) {
        return it->second;
    }

    // Get base font name (this is what FPDFText_GetFontInfo returns, and more reliable)
    char baseFontName[256] = {0};
    size_t baseLen = FPDFFont_GetBaseFontName(font, baseFontName, sizeof(baseFontName));
    std::string baseName = (baseLen > 0 && baseFontName[0] != '\0') ? baseFontName : "";

    // Get font family name (often empty for embedded fonts)
    char familyName[256] = {0};
    size_t familyLen = FPDFFont_GetFamilyName(font, familyName, sizeof(familyName));

    // Use base name if family name is empty (more common for embedded PDF fonts)
    std::string fontFamily = (familyLen > 0 && familyName[0] != '\0') ? familyName : baseName;
    if (fontFamily.empty()) {
        fontFamily = "PDFFont_" + std::to_string(reinterpret_cast<uintptr_t>(font));
    }

    yinfo("PdfiumWidget: font family='{}' baseName='{}'", fontFamily, baseName);

    // Check if font is embedded
    int isEmbedded = FPDFFont_GetIsEmbedded(font);
    if (!isEmbedded) {
        yinfo("PdfiumWidget: font '{}' is not embedded, will use system fallback", fontFamily);
        _fontNameMap[font] = "";  // Empty = use default
        _fontInfoToFamily[baseName] = "";
        return "";
    }

    // Get font data size first
    size_t dataLen = 0;
    if (!FPDFFont_GetFontData(font, nullptr, 0, &dataLen) || dataLen == 0) {
        ywarn("PdfiumWidget: failed to get font data size for '{}'", fontFamily);
        _fontNameMap[font] = "";
        _fontInfoToFamily[baseName] = "";
        return "";
    }

    // Allocate buffer and get font data
    std::vector<uint8_t> fontData(dataLen);
    size_t actualLen = 0;
    if (!FPDFFont_GetFontData(font, fontData.data(), fontData.size(), &actualLen)) {
        ywarn("PdfiumWidget: failed to extract font data for '{}'", fontFamily);
        _fontNameMap[font] = "";
        _fontInfoToFamily[baseName] = "";
        return "";
    }

    yinfo("PdfiumWidget: extracted embedded font '{}' ({} bytes)", fontFamily, actualLen);

    // Store for later atlas generation (need FontManager from plugin)
    PendingFont pending;
    pending.data = std::move(fontData);
    pending.name = fontFamily;
    _pendingFonts[font] = std::move(pending);
    _fontNameMap[font] = fontFamily;

    // Map base font name to family name for FPDFText lookups
    _fontInfoToFamily[baseName] = fontFamily;

    return fontFamily;
}

Result<void> PdfiumWidget::generateFontAtlases() {
    if (_pendingFonts.empty()) {
        return Ok();
    }

    FontManager* fontMgr = _plugin->getFontManager();
    if (!fontMgr) {
        return Err<void>("No FontManager available");
    }

    for (auto& [font, pending] : _pendingFonts) {
        // Check if font already registered
        if (fontMgr->hasFont(pending.name)) {
            yinfo("PdfiumWidget: font '{}' already in cache", pending.name);
            continue;
        }

        // Generate MSDF atlas for the embedded font
        yinfo("PdfiumWidget: generating MSDF atlas for '{}'...", pending.name);
        auto result = fontMgr->getFont(pending.data.data(), pending.data.size(), pending.name, 32.0f);
        if (!result) {
            ywarn("PdfiumWidget: failed to generate atlas for '{}': {}", pending.name, result.error().message());
            // Mark as failed - will use default font
            _fontNameMap[font] = "";
        } else {
            yinfo("PdfiumWidget: successfully generated atlas for '{}'", pending.name);
        }
    }

    _pendingFonts.clear();  // Free the font data memory
    return Ok();
}

//-----------------------------------------------------------------------------
// Font Extraction from Page Objects
//-----------------------------------------------------------------------------

void PdfiumWidget::extractFontsFromPage(FPDF_PAGE page) {
    int objCount = FPDFPage_CountObjects(page);
    yinfo("PdfiumWidget: page has {} objects", objCount);

    std::set<FPDF_FONT> seenFonts;

    for (int i = 0; i < objCount; i++) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
        if (!obj) continue;

        int objType = FPDFPageObj_GetType(obj);
        if (objType != FPDF_PAGEOBJ_TEXT) continue;

        // Get font from text object
        FPDF_FONT font = FPDFTextObj_GetFont(obj);
        if (!font || seenFonts.count(font)) continue;

        seenFonts.insert(font);

        // Register this font (extracts embedded data if available)
        std::string fontName = registerFont(font);
        yinfo("PdfiumWidget: found text object with font: {}", fontName.empty() ? "(default)" : fontName);
    }

    yinfo("PdfiumWidget: extracted {} unique fonts from page", seenFonts.size());
}

//-----------------------------------------------------------------------------
// Page Content Extraction
//-----------------------------------------------------------------------------

Result<void> PdfiumWidget::extractPageContent(int pageNum) {
    if (!_doc || pageNum < 0 || pageNum >= _pageCount) {
        return Err<void>("Invalid page number");
    }

    _currentPage = pageNum;
    _pages.clear();

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(_doc), pageNum);
    if (!page) {
        return Err<void>("Failed to load page");
    }

    // Get page dimensions
    double pageWidth = FPDF_GetPageWidth(page);
    double pageHeight = FPDF_GetPageHeight(page);

    ExtractedPage pdfPage;
    pdfPage.width = static_cast<float>(pageWidth);
    pdfPage.height = static_cast<float>(pageHeight);

    // Extract embedded fonts from page objects FIRST
    extractFontsFromPage(page);

    // Generate MSDF atlases for extracted fonts
    if (auto res = generateFontAtlases(); !res) {
        ywarn("PdfiumWidget: failed to generate font atlases: {}", res.error().message());
    }

    // Load text page for character extraction
    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    if (!textPage) {
        FPDF_ClosePage(page);
        return Err<void>("Failed to load text page");
    }

    // Get character count
    int charCount = FPDFText_CountChars(textPage);
    yinfo("PdfiumWidget: page {} has {} characters, pageSize={}x{}", pageNum, charCount, pageWidth, pageHeight);

    // First pass: find font size per baseline Y position (line) using full-height chars
    std::map<int, double> lineFontSizes;  // Baseline Y (rounded) -> font size
    for (int i = 0; i < charCount; i++) {
        unsigned int cp = FPDFText_GetUnicode(textPage, i);
        if (cp == 0 || cp == '\n' || cp == '\r') continue;

        // Only use full-height chars (uppercase, digits) to determine line font size
        bool isFullHeight = (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') ||
                           cp == 'b' || cp == 'd' || cp == 'f' || cp == 'h' ||
                           cp == 'k' || cp == 'l' || cp == 't';
        if (!isFullHeight) continue;

        // Use ORIGIN (baseline) for line grouping, not bounding box bottom
        // This ensures descender chars ('g', 'p', 'y') find the same line
        double originX, originY;
        if (!FPDFText_GetCharOrigin(textPage, i, &originX, &originY)) continue;

        double left, right, bottom, top;
        if (FPDFText_GetCharBox(textPage, i, &left, &right, &bottom, &top)) {
            int yKey = static_cast<int>(originY);  // Use baseline Y to group by line
            double boxHeight = top - bottom;
            double estFontSize = boxHeight / 0.7;  // Cap height is ~70% of em
            if (lineFontSizes.find(yKey) == lineFontSizes.end() || estFontSize > lineFontSizes[yKey]) {
                lineFontSizes[yKey] = estFontSize;
            }
        }
    }

    int debugCount = 0;
    for (int i = 0; i < charCount; i++) {
        // Get Unicode codepoint
        unsigned int codepoint = FPDFText_GetUnicode(textPage, i);

        // Skip control characters and special markers
        if (codepoint == 0 || codepoint == 0xFFFE || codepoint == 0xFFFF) {
            continue;
        }

        // Skip newlines/carriage returns (they mess up positioning)
        if (codepoint == '\n' || codepoint == '\r') {
            continue;
        }

        ExtractedChar textChar;
        textChar.codepoint = codepoint;

        // Get character origin (baseline position) - this is correct for font rendering
        double originX = 0, originY = 0;
        if (!FPDFText_GetCharOrigin(textPage, i, &originX, &originY)) {
            // Fallback to bounding box if origin not available
            double left = 0, right = 0, bottom = 0, top = 0;
            if (!FPDFText_GetCharBox(textPage, i, &left, &right, &bottom, &top)) {
                continue;
            }
            originX = left;
            originY = bottom;  // Approximate baseline
        }

        // Get bounding box for width/height (still needed for sizing)
        double left = 0, right = 0, bottom = 0, top = 0;
        FPDFText_GetCharBox(textPage, i, &left, &right, &bottom, &top);

        // Store ORIGIN (baseline) position and ASCENT from charbox
        // ascent = distance from baseline to charbox TOP (this is what PDFium uses for positioning)
        textChar.x = static_cast<float>(originX);
        textChar.y = static_cast<float>(originY);  // Baseline in PDF coords (Y-up)
        textChar.width = static_cast<float>(right - left);
        textChar.height = static_cast<float>(top - bottom);
        textChar.ascent = static_cast<float>(top - originY);  // PDFium's actual ascent for this glyph

        // Get font size - prefer pre-calculated line font size for consistency
        double fontSize = FPDFText_GetFontSize(textPage, i);
        double boxHeight = top - bottom;

        // Many PDFs use text matrix scaling with fontSize=1.0
        // Look up consistent font size for this line using baseline Y (originY)
        int yKey = static_cast<int>(originY);  // Use baseline Y to find line
        auto lineIt = lineFontSizes.find(yKey);
        if (lineIt != lineFontSizes.end()) {
            fontSize = lineIt->second;
        } else if (fontSize < 2.0 && boxHeight > 1.0) {
            // No full-height char on this line, estimate from this char's box
            // x-height is ~50% of em for lowercase, cap-height ~70% for uppercase
            bool isUpperOrDigit = (codepoint >= 'A' && codepoint <= 'Z') || (codepoint >= '0' && codepoint <= '9');
            fontSize = boxHeight / (isUpperOrDigit ? 0.7 : 0.5);
        }

        textChar.size = static_cast<float>(fontSize);

        // Skip characters with zero or very small size
        if (textChar.size < 0.1f) {
            continue;
        }

        // Debug first few characters
        if (debugCount < 10) {
            yinfo("PdfiumWidget: char[{}] cp={} ('{}') pos=({:.1f},{:.1f}) size={:.1f}",
                  debugCount, codepoint, (codepoint > 31 && codepoint < 127) ? (char)codepoint : '?',
                  textChar.x, textChar.y, textChar.size);
            debugCount++;
        }

        // Get font info (name and flags)
        char fontInfoName[256] = {0};
        int flags = 0;
        unsigned long fontNameLen = FPDFText_GetFontInfo(textPage, i, fontInfoName, sizeof(fontInfoName), &flags);
        (void)fontNameLen;

        // Look up registered font family from the font info name
        std::string fontInfoStr = fontInfoName;
        auto fontIt = _fontInfoToFamily.find(fontInfoStr);
        if (fontIt != _fontInfoToFamily.end()) {
            textChar.fontFamily = fontIt->second;
        } else {
            // Font not found in our map - use default
            textChar.fontFamily = "";
        }

        // Determine bold/italic from font flags
        // PDF font flags: bit 19 = force bold, bit 7 = italic
        textChar.bold = (flags & 0x40000) != 0;
        textChar.italic = (flags & 0x40) != 0;

        // Also check font weight for bold detection
        int fontWeight = FPDFText_GetFontWeight(textPage, i);
        if (fontWeight >= 700) {
            textChar.bold = true;
        }

        // Default black color - PDFium's FPDFText_GetFillColor often returns false
        textChar.color = 0xFF000000;

        pdfPage.chars.push_back(textChar);
    }

    FPDFText_ClosePage(textPage);
    FPDF_ClosePage(page);

    _pages.push_back(std::move(pdfPage));
    yinfo("PdfiumWidget: extracted {} valid characters from page {}", _pages[0].chars.size(), pageNum);

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

void PdfiumWidget::buildRichTextContent(float viewWidth) {
    if (!_richText || _pages.empty()) return;

    _richText->clear();

    const auto& page = _pages[0];
    float pdfWidth = page.width;
    float pdfHeight = page.height;

    // Calculate scale to fit view width
    float scale = viewWidth / pdfWidth * _zoom;
    _documentHeight = pdfHeight * scale;

    yinfo("PdfiumWidget: building RichText, pdfSize={}x{}, viewWidth={}, scale={}, docHeight={}",
          pdfWidth, pdfHeight, viewWidth, scale, _documentHeight);

    int debugCount = 0;
    for (const auto& ch : page.chars) {
        // Convert PDF coordinates to screen coordinates
        // PDF has Y-up (origin at bottom-left), screen has Y-down (origin at top-left)
        // ch.x, ch.y = origin (baseline) position in PDF coords
        // ch.ascent = distance from baseline to charbox TOP (PDFium's actual ascent)
        float baselineScreenX = ch.x * scale;
        float baselineScreenY = (pdfHeight - ch.y) * scale;  // Flip Y: baseline in screen coords
        float fontSize = ch.size * scale;
        float pdfiumAscent = ch.ascent * scale;  // PDFium's ascent for this glyph

        // Skip very small text
        if (fontSize < 1.0f) {
            continue;
        }

        // Color conversion (ARGB to RGBA)
        float r = ((ch.color >> 16) & 0xFF) / 255.0f;
        float g = ((ch.color >> 8) & 0xFF) / 255.0f;
        float b = (ch.color & 0xFF) / 255.0f;
        float a = ((ch.color >> 24) & 0xFF) / 255.0f;

        // Keep original PDF colors - we render on white background
        // (no color conversion needed)

        // Ensure alpha is 1.0 for visibility
        if (a < 0.1f) {
            a = 1.0f;
        }

        // Debug first few screen coordinates
        if (debugCount < 10) {
            yinfo("PdfiumWidget: screen[{}] cp={} ('{}') baseline=({:.1f},{:.1f}) pdfiumAscent={:.1f} fontSize={:.1f} font={}",
                  debugCount, ch.codepoint, (ch.codepoint > 31 && ch.codepoint < 127) ? (char)ch.codepoint : '?',
                  baselineScreenX, baselineScreenY, pdfiumAscent, fontSize, ch.fontFamily);
            debugCount++;
        }

        // Create TextChar for RichText
        // Use baseline position + PDFium's ascent to compute glyph top
        TextChar textChar;
        textChar.codepoint = ch.codepoint;
        textChar.x = baselineScreenX;
        textChar.y = baselineScreenY - pdfiumAscent;  // Glyph TOP = baseline - ascent (screen Y-down)
        textChar.size = fontSize;
        textChar.color = glm::vec4(r, g, b, a);
        textChar.fontFamily = ch.fontFamily;
        // prePositioned=true: position is glyph top-left (computed from PDFium's ascent)
        textChar.prePositioned = true;
        // Set target size from charbox
        textChar.targetWidth = ch.width * scale;
        textChar.targetHeight = ch.height * scale;

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
    ydebug("PdfiumWidget: built RichText content with {} chars", page.chars.size());
}

//-----------------------------------------------------------------------------
// Render
//-----------------------------------------------------------------------------

void PdfiumWidget::prepareFrame(WebGPUContext& ctx, bool on) {
    (void)ctx;
    (void)on;
    // PDF renders directly to render pass - no texture preparation needed
}

Result<void> PdfiumWidget::render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on) {
    if (!on || _failed || !_visible) return Ok();
    if (_pixelWidth <= 0 || _pixelHeight <= 0) return Ok();

    // Create RichText if needed
    if (!_richText) {
        auto fontMgr = _plugin->getFontManager();
        if (!fontMgr) {
            _failed = true;
            return Err<void>("PdfiumWidget: no font manager");
        }

        auto result = RichText::create(&ctx, ctx.getSurfaceFormat(), fontMgr);
        if (!result) {
            _failed = true;
            return Err<void>("PdfiumWidget: failed to create RichText", result);
        }
        _richText = *result;
        _richText->setDefaultFontFamily("sans-serif");
        _richText->setBackgroundColor(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
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

bool PdfiumWidget::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)xoffset;

    // Ctrl + scroll = zoom
    if (mods & 0x0002) {  // GLFW_MOD_CONTROL
        float zoomFactor = (yoffset > 0) ? 1.1f : 0.9f;
        _zoom *= zoomFactor;
        _zoom = std::clamp(_zoom, 0.1f, 10.0f);
        // Force re-layout
        _lastViewWidth = 0;
        _lastViewHeight = 0;
        ydebug("PdfiumWidget: zoom changed to {}", _zoom);
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

bool PdfiumWidget::onKey(int key, int scancode, int action, int mods) {
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
const char* name() { return "pdfium"; }
yetty::Result<yetty::PluginPtr> create() {
    return yetty::PdfiumPlugin::create();
}
}
