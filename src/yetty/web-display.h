#pragma once

#if YETTY_WEB

#include <yetty/widget.h>
#include <yetty/font.h>
#include <yetty/result.hpp>
#include "grid.h"
#include "grid-renderer.h"

extern "C" {
#include <vterm.h>
}

#include <memory>
#include <string>
#include <vector>

namespace yetty {

//=============================================================================
// WebDisplay - Terminal Widget for web builds with vterm emulation
//
// This provides full terminal emulation via libvterm, with input/output
// bridged to JavaScript for integration with Toybox shell.
//=============================================================================

class WebDisplay : public Widget {
public:
    using Ptr = std::shared_ptr<WebDisplay>;

    // Factory - creates display with given grid size
    static Result<Ptr> create(uint32_t cols, uint32_t rows,
                              WebGPUContext::Ptr ctx, FontManager::Ptr fontManager) noexcept;

    ~WebDisplay() override;

    // Non-copyable
    WebDisplay(const WebDisplay&) = delete;
    WebDisplay& operator=(const WebDisplay&) = delete;

    //=========================================================================
    // Widget interface
    //=========================================================================
    // id() inherited from Widget base class
    uint32_t zOrder() const override { return _zOrder; }
    const std::string& name() const override { return _name; }

    void start() override { _running.store(true); }
    void stop() override { _running.store(false); }
    bool isRunning() const override { return _running.load(); }

    void prepareFrame(WebGPUContext& ctx, bool on) override;
    Result<void> render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on) override;

    //=========================================================================
    // Terminal emulation interface
    //=========================================================================

    // Write data to terminal (from shell output)
    void write(const char* data, size_t len);
    void write(const std::string& str) { write(str.c_str(), str.size()); }

    // Send keyboard input (returns data to send to shell)
    void sendKey(uint32_t codepoint);
    void sendSpecialKey(VTermKey key, VTermModifier mod = VTERM_MOD_NONE);

    // Read pending output (what to send to shell after keyboard input)
    size_t readOutput(char* buffer, size_t maxlen);

    // Grid access
    Grid& grid() { return _grid; }
    const Grid& grid() const { return _grid; }

    // Font access
    Font* font() const { return _font; }

    // Cursor control
    void setCursor(int col, int row, bool visible = true);
    int getCursorCol() const { return _cursorCol; }
    int getCursorRow() const { return _cursorRow; }
    bool isCursorVisible() const { return _cursorVisible; }

    // Cell size for scaling
    void setCellSize(float width, float height);

    // Set rendering scale
    void setScale(float scale);

    // Sync vterm state to grid
    void syncToGrid();

    // Check if rendering is needed
    bool needsRender() const;

    // Static instance accessor (for C callbacks)
    static WebDisplay* instance() { return _sInstance; }

    // vterm callbacks (need to be public for C callback struct)
    static int onDamage(VTermRect rect, void* user);
    static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user);
    static int onSetTermProp(VTermProp prop, VTermValue* val, void* user);

private:
    WebDisplay(uint32_t cols, uint32_t rows,
               WebGPUContext::Ptr ctx, FontManager::Ptr fontManager) noexcept;
    Result<void> init() noexcept override;

    void colorToRGB(const VTermColor& color, uint8_t& r, uint8_t& g, uint8_t& b);

    // Display state
    Grid _grid;
    Font* _font = nullptr;
    GridRenderer::Ptr _renderer;
    FontManager::Ptr _fontManager;

    // vterm state
    VTerm* _vterm = nullptr;
    VTermScreen* _vtermScreen = nullptr;
    bool _needsSync = true;
    bool _needsRender = true;

    // Cursor
    int _cursorCol = 0;
    int _cursorRow = 0;
    bool _cursorVisible = true;

    uint32_t _cols;
    uint32_t _rows;

    static WebDisplay* _sInstance;
};

} // namespace yetty

#endif // YETTY_WEB
