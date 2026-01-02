#pragma once

#include "yetty/grid.h"
#include <yetty/font.h>
#include "yetty/config.h"
#include <yetty/result.hpp>

extern "C" {
#include <vterm.h>
}

#include <string>
#include <vector>
#include <deque>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#endif

namespace yetty {

// A single line in the scrollback buffer
struct ScrollbackLine {
    std::vector<uint32_t> _chars;      // Unicode codepoints
    std::vector<uint8_t> _fgColors;    // RGB per cell (3 bytes each)
    std::vector<uint8_t> _bgColors;    // RGB per cell (3 bytes each)
};

// Forward declarations
class PluginManager;
class EmojiAtlas;

// Selection mode for mouse selection
enum class SelectionMode {
    None,
    Character,  // Single click drag
    Word,       // Double click
    Line        // Triple click
};

// Rectangle representing damaged (changed) cells
struct DamageRect {
    uint32_t _startCol, _startRow;
    uint32_t _endCol, _endRow;  // exclusive
};

// Terminal class that wraps libvterm with PTY support
class Terminal {
public:
    using Ptr = std::shared_ptr<Terminal>;

    static Result<Ptr> create(uint32_t cols, uint32_t rows, Font* font) noexcept;

    ~Terminal();

    // Non-copyable
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    // Start the terminal with a shell
    Result<void> start(const std::string& shell = "");

    // Process input from PTY (call regularly)
    Result<void> update();

    // Send keyboard input to the terminal
    Result<void> sendKey(uint32_t codepoint, VTermModifier mod = VTERM_MOD_NONE);
    Result<void> sendSpecialKey(VTermKey key, VTermModifier mod = VTERM_MOD_NONE);

    // Send raw bytes directly to the PTY (for control characters)
    Result<void> sendRaw(const char* data, size_t len);

    // Resize the terminal
    void resize(uint32_t cols, uint32_t rows);

    // Get the grid for rendering
    const Grid& getGrid() const { return grid_; }

    // Check if terminal is still running
    bool isRunning() const { return running_; }

    // Get cursor position
    int getCursorRow() const { return cursorRow_; }
    int getCursorCol() const { return cursorCol_; }
    bool isCursorVisible() const { return cursorVisible_ && cursorBlink_; }

    // Update cursor blink state (call each frame with current time)
    // Returns Ok(true) if blink state changed, Ok(false) otherwise
    Result<bool> updateCursorBlink(double currentTime);

    // Damage tracking
    const std::vector<DamageRect>& getDamageRects() const { return damageRects_; }
    void clearDamageRects() { damageRects_.clear(); }
    bool hasDamage() const { return !damageRects_.empty() || fullDamage_; }
    bool hasFullDamage() const { return fullDamage_; }
    void clearFullDamage() { fullDamage_ = false; }

    // Configuration
    void setConfig(const Config* config) { config_ = config; }

    // Alternate screen state
    bool isAltScreen() const { return isAltScreen_; }

    // Static callbacks for libvterm (must be public for C callback)
    static int onDamage(VTermRect rect, void* user);
    static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user);
    static int onSetTermProp(VTermProp prop, VTermValue* val, void* user);
    static int onResize(int rows, int cols, void* user);
    static int onBell(void* user);
    static int onOSC(int command, VTermStringFragment frag, void* user);
    static int onSbPushline(int cols, const VTermScreenCell* cells, void* user);
    static int onSbPopline(int cols, VTermScreenCell* cells, void* user);
    static int onMoverect(VTermRect dest, VTermRect src, void* user);

    // Scrollback navigation
    void scrollUp(int lines = 1);
    void scrollDown(int lines = 1);
    void scrollToTop();
    void scrollToBottom();
    int getScrollOffset() const { return scrollOffset_; }
    bool isScrolledBack() const { return scrollOffset_ > 0; }
    size_t getScrollbackSize() const { return scrollback_.size(); }

    // Plugin support
    void setPluginManager(PluginManager* mgr) { pluginManager_ = mgr; }
    PluginManager* getPluginManager() const { return pluginManager_; }

    // Emoji atlas for dynamic emoji loading
    void setEmojiAtlas(EmojiAtlas* atlas) { emojiAtlas_ = atlas; }
    void setCellSize(uint32_t width, uint32_t height) {
        cellWidth_ = width;
        cellHeight_ = height;
    }
    uint32_t getCellWidth() const { return cellWidth_; }
    uint32_t getCellHeight() const { return cellHeight_; }

    // Get mutable grid for plugin cell marking
    Grid& getGridMutable() { return grid_; }

    // Selection support
    void startSelection(int row, int col, SelectionMode mode = SelectionMode::Character);
    void extendSelection(int row, int col);
    void clearSelection();
    bool hasSelection() const { return selectionMode_ != SelectionMode::None; }
    bool isInSelection(int row, int col) const;
    std::string getSelectedText() const;

    // Mouse mode (for apps like vim that want mouse events)
    int getMouseMode() const { return mouseMode_; }
    bool wantsMouseEvents() const { return mouseMode_ != VTERM_PROP_MOUSE_NONE; }

    // Get VTerm screen for text extraction
    VTermScreen* getVTermScreen() const { return vtermScreen_; }

private:
    Terminal(uint32_t cols, uint32_t rows, Font* font) noexcept;
    Result<void> init() noexcept;

    // Write to PTY with error handling
    Result<void> writeToPty(const char* data, size_t len);

    // Flush libvterm output buffer to PTY
    Result<void> flushVtermOutput();

    // Sync libvterm screen to our Grid
    void syncToGrid();
    void syncDamageToGrid();  // Only sync damaged regions

    // Convert VTermColor to RGB
    void colorToRGB(const VTermColor& color, uint8_t& r, uint8_t& g, uint8_t& b);

    VTerm* vterm_ = nullptr;
    VTermScreen* vtermScreen_ = nullptr;

    Grid grid_;
    Font* font_;

#ifdef _WIN32
    // Windows ConPTY handles
    HPCON hPC_ = INVALID_HANDLE_VALUE;
    HANDLE hPipeIn_ = INVALID_HANDLE_VALUE;   // Read from child
    HANDLE hPipeOut_ = INVALID_HANDLE_VALUE;  // Write to child
    HANDLE hProcess_ = INVALID_HANDLE_VALUE;
    HANDLE hThread_ = INVALID_HANDLE_VALUE;
#else
    // Unix PTY
    int ptyMaster_ = -1;
    pid_t childPid_ = -1;
#endif
    bool running_ = false;

    int cursorRow_ = 0;
    int cursorCol_ = 0;
    bool cursorVisible_ = true;   // From libvterm (cursor shown/hidden by escape codes)
    bool cursorBlink_ = true;     // Blink state (toggled by timer)
    bool isAltScreen_ = false;    // True when on alternate screen (vim, less, etc.)
    double lastBlinkTime_ = 0.0;
    double blinkInterval_ = 0.5;  // 500ms blink interval

    uint32_t cols_;
    uint32_t rows_;

    // Damage tracking
    std::vector<DamageRect> damageRects_;
    bool fullDamage_ = true;  // Start with full damage to render initial content

    const Config* config_ = nullptr;

    // Plugin support
    PluginManager* pluginManager_ = nullptr;
    uint32_t cellWidth_ = 10;   // Default, will be set by renderer
    uint32_t cellHeight_ = 20;

    // Emoji atlas for dynamic loading
    EmojiAtlas* emojiAtlas_ = nullptr;

    // OSC sequence buffer (for multi-fragment sequences)
    std::string oscBuffer_;
    int oscCommand_ = -1;

    // Scrollback buffer
    std::deque<ScrollbackLine> scrollback_;
    int scrollOffset_ = 0;  // 0 = at bottom, >0 = scrolled back N lines

    // Deferred newlines for plugin activation (can't feed during OSC callback)
    uint32_t pendingNewlines_ = 0;

    // Selection state
    VTermPos selectionStart_ = {0, 0};
    VTermPos selectionEnd_ = {0, 0};
    SelectionMode selectionMode_ = SelectionMode::None;

    // Mouse mode (apps can request mouse events)
    int mouseMode_ = VTERM_PROP_MOUSE_NONE;
};

} // namespace yetty
