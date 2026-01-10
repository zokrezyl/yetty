#pragma once

#include "terminal-backend.h"

#ifndef YETTY_SERVER_BUILD
#include <yetty/config.h>
#endif

#include <uv.h>
#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace yetty {

// Forward declarations
class Font;
class Config;
class EmojiAtlas;

//=============================================================================
// LocalTerminalBackend - in-process terminal with PTY and libvterm
//
// This is the original Terminal implementation refactored as a backend.
// It runs vterm and PTY in the same process as the renderer.
//=============================================================================

class LocalTerminalBackend : public ITerminalBackend {
public:
    using Ptr = std::shared_ptr<LocalTerminalBackend>;

    // Factory
    static Result<Ptr> create(uint32_t cols, uint32_t rows, Font* font, uv_loop_t* loop) noexcept;

    ~LocalTerminalBackend() override;

    // Non-copyable
    LocalTerminalBackend(const LocalTerminalBackend&) = delete;
    LocalTerminalBackend& operator=(const LocalTerminalBackend&) = delete;

    //=========================================================================
    // ITerminalBackend interface
    //=========================================================================
    
    Result<void> start(const std::string& shell = "") override;
    void stop() override;
    bool isRunning() const override { return running_; }

    void sendKey(uint32_t codepoint, VTermModifier mod = VTERM_MOD_NONE) override;
    void sendSpecialKey(VTermKey key, VTermModifier mod = VTERM_MOD_NONE) override;
    void sendRaw(const char* data, size_t len) override;

    void resize(uint32_t cols, uint32_t rows) override;

    const Grid& getGrid() const override { return grid_; }
    Grid& getGridMutable() override { return grid_; }
    
    uint32_t getCols() const override { return cols_; }
    uint32_t getRows() const override { return rows_; }

    int getCursorRow() const override { return cursorRow_; }
    int getCursorCol() const override { return cursorCol_; }
    bool isCursorVisible() const override { return cursorVisible_; }

    const std::vector<DamageRect>& getDamageRects() const override { return damageRects_; }
    void clearDamageRects() override { damageRects_.clear(); }
    bool hasDamage() const override { return !damageRects_.empty() || fullDamage_; }
    bool hasFullDamage() const override { return fullDamage_; }
    void clearFullDamage() override { fullDamage_ = false; }

    void scrollUp(int lines = 1) override;
    void scrollDown(int lines = 1) override;
    void scrollToTop() override;
    void scrollToBottom() override;
    int getScrollOffset() const override { return scrollOffset_; }
    bool isScrolledBack() const override { return scrollOffset_ > 0; }
    size_t getScrollbackSize() const override { return scrollback_.size(); }

    void startSelection(int row, int col, SelectionMode mode) override;
    void extendSelection(int row, int col) override;
    void clearSelection() override;
    bool hasSelection() const override { return selectionMode_ != SelectionMode::None; }
    bool isInSelection(int row, int col) const override;
    std::string getSelectedText() override;

    int getMouseMode() const override { return mouseMode_; }
    bool wantsMouseEvents() const override { return mouseMode_ != VTERM_PROP_MOUSE_NONE; }
    bool isAltScreen() const override { return isAltScreen_; }

    void syncToGrid() override;

    //=========================================================================
    // Local-specific interface
    //=========================================================================
    
    // Configuration
    void setConfig(const Config* config) { config_ = config; }
    void setCallbacks(const TerminalBackendCallbacks& callbacks) { callbacks_ = callbacks; }
    
    // Emoji support
    void setEmojiAtlas(EmojiAtlas* atlas) { emojiAtlas_ = atlas; }
    
    // VTerm access (needed for some operations)
    VTermScreen* getVTermScreen() const { return vtermScreen_; }

    // libuv poll handle (for integration with main loop)
    uv_poll_t* getPtyPoll() const { return ptyPoll_; }

    // libvterm callbacks (public for C callback access)
    static int onDamage(VTermRect rect, void* user);
    static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user);
    static int onSetTermProp(VTermProp prop, VTermValue* val, void* user);
    static int onResize(int rows, int cols, void* user);
    static int onBell(void* user);
    static int onOSC(int command, VTermStringFragment frag, void* user);
    static int onSbPushline(int cols, const VTermScreenCell* cells, void* user);
    static int onSbPopline(int cols, VTermScreenCell* cells, void* user);
    static int onMoverect(VTermRect dest, VTermRect src, void* user);

private:
    LocalTerminalBackend(uint32_t cols, uint32_t rows, Font* font, uv_loop_t* loop) noexcept;
    Result<void> init() noexcept;

    // libuv callbacks
    static void onPtyPoll(uv_poll_t* handle, int status, int events);

    // PTY operations
    Result<void> readPty();
    Result<void> writeToPty(const char* data, size_t len);
    Result<void> flushVtermOutput();

    // Grid sync helpers
    void syncDamageToGrid();
    void colorToRGB(const VTermColor& color, uint8_t& r, uint8_t& g, uint8_t& b);

    //=========================================================================
    // State
    //=========================================================================
    
    uv_loop_t* loop_ = nullptr;
    uv_poll_t* ptyPoll_ = nullptr;
    bool running_ = false;

    VTerm* vterm_ = nullptr;
    VTermScreen* vtermScreen_ = nullptr;

    Grid grid_;
    Font* font_;

#ifdef _WIN32
    HPCON hPC_ = INVALID_HANDLE_VALUE;
    HANDLE hPipeIn_ = INVALID_HANDLE_VALUE;
    HANDLE hPipeOut_ = INVALID_HANDLE_VALUE;
    HANDLE hProcess_ = INVALID_HANDLE_VALUE;
    HANDLE hThread_ = INVALID_HANDLE_VALUE;
#else
    int ptyMaster_ = -1;
    pid_t childPid_ = -1;
#endif

    int cursorRow_ = 0;
    int cursorCol_ = 0;
    bool cursorVisible_ = true;
    bool isAltScreen_ = false;

    uint32_t cols_;
    uint32_t rows_;

    std::vector<DamageRect> damageRects_;
    bool fullDamage_ = true;

    const Config* config_ = nullptr;
    TerminalBackendCallbacks callbacks_;
    EmojiAtlas* emojiAtlas_ = nullptr;

    std::string oscBuffer_;
    int oscCommand_ = -1;

    std::deque<ScrollbackLine> scrollback_;
    int scrollOffset_ = 0;

    uint32_t pendingNewlines_ = 0;

    VTermPos selectionStart_ = {0, 0};
    VTermPos selectionEnd_ = {0, 0};
    SelectionMode selectionMode_ = SelectionMode::None;

    int mouseMode_ = VTERM_PROP_MOUSE_NONE;

    static constexpr size_t PTY_READ_BUFFER_SIZE = 40960;
    std::unique_ptr<char[]> ptyReadBuffer_;
};

} // namespace yetty
