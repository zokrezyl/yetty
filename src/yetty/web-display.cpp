#if YETTY_WEB

#include "web-display.h"
#include <emscripten.h>
#include <ytrace/ytrace.hpp>
#include <cstring>

namespace yetty {

// Static instance for JavaScript callbacks
WebDisplay* WebDisplay::_sInstance = nullptr;

// vterm screen callbacks
static VTermScreenCallbacks screenCallbacks = {
    .damage = WebDisplay::onDamage,
    .moverect = nullptr,
    .movecursor = WebDisplay::onMoveCursor,
    .settermprop = WebDisplay::onSetTermProp,
    .bell = nullptr,
    .resize = nullptr,
    .sb_pushline = nullptr,
    .sb_popline = nullptr,
    .sb_clear = nullptr,
};

WebDisplay::WebDisplay(uint32_t cols, uint32_t rows,
                       WebGPUContext::Ptr ctx, FontManager::Ptr fontManager) noexcept
    : Widget()
    , _grid(cols, rows)
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

    // Initialize vterm
    _vterm = vterm_new(_rows, _cols);
    if (!_vterm) {
        return Err<void>("Failed to create vterm");
    }
    vterm_set_utf8(_vterm, 1);

    _vtermScreen = vterm_obtain_screen(_vterm);
    vterm_screen_set_callbacks(_vtermScreen, &screenCallbacks, this);
    vterm_screen_reset(_vtermScreen, 1);

    yinfo("WebDisplay initialized: {}x{} grid with vterm", _cols, _rows);

    // Print initial prompt
    const char* welcome = "\r\n  Welcome to yetty - WebGPU Terminal\r\n\r\n$ ";
    vterm_input_write(_vterm, welcome, strlen(welcome));
    syncToGrid();

    return Ok();
}

//=============================================================================
// vterm callbacks
//=============================================================================

int WebDisplay::onDamage(VTermRect rect, void* user)
{
    auto* self = static_cast<WebDisplay*>(user);
    self->_needsSync = true;
    return 1;
}

int WebDisplay::onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user)
{
    auto* self = static_cast<WebDisplay*>(user);
    self->_cursorRow = pos.row;
    self->_cursorCol = pos.col;
    self->_cursorVisible = visible;
    return 1;
}

int WebDisplay::onSetTermProp(VTermProp prop, VTermValue* val, void* user)
{
    return 1;
}

//=============================================================================
// Terminal I/O
//=============================================================================

void WebDisplay::write(const char* data, size_t len)
{
    if (!_vterm || !data || len == 0) return;
    vterm_input_write(_vterm, data, len);
    _needsSync = true;
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
// Grid synchronization
//=============================================================================

void WebDisplay::colorToRGB(const VTermColor& color, uint8_t& r, uint8_t& g, uint8_t& b)
{
    if (VTERM_COLOR_IS_RGB(&color)) {
        r = color.rgb.red;
        g = color.rgb.green;
        b = color.rgb.blue;
    } else if (VTERM_COLOR_IS_INDEXED(&color)) {
        // Basic 16-color palette
        static const uint8_t palette[16][3] = {
            {0, 0, 0}, {170, 0, 0}, {0, 170, 0}, {170, 85, 0},
            {0, 0, 170}, {170, 0, 170}, {0, 170, 170}, {170, 170, 170},
            {85, 85, 85}, {255, 85, 85}, {85, 255, 85}, {255, 255, 85},
            {85, 85, 255}, {255, 85, 255}, {85, 255, 255}, {255, 255, 255}
        };
        int idx = color.indexed.idx;
        if (idx < 16) {
            r = palette[idx][0];
            g = palette[idx][1];
            b = palette[idx][2];
        } else if (idx < 232) {
            // 6x6x6 color cube
            idx -= 16;
            r = (idx / 36) * 51;
            g = ((idx / 6) % 6) * 51;
            b = (idx % 6) * 51;
        } else {
            // Grayscale
            int gray = (idx - 232) * 10 + 8;
            r = g = b = gray;
        }
    } else {
        // Default colors
        r = g = b = 204;
    }
}

void WebDisplay::syncToGrid()
{
    if (!_vtermScreen) return;

    for (int row = 0; row < static_cast<int>(_rows); row++) {
        for (int col = 0; col < static_cast<int>(_cols); col++) {
            VTermPos pos = {row, col};
            VTermScreenCell cell;
            vterm_screen_get_cell(_vtermScreen, pos, &cell);

            uint32_t codepoint = cell.chars[0] ? cell.chars[0] : ' ';
            uint8_t fgR, fgG, fgB, bgR, bgG, bgB;
            colorToRGB(cell.fg, fgR, fgG, fgB);
            colorToRGB(cell.bg, bgR, bgG, bgB);

            // Use default colors if black on black
            if (fgR == 0 && fgG == 0 && fgB == 0 && bgR == 0 && bgG == 0 && bgB == 0) {
                fgR = fgG = fgB = 204;
            }

            _grid.setCell(col, row, codepoint,
                         fgR, fgG, fgB,
                         bgR, bgG, bgB);
        }
    }

    _needsSync = false;
}

//=============================================================================
// Rendering
//=============================================================================

Result<void> WebDisplay::render(WebGPUContext& ctx)
{
    if (!_renderer) {
        return Err<void>("No renderer available");
    }

    // Sync vterm to grid if needed
    if (_needsSync) {
        syncToGrid();
    }

    // Render the grid with cursor
    _renderer->render(_grid, _cursorCol, _cursorRow, _cursorVisible);

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

// Force sync display
EMSCRIPTEN_KEEPALIVE
void yetty_sync()
{
    auto* display = yetty::WebDisplay::instance();
    if (display) {
        display->syncToGrid();
    }
}

} // extern "C"

#endif // YETTY_WEB
