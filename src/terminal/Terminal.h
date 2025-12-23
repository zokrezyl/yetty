#pragma once

#include "Grid.h"
#include "Font.h"
#include "../Config.h"

extern "C" {
#include <vterm.h>
}

#include <string>
#include <vector>

namespace yetty {

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
    bool start(const std::string& shell = "");

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
    void updateCursorBlink(double currentTime);

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
};

} // namespace yetty
