#pragma once

#include <yetty/widget.h>
#include <yetty/font.h>
#include <yetty/config.h>
#include <yetty/result.hpp>
#include "remote-terminal-backend.h"
#include "damage-rect.h"

#include <uv.h>
#include <memory>
#include <string>

namespace yetty {

class EmojiAtlas;
class GridRenderer;

//=============================================================================
// RemoteTerminal - Widget that uses RemoteTerminalBackend for multiplexing
//
// This class provides a Terminal-like interface but connects to yetty-server
// for the actual PTY/vterm processing. The Grid is in shared memory.
//
// Threading model:
//   - Single-threaded: uses external libuv loop (from Yetty)
//   - Server notifications trigger re-render
//   - Input is forwarded to server via socket
//=============================================================================

class RemoteTerminal : public Widget {
public:
    using Ptr = std::shared_ptr<RemoteTerminal>;

    // Factory - creates remote terminal with given grid size
    // ID is ignored - Widget base class auto-assigns IDs
    static Result<Ptr> create(uint32_t id, uint32_t cols, uint32_t rows,
                              Font* font, uv_loop_t* loop) noexcept;

    ~RemoteTerminal() override;

    // Non-copyable
    RemoteTerminal(const RemoteTerminal&) = delete;
    RemoteTerminal& operator=(const RemoteTerminal&) = delete;

    //=========================================================================
    // Widget interface
    //=========================================================================
    // id() inherited from Widget
    uint32_t zOrder() const override { return _zOrder; }
    const std::string& name() const override { return _name; }

    void start() override;
    void stop() override;
    bool isRunning() const override { return _backend && _backend->isRunning(); }

    // Pre-render (RemoteTerminal manages its own pass for main content)
    void prepareFrame(WebGPUContext& ctx) override;

    // Batched render (for child widgets - RemoteTerminal doesn't use this directly)
    Result<void> render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override {
        (void)pass; (void)ctx; return Ok();
    }

    //=========================================================================
    // Terminal-compatible interface
    //=========================================================================

    // Send keyboard input (forwarded to server)
    void sendKey(uint32_t codepoint, VTermModifier mod = VTERM_MOD_NONE);
    void sendSpecialKey(VTermKey key, VTermModifier mod = VTERM_MOD_NONE);
    void sendRaw(const char* data, size_t len);

    // Resize terminal
    void resize(uint32_t cols, uint32_t rows);

    // Grid access (from shared memory)
    const Grid& getGrid() const { return _backend->getGrid(); }
    Grid& getGridMutable() { return _backend->getGridMutable(); }

    // Cursor state
    int getCursorRow() const { return _backend->getCursorRow(); }
    int getCursorCol() const { return _backend->getCursorCol(); }
    bool isCursorVisible() const { return _backend->isCursorVisible() && _cursorBlink; }

    // Damage tracking
    const std::vector<DamageRect>& getDamageRects() const { return _backend->getDamageRects(); }
    void clearDamageRects() { _backend->clearDamageRects(); }
    bool hasDamage() const { return _backend->hasDamage(); }
    bool hasFullDamage() const { return _backend->hasFullDamage(); }
    void clearFullDamage() { _backend->clearFullDamage(); }

    // Scrollback navigation
    void scrollUp(int lines = 1) { _backend->scrollUp(lines); }
    void scrollDown(int lines = 1) { _backend->scrollDown(lines); }
    void scrollToTop() { _backend->scrollToTop(); }
    void scrollToBottom() { _backend->scrollToBottom(); }
    int getScrollOffset() const { return _backend->getScrollOffset(); }
    bool isScrolledBack() const { return _backend->isScrolledBack(); }
    size_t getScrollbackSize() const { return _backend->getScrollbackSize(); }

    // Selection
    void startSelection(int row, int col, SelectionMode mode = SelectionMode::Character) {
        _backend->startSelection(row, col, mode);
    }
    void extendSelection(int row, int col) { _backend->extendSelection(row, col); }
    void clearSelection() { _backend->clearSelection(); }
    bool hasSelection() const { return _backend->hasSelection(); }
    bool isInSelection(int row, int col) const { return _backend->isInSelection(row, col); }
    std::string getSelectedText() { return _backend->getSelectedText(); }

    // Configuration
    void setConfig(const Config* config) { _config = config; }
    void setShell(const std::string& shell) { _shell = shell; }

    // Emoji support
    void setEmojiAtlas(EmojiAtlas* atlas) { _emojiAtlas = atlas; }
    void setRenderer(GridRenderer* renderer) { _renderer = renderer; }

    // Cell size
    void setCellSize(uint32_t width, uint32_t height);
    uint32_t getCellWidth() const { return _cellWidth; }
    uint32_t getCellHeight() const { return _cellHeight; }

    void setBaseCellSize(float width, float height);
    float getBaseCellWidth() const { return _baseCellWidth; }
    float getBaseCellHeight() const { return _baseCellHeight; }

    void setZoomLevel(float zoom);
    float getZoomLevel() const { return _zoomLevel; }
    float getCellWidthF() const { return _baseCellWidth * _zoomLevel; }
    float getCellHeightF() const { return _baseCellHeight * _zoomLevel; }

    // Mouse mode
    int getMouseMode() const { return _backend->getMouseMode(); }
    bool wantsMouseEvents() const { return _backend->wantsMouseEvents(); }
    bool isAltScreen() const { return _backend->isAltScreen(); }

    // Backend access (for advanced use)
    RemoteTerminalBackend* getBackend() const { return _backend.get(); }

private:
    RemoteTerminal(uint32_t cols, uint32_t rows, Font* font, uv_loop_t* loop) noexcept;
    Result<void> init() noexcept override;

    // libuv callbacks
    static void onTimer(uv_timer_t* handle);

    // Update cursor blink
    void updateCursorBlink(double currentTime);

    //=========================================================================
    // libuv (external loop, not owned)
    //=========================================================================
    uv_loop_t* _loop = nullptr;
    uv_timer_t* _cursorTimer = nullptr;

    //=========================================================================
    // Backend and state
    //=========================================================================
    RemoteTerminalBackend::Ptr _backend;
    Font* _font;
    std::string _shell;

    uint32_t _cols;
    uint32_t _rows;

    bool _cursorBlink = true;
    double _lastBlinkTime = 0.0;
    double _blinkInterval = 0.5;

    const Config* _config = nullptr;
    EmojiAtlas* _emojiAtlas = nullptr;
    GridRenderer* _renderer = nullptr;

    uint32_t _cellWidth = 10;
    uint32_t _cellHeight = 20;
    float _baseCellWidth = 10.0f;
    float _baseCellHeight = 20.0f;
    float _zoomLevel = 1.0f;
};

} // namespace yetty
