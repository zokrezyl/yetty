#pragma once

#include "Grid.h"
#include "Font.h"
#include "../Config.h"
#include "../result.hpp"

extern "C" {
#include <vterm.h>
}

#include <string>
#include <vector>
#include <deque>

namespace yetty {

// A single line in the scrollback buffer
struct ScrollbackLine {
    std::vector<uint32_t> chars;      // Unicode codepoints
    std::vector<uint8_t> fgColors;    // RGB per cell (3 bytes each)
    std::vector<uint8_t> bgColors;    // RGB per cell (3 bytes each)
};

// Forward declaration
class PluginManager;

// Rectangle representing damaged (changed) cells
struct DamageRect {
    uint32_t startCol, startRow;
    uint32_t endCol, endRow;  // exclusive
};

// Terminal class that wraps libvterm with PTY support
class Terminal {
public:
    Terminal(uint32_t cols, uint32_t rows, Font* font);
    ~Terminal();

    // Start the terminal with a shell
    Result<void> start(const std::string& shell = "");

    // Process input from PTY (call regularly)
    void update();

    // Send keyboard input to the terminal
    void sendKey(uint32_t codepoint, VTermModifier mod = VTERM_MOD_NONE);
    void sendSpecialKey(VTermKey key, VTermModifier mod = VTERM_MOD_NONE);

    // Send raw bytes directly to the PTY (for control characters)
    void sendRaw(const char* data, size_t len);

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

    // Static callbacks for libvterm (must be public for C callback)
    static int onDamage(VTermRect rect, void* user);
    static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user);
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
    void setCellSize(uint32_t width, uint32_t height) {
        cellWidth_ = width;
        cellHeight_ = height;
    }
    uint32_t getCellWidth() const { return cellWidth_; }
    uint32_t getCellHeight() const { return cellHeight_; }

    // Get mutable grid for plugin cell marking
    Grid& getGridMutable() { return grid_; }

private:
    // Sync libvterm screen to our Grid
    void syncToGrid();
    void syncDamageToGrid();  // Only sync damaged regions

    // Convert VTermColor to RGB
    void colorToRGB(const VTermColor& color, uint8_t& r, uint8_t& g, uint8_t& b);

    VTerm* vterm_ = nullptr;
    VTermScreen* vtermScreen_ = nullptr;

    Grid grid_;
    Font* font_;

    int ptyMaster_ = -1;
    pid_t childPid_ = -1;
    bool running_ = false;

    int cursorRow_ = 0;
    int cursorCol_ = 0;
    bool cursorVisible_ = true;   // From libvterm (cursor shown/hidden by escape codes)
    bool cursorBlink_ = true;     // Blink state (toggled by timer)
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

    // OSC sequence buffer (for multi-fragment sequences)
    std::string oscBuffer_;
    int oscCommand_ = -1;

    // Scrollback buffer
    std::deque<ScrollbackLine> scrollback_;
    int scrollOffset_ = 0;  // 0 = at bottom, >0 = scrolled back N lines

    // Deferred newlines for plugin activation (can't feed during OSC callback)
    uint32_t pendingNewlines_ = 0;
};

} // namespace yetty
