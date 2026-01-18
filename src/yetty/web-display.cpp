#if YETTY_WEB

#include "web-display.h"
#include <emscripten.h>
#include <ytrace/ytrace.hpp>
#include <cstring>

namespace yetty {

// Static instance for JavaScript callbacks
WebDisplay* WebDisplay::_sInstance = nullptr;

WebDisplay::WebDisplay(uint32_t cols, uint32_t rows,
                       WebGPUContext::Ptr ctx, FontManager::Ptr fontManager) noexcept
    : Widget()
    , _fontManager(fontManager)
    , _cols(cols)
    , _rows(rows)
{
    _ctx = ctx.get();
    _name = "WebDisplay";
    _sInstance = this;
}

WebDisplay::~WebDisplay()
{
    if (_vterm) {
        vterm_free(_vterm);
        _vterm = nullptr;
    }
    if (_sInstance == this) {
        _sInstance = nullptr;
    }
}

Result<WebDisplay::Ptr> WebDisplay::create(uint32_t cols, uint32_t rows,
                                           WebGPUContext::Ptr ctx,
                                           FontManager::Ptr fontManager) noexcept
{
    auto display = Ptr(new WebDisplay(cols, rows, ctx, fontManager));
    if (auto res = display->init(); !res) {
        return Err<Ptr>("Failed to initialize WebDisplay", res);
    }
    return Ok(display);
}

Result<void> WebDisplay::init() noexcept
{
    // Get default font from FontManager
    _font = _fontManager->getDefaultFont();
    if (!_font) {
        return Err<void>("No default font available in FontManager");
    }

    // Create GridRenderer
    auto ctxShared = std::shared_ptr<WebGPUContext>(_ctx, [](WebGPUContext*){});  // non-owning shared_ptr
    auto rendererResult = GridRenderer::create(ctxShared, _fontManager);
    if (!rendererResult) {
        return Err<void>("Failed to create GridRenderer", rendererResult);
    }
    _renderer = *rendererResult;

    // Set cell size based on font metrics
    float lineHeight = _font->getLineHeight();
    float cellWidth = lineHeight * 0.5f;
    float cellHeight = lineHeight;
    _renderer->setCellSize(cellWidth, cellHeight);

    // Initialize renderer screen dimensions from WebGPU context
    // This is critical for correct projection matrix calculation
    uint32_t screenWidth = _ctx->getSurfaceWidth();
    uint32_t screenHeight = _ctx->getSurfaceHeight();
    if (screenWidth > 0 && screenHeight > 0) {
        _renderer->resize(screenWidth, screenHeight);
    }

    // Initialize vterm
    _vterm = vterm_new(_rows, _cols);
    if (!_vterm) {
        return Err<void>("Failed to create vterm");
    }
    vterm_set_utf8(_vterm, 1);

    // Create GPUScreen - replaces vterm's Screen layer with direct State callbacks
    _gpuScreen = std::make_unique<GPUScreen>(_rows, _cols, _font);
    _gpuScreen->attach(_vterm);

    yinfo("WebDisplay initialized: {}x{} grid with GPUScreen, screen {}x{}", 
          _cols, _rows, screenWidth, screenHeight);

    return Ok();
}

//=============================================================================
// GPUScreen callbacks (unused for now, but available if needed)
//=============================================================================

//=============================================================================
// Terminal I/O
//=============================================================================

void WebDisplay::write(const char* data, size_t len)
{
    if (!_vterm || !data || len == 0) return;
    vterm_input_write(_vterm, data, len);
    // Note: damage callbacks will be triggered by vterm_input_write
}

void WebDisplay::sendKey(uint32_t codepoint)
{
    if (!_vterm) return;
    vterm_keyboard_unichar(_vterm, codepoint, VTERM_MOD_NONE);
}

void WebDisplay::sendSpecialKey(VTermKey key, VTermModifier mod)
{
    if (!_vterm) return;
    vterm_keyboard_key(_vterm, key, mod);
}

size_t WebDisplay::readOutput(char* buffer, size_t maxlen)
{
    if (!_vterm || !buffer || maxlen == 0) return 0;
    return vterm_output_read(_vterm, buffer, maxlen);
}

//=============================================================================
// Rendering
//=============================================================================

void WebDisplay::prepareFrame(WebGPUContext& ctx, bool on)
{
    (void)ctx;
    (void)on;
    // Nothing to do here - GPUScreen handles data directly
}

bool WebDisplay::needsRender() const
{
    return _fullDamage || (_gpuScreen && _gpuScreen->hasDamage());
}

Result<void> WebDisplay::render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on)
{
    (void)ctx;
    (void)on;
    if (!_renderer || !_gpuScreen) {
        return Err<void>("No renderer or GPUScreen available");
    }

    // Get cursor info from GPUScreen
    _cursorCol = _gpuScreen->getCursorCol();
    _cursorRow = _gpuScreen->getCursorRow();
    _cursorVisible = _gpuScreen->isCursorVisible();

    // Determine if we need to upload textures (any damage = full upload for now)
    bool needsUpload = _fullDamage || _gpuScreen->hasDamage();

    // Render directly from GPUScreen buffers - zero-copy path
    _renderer->renderToPassFromBuffers(
        pass,
        _cols,
        _rows,
        _gpuScreen->getGlyphData(),
        _gpuScreen->getFgColorData(),
        _gpuScreen->getBgColorData(),
        _gpuScreen->getAttrsData(),
        needsUpload,
        _cursorCol, _cursorRow, _cursorVisible
    );

    // Clear damage after rendering
    _gpuScreen->clearDamage();
    _fullDamage = false;

    return Ok();
}

void WebDisplay::setCursor(int col, int row, bool visible)
{
    _cursorCol = col;
    _cursorRow = row;
    _cursorVisible = visible;
}

void WebDisplay::setCellSize(float width, float height)
{
    if (_renderer) {
        _renderer->setCellSize(width, height);
    }
}

void WebDisplay::setScale(float scale)
{
    if (_renderer && _font) {
        // Scale both cell size AND glyph rendering (like desktop)
        float lineHeight = _font->getLineHeight();
        float baseCellWidth = lineHeight * 0.5f;
        float baseCellHeight = lineHeight;
        _renderer->setCellSize(baseCellWidth * scale, baseCellHeight * scale);
        _renderer->setScale(scale);
        // Mark full damage to trigger re-render with new scale
        _fullDamage = true;
    }
}

void WebDisplay::resize(uint32_t cols, uint32_t rows)
{
    if (cols == _cols && rows == _rows) {
        return;  // No change
    }

    _cols = cols;
    _rows = rows;

    // Resize vterm
    if (_vterm) {
        vterm_set_size(_vterm, _rows, _cols);
    }

    // Resize GPUScreen (note: GPUScreen uses rows, cols order)
    if (_gpuScreen) {
        _gpuScreen->resize(_rows, _cols);
    }

    // Mark full damage for re-render
    _fullDamage = true;

    yinfo("WebDisplay resized to {}x{}", _cols, _rows);
}

void WebDisplay::resizeToPixels(uint32_t pixelWidth, uint32_t pixelHeight)
{
    if (!_font || !_renderer || !_ctx) {
        return;
    }

    // Resize the WebGPU context/swapchain to match canvas size
    _ctx->resize(pixelWidth, pixelHeight);

    // Resize the GridRenderer's screen dimensions
    _renderer->resize(pixelWidth, pixelHeight);

    // Calculate cell size from font
    float lineHeight = _font->getLineHeight();
    float cellWidth = lineHeight * 0.5f;
    float cellHeight = lineHeight;

    // Calculate grid dimensions
    uint32_t cols = static_cast<uint32_t>(pixelWidth / cellWidth);
    uint32_t rows = static_cast<uint32_t>(pixelHeight / cellHeight);

    // Minimum size
    if (cols < 10) cols = 10;
    if (rows < 3) rows = 3;

    // Resize the terminal grid
    resize(cols, rows);
}

} // namespace yetty

//=============================================================================
// JavaScript Bridge Functions
//=============================================================================

extern "C" {

// Write shell output to terminal
EMSCRIPTEN_KEEPALIVE
void yetty_write(const char* data, int len)
{
    auto* display = yetty::WebDisplay::instance();
    if (display && data) {
        if (len < 0) len = strlen(data);
        display->write(data, len);
    }
}

// Send keyboard character
EMSCRIPTEN_KEEPALIVE
void yetty_key(int codepoint)
{
    auto* display = yetty::WebDisplay::instance();
    if (display) {
        display->sendKey(codepoint);
    }
}

// Send special key (Enter, Backspace, etc.)
// key values: 1=Enter, 2=Tab, 3=Backspace, 4=Escape, 5=Up, 6=Down, 7=Left, 8=Right
EMSCRIPTEN_KEEPALIVE
void yetty_special_key(int key, int mods)
{
    auto* display = yetty::WebDisplay::instance();
    if (!display) return;

    VTermKey vtkey = VTERM_KEY_NONE;
    switch (key) {
        case 1: vtkey = VTERM_KEY_ENTER; break;
        case 2: vtkey = VTERM_KEY_TAB; break;
        case 3: vtkey = VTERM_KEY_BACKSPACE; break;
        case 4: vtkey = VTERM_KEY_ESCAPE; break;
        case 5: vtkey = VTERM_KEY_UP; break;
        case 6: vtkey = VTERM_KEY_DOWN; break;
        case 7: vtkey = VTERM_KEY_LEFT; break;
        case 8: vtkey = VTERM_KEY_RIGHT; break;
        case 9: vtkey = VTERM_KEY_HOME; break;
        case 10: vtkey = VTERM_KEY_END; break;
        case 11: vtkey = VTERM_KEY_DEL; break;
    }

    if (vtkey != VTERM_KEY_NONE) {
        VTermModifier vtmods = static_cast<VTermModifier>(mods);
        display->sendSpecialKey(vtkey, vtmods);
    }
}

// Read pending output (what user typed, to send to shell)
EMSCRIPTEN_KEEPALIVE
int yetty_read_input(char* buffer, int maxlen)
{
    auto* display = yetty::WebDisplay::instance();
    if (display && buffer && maxlen > 0) {
        return display->readOutput(buffer, maxlen);
    }
    return 0;
}

// Force sync display (marks full damage for next render)
EMSCRIPTEN_KEEPALIVE
void yetty_sync()
{
    auto* display = yetty::WebDisplay::instance();
    if (display) {
        display->markFullDamage();
    }
}

// Set display scale (glyph rendering scale)
EMSCRIPTEN_KEEPALIVE
void yetty_set_scale(float scale)
{
    auto* display = yetty::WebDisplay::instance();
    if (display) {
        display->setScale(scale);
    }
}

// Resize terminal to fit pixel dimensions
// Returns: cols in high 16 bits, rows in low 16 bits
EMSCRIPTEN_KEEPALIVE
int yetty_resize(int pixelWidth, int pixelHeight)
{
    auto* display = yetty::WebDisplay::instance();
    if (!display || !display->font()) {
        return 0;
    }

    // Resize display to fit pixel dimensions (updates both renderer and grid)
    display->resizeToPixels(static_cast<uint32_t>(pixelWidth), 
                            static_cast<uint32_t>(pixelHeight));

    // Return packed dimensions for JavaScript
    return (static_cast<int>(display->getCols()) << 16) | static_cast<int>(display->getRows());
}

// Get current terminal dimensions
EMSCRIPTEN_KEEPALIVE
int yetty_get_cols()
{
    auto* display = yetty::WebDisplay::instance();
    return display ? static_cast<int>(display->getCols()) : 80;
}

EMSCRIPTEN_KEEPALIVE
int yetty_get_rows()
{
    auto* display = yetty::WebDisplay::instance();
    return display ? static_cast<int>(display->getRows()) : 24;
}

} // extern "C"

#endif // YETTY_WEB
