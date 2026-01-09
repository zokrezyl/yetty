#pragma once

#include <yetty/renderable.h>
#include <yetty/font.h>
#include <yetty/config.h>
#include <yetty/result.hpp>
#include "remote-terminal-backend.h"
#include "damage-rect.h"

#include <uv.h>
#include <memory>
#include <string>

namespace yetty {

class PluginManager;
class EmojiAtlas;
class GridRenderer;

//=============================================================================
// RemoteTerminal - Renderable that uses RemoteTerminalBackend for multiplexing
//
// This class provides a Terminal-like interface but connects to yetty-server
// for the actual PTY/vterm processing. The Grid is in shared memory.
//
// Threading model:
//   - Single-threaded: uses external libuv loop (from Yetty)
//   - Server notifications trigger re-render
//   - Input is forwarded to server via socket
//=============================================================================

class RemoteTerminal : public Renderable {
public:
    using Ptr = std::shared_ptr<RemoteTerminal>;

    // Factory - creates remote terminal with given grid size
    static Result<Ptr> create(uint32_t id, uint32_t cols, uint32_t rows,
                              Font* font, uv_loop_t* loop) noexcept;

    ~RemoteTerminal() override;

    // Non-copyable
    RemoteTerminal(const RemoteTerminal&) = delete;
    RemoteTerminal& operator=(const RemoteTerminal&) = delete;

    //=========================================================================
    // Renderable interface
    //=========================================================================
    uint32_t id() const override { return id_; }
    uint32_t zOrder() const override { return zOrder_; }
    const std::string& name() const override { return name_; }

    void start() override;
    void stop() override;
    bool isRunning() const override { return backend_ && backend_->isRunning(); }

    Result<void> render(WebGPUContext& ctx) override;

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
    const Grid& getGrid() const { return backend_->getGrid(); }
    Grid& getGridMutable() { return backend_->getGridMutable(); }

    // Cursor state
    int getCursorRow() const { return backend_->getCursorRow(); }
    int getCursorCol() const { return backend_->getCursorCol(); }
    bool isCursorVisible() const { return backend_->isCursorVisible() && cursorBlink_; }

    // Damage tracking
    const std::vector<DamageRect>& getDamageRects() const { return backend_->getDamageRects(); }
    void clearDamageRects() { backend_->clearDamageRects(); }
    bool hasDamage() const { return backend_->hasDamage(); }
    bool hasFullDamage() const { return backend_->hasFullDamage(); }
    void clearFullDamage() { backend_->clearFullDamage(); }

    // Scrollback navigation
    void scrollUp(int lines = 1) { backend_->scrollUp(lines); }
    void scrollDown(int lines = 1) { backend_->scrollDown(lines); }
    void scrollToTop() { backend_->scrollToTop(); }
    void scrollToBottom() { backend_->scrollToBottom(); }
    int getScrollOffset() const { return backend_->getScrollOffset(); }
    bool isScrolledBack() const { return backend_->isScrolledBack(); }
    size_t getScrollbackSize() const { return backend_->getScrollbackSize(); }

    // Selection
    void startSelection(int row, int col, SelectionMode mode = SelectionMode::Character) {
        backend_->startSelection(row, col, mode);
    }
    void extendSelection(int row, int col) { backend_->extendSelection(row, col); }
    void clearSelection() { backend_->clearSelection(); }
    bool hasSelection() const { return backend_->hasSelection(); }
    bool isInSelection(int row, int col) const { return backend_->isInSelection(row, col); }
    std::string getSelectedText() { return backend_->getSelectedText(); }

    // Configuration
    void setConfig(const Config* config) { config_ = config; }
    void setShell(const std::string& shell) { shell_ = shell; }

    // Plugin support
    void setPluginManager(PluginManager* mgr) { pluginManager_ = mgr; }
    PluginManager* getPluginManager() const { return pluginManager_; }
    void setEmojiAtlas(EmojiAtlas* atlas) { emojiAtlas_ = atlas; }
    void setRenderer(GridRenderer* renderer) { renderer_ = renderer; }

    // Cell size
    void setCellSize(uint32_t width, uint32_t height);
    uint32_t getCellWidth() const { return cellWidth_; }
    uint32_t getCellHeight() const { return cellHeight_; }

    void setBaseCellSize(float width, float height);
    float getBaseCellWidth() const { return baseCellWidth_; }
    float getBaseCellHeight() const { return baseCellHeight_; }

    void setZoomLevel(float zoom);
    float getZoomLevel() const { return zoomLevel_; }
    float getCellWidthF() const { return baseCellWidth_ * zoomLevel_; }
    float getCellHeightF() const { return baseCellHeight_ * zoomLevel_; }

    // Mouse mode
    int getMouseMode() const { return backend_->getMouseMode(); }
    bool wantsMouseEvents() const { return backend_->wantsMouseEvents(); }
    bool isAltScreen() const { return backend_->isAltScreen(); }

    // Backend access (for advanced use)
    RemoteTerminalBackend* getBackend() const { return backend_.get(); }

private:
    RemoteTerminal(uint32_t id, uint32_t cols, uint32_t rows, Font* font, uv_loop_t* loop) noexcept;
    Result<void> init() noexcept;

    // libuv callbacks
    static void onTimer(uv_timer_t* handle);

    // Update cursor blink
    void updateCursorBlink(double currentTime);

    //=========================================================================
    // Identity
    //=========================================================================
    uint32_t id_;
    uint32_t zOrder_ = 0;
    std::string name_;

    //=========================================================================
    // libuv (external loop, not owned)
    //=========================================================================
    uv_loop_t* loop_ = nullptr;
    uv_timer_t* cursorTimer_ = nullptr;

    //=========================================================================
    // Backend and state
    //=========================================================================
    RemoteTerminalBackend::Ptr backend_;
    Font* font_;
    std::string shell_;

    uint32_t cols_;
    uint32_t rows_;

    bool cursorBlink_ = true;
    double lastBlinkTime_ = 0.0;
    double blinkInterval_ = 0.5;

    const Config* config_ = nullptr;
    PluginManager* pluginManager_ = nullptr;
    EmojiAtlas* emojiAtlas_ = nullptr;
    GridRenderer* renderer_ = nullptr;

    uint32_t cellWidth_ = 10;
    uint32_t cellHeight_ = 20;
    float baseCellWidth_ = 10.0f;
    float baseCellHeight_ = 20.0f;
    float zoomLevel_ = 1.0f;
};

} // namespace yetty
