#pragma once

#include "Grid.h"
#include "Font.h"

extern "C" {
#include <vterm.h>
}

#include <string>
#include <functional>

namespace yetty {

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
    void sendKey(uint32_t codepoint);
    void sendSpecialKey(VTermKey key, VTermModifier mod = VTERM_MOD_NONE);

    // Resize the terminal
    void resize(uint32_t cols, uint32_t rows);

    // Get the grid for rendering
    const Grid& getGrid() const { return grid_; }

    // Check if terminal is still running
    bool isRunning() const { return running_; }

    // Get cursor position
    int getCursorRow() const { return cursorRow_; }
    int getCursorCol() const { return cursorCol_; }
    bool isCursorVisible() const { return cursorVisible_; }

    // Static callbacks for libvterm (must be public for C callback)
    static int onDamage(VTermRect rect, void* user);
    static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user);
    static int onResize(int rows, int cols, void* user);
    static int onBell(void* user);

private:
    // Sync libvterm screen to our Grid
    void syncToGrid();

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
    bool cursorVisible_ = true;

    uint32_t cols_;
    uint32_t rows_;
};

} // namespace yetty
