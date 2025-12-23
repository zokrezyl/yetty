#include "Terminal.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <pty.h>
#include <signal.h>

namespace yetty {

// Static callbacks for libvterm
static VTermScreenCallbacks screenCallbacks = {
    .damage = Terminal::onDamage,
    .moverect = nullptr,
    .movecursor = Terminal::onMoveCursor,
    .settermprop = nullptr,
    .bell = Terminal::onBell,
    .resize = Terminal::onResize,
    .sb_pushline = nullptr,
    .sb_popline = nullptr,
    .sb_clear = nullptr,
};

Terminal::Terminal(uint32_t cols, uint32_t rows, Font* font)
    : grid_(cols, rows), font_(font), cols_(cols), rows_(rows) {

    // Create libvterm
    vterm_ = vterm_new(rows, cols);
    vterm_set_utf8(vterm_, 1);

    // Get the screen layer
    vtermScreen_ = vterm_obtain_screen(vterm_);
    vterm_screen_set_callbacks(vtermScreen_, &screenCallbacks, this);
    vterm_screen_reset(vtermScreen_, 1);
}

Terminal::~Terminal() {
    if (ptyMaster_ >= 0) {
        close(ptyMaster_);
    }

    if (childPid_ > 0) {
        kill(childPid_, SIGTERM);
        waitpid(childPid_, nullptr, 0);
    }

    if (vterm_) {
        vterm_free(vterm_);
    }
}

bool Terminal::start(const std::string& shell) {
    // Get shell from environment if not specified
    std::string shellPath = shell;
    if (shellPath.empty()) {
        const char* envShell = getenv("SHELL");
        shellPath = envShell ? envShell : "/bin/sh";
    }

    // Create PTY
    struct winsize ws;
    ws.ws_row = rows_;
    ws.ws_col = cols_;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    childPid_ = forkpty(&ptyMaster_, nullptr, nullptr, &ws);

    if (childPid_ < 0) {
        std::cerr << "forkpty failed: " << strerror(errno) << std::endl;
        return false;
    }

    if (childPid_ == 0) {
        // Child process - exec the shell
        setenv("TERM", "xterm-256color", 1);

        // Close all file descriptors except stdin/stdout/stderr
        for (int fd = 3; fd < 1024; fd++) {
            close(fd);
        }

        execlp(shellPath.c_str(), shellPath.c_str(), nullptr);
        _exit(1);  // Only reached if exec fails
    }

    // Parent process
    // Set PTY to non-blocking
    int flags = fcntl(ptyMaster_, F_GETFL, 0);
    fcntl(ptyMaster_, F_SETFL, flags | O_NONBLOCK);

    running_ = true;
    std::cout << "Terminal started with shell: " << shellPath << std::endl;
    std::cout << "PTY master fd: " << ptyMaster_ << ", child PID: " << childPid_ << std::endl;

    return true;
}

void Terminal::update() {
    if (!running_ || ptyMaster_ < 0) return;

    // Check if child is still running
    int status;
    pid_t result = waitpid(childPid_, &status, WNOHANG);
    if (result > 0) {
        running_ = false;
        std::cout << "Shell exited with status: " << WEXITSTATUS(status) << std::endl;
        return;
    }

    // Read from PTY
    char buf[4096];
    ssize_t nread = read(ptyMaster_, buf, sizeof(buf));

    if (nread > 0) {
        // Feed data to libvterm
        vterm_input_write(vterm_, buf, nread);
        vterm_screen_flush_damage(vtermScreen_);

        // Sync based on damage tracking config
        if (config_ && config_->useDamageTracking && !fullDamage_) {
            syncDamageToGrid();
        } else {
            syncToGrid();
            fullDamage_ = false;
        }
    } else if (nread < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        running_ = false;
    }
}

void Terminal::sendKey(uint32_t codepoint) {
    if (!running_ || ptyMaster_ < 0) return;

    // Send to libvterm which will output to PTY via output callback
    vterm_keyboard_unichar(vterm_, codepoint, VTERM_MOD_NONE);

    // Read vterm output buffer and write to PTY
    size_t buflen = vterm_output_get_buffer_current(vterm_);
    if (buflen > 0) {
        char buf[256];
        size_t len = vterm_output_read(vterm_, buf, sizeof(buf));
        if (len > 0) {
            ssize_t written = write(ptyMaster_, buf, len);
            (void)written;  // Ignore errors for now
        }
    }
}

void Terminal::sendSpecialKey(VTermKey key, VTermModifier mod) {
    if (!running_ || ptyMaster_ < 0) return;

    vterm_keyboard_key(vterm_, key, mod);

    // Read vterm output buffer and write to PTY
    size_t buflen = vterm_output_get_buffer_current(vterm_);
    if (buflen > 0) {
        char buf[256];
        size_t len = vterm_output_read(vterm_, buf, sizeof(buf));
        if (len > 0) {
            ssize_t written = write(ptyMaster_, buf, len);
            (void)written;  // Ignore errors for now
        }
    }
}

void Terminal::resize(uint32_t cols, uint32_t rows) {
    cols_ = cols;
    rows_ = rows;

    vterm_set_size(vterm_, rows, cols);
    grid_.resize(cols, rows);

    // Update PTY window size
    if (ptyMaster_ >= 0) {
        struct winsize ws;
        ws.ws_row = rows;
        ws.ws_col = cols;
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;
        ioctl(ptyMaster_, TIOCSWINSZ, &ws);
    }

    syncToGrid();
}

void Terminal::syncToGrid() {
    VTermScreenCell cell;
    VTermPos pos;

    for (uint32_t row = 0; row < rows_; row++) {
        for (uint32_t col = 0; col < cols_; col++) {
            pos.row = row;
            pos.col = col;

            vterm_screen_get_cell(vtermScreen_, pos, &cell);

            // Get glyph index
            uint32_t codepoint = cell.chars[0];
            if (codepoint == 0) codepoint = ' ';

            uint16_t glyphIndex = font_ ? font_->getGlyphIndex(codepoint) : static_cast<uint16_t>(codepoint);

            // Convert colors
            uint8_t fgR, fgG, fgB;
            uint8_t bgR, bgG, bgB;

            VTermColor fg = cell.fg;
            VTermColor bg = cell.bg;

            // Convert indexed colors to RGB
            vterm_screen_convert_color_to_rgb(vtermScreen_, &fg);
            vterm_screen_convert_color_to_rgb(vtermScreen_, &bg);

            colorToRGB(fg, fgR, fgG, fgB);
            colorToRGB(bg, bgR, bgG, bgB);

            // Handle reverse video
            if (cell.attrs.reverse) {
                std::swap(fgR, bgR);
                std::swap(fgG, bgG);
                std::swap(fgB, bgB);
            }

            grid_.setCell(col, row, glyphIndex, fgR, fgG, fgB, bgR, bgG, bgB);
        }
    }

    // Clear damage rects since we did a full sync
    damageRects_.clear();
}

void Terminal::syncDamageToGrid() {
    if (damageRects_.empty()) return;

    VTermScreenCell cell;
    VTermPos pos;

    for (const auto& damage : damageRects_) {
        for (uint32_t row = damage.startRow; row < damage.endRow && row < rows_; row++) {
            for (uint32_t col = damage.startCol; col < damage.endCol && col < cols_; col++) {
                pos.row = row;
                pos.col = col;

                vterm_screen_get_cell(vtermScreen_, pos, &cell);

                // Get glyph index
                uint32_t codepoint = cell.chars[0];
                if (codepoint == 0) codepoint = ' ';

                uint16_t glyphIndex = font_ ? font_->getGlyphIndex(codepoint) : static_cast<uint16_t>(codepoint);

                // Convert colors
                uint8_t fgR, fgG, fgB;
                uint8_t bgR, bgG, bgB;

                VTermColor fg = cell.fg;
                VTermColor bg = cell.bg;

                vterm_screen_convert_color_to_rgb(vtermScreen_, &fg);
                vterm_screen_convert_color_to_rgb(vtermScreen_, &bg);

                colorToRGB(fg, fgR, fgG, fgB);
                colorToRGB(bg, bgR, bgG, bgB);

                if (cell.attrs.reverse) {
                    std::swap(fgR, bgR);
                    std::swap(fgG, bgG);
                    std::swap(fgB, bgB);
                }

                grid_.setCell(col, row, glyphIndex, fgR, fgG, fgB, bgR, bgG, bgB);
            }
        }
    }
    // Note: don't clear damageRects_ here - TextRenderer needs them for partial updates
}

void Terminal::colorToRGB(const VTermColor& color, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (VTERM_COLOR_IS_DEFAULT_FG(&color)) {
        // Default foreground: white
        r = 255; g = 255; b = 255;
    } else if (VTERM_COLOR_IS_DEFAULT_BG(&color)) {
        // Default background: black
        r = 0; g = 0; b = 0;
    } else if (VTERM_COLOR_IS_RGB(&color)) {
        r = color.rgb.red;
        g = color.rgb.green;
        b = color.rgb.blue;
    } else {
        // Fallback for indexed colors (shouldn't happen after convert_to_rgb)
        r = 128; g = 128; b = 128;
    }
}

int Terminal::onDamage(VTermRect rect, void* user) {
    Terminal* term = static_cast<Terminal*>(user);

    // Record the damage rectangle
    DamageRect damage;
    damage.startCol = static_cast<uint32_t>(rect.start_col);
    damage.startRow = static_cast<uint32_t>(rect.start_row);
    damage.endCol = static_cast<uint32_t>(rect.end_col);
    damage.endRow = static_cast<uint32_t>(rect.end_row);
    term->damageRects_.push_back(damage);

    if (term->config_ && term->config_->debugDamageRects) {
        std::cout << "Damage: [" << damage.startCol << "," << damage.startRow
                  << "] -> [" << damage.endCol << "," << damage.endRow << "]" << std::endl;
    }

    return 0;
}

int Terminal::onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user) {
    Terminal* term = static_cast<Terminal*>(user);
    term->cursorRow_ = pos.row;
    term->cursorCol_ = pos.col;
    term->cursorVisible_ = visible;
    (void)oldpos;
    return 0;
}

int Terminal::onResize(int rows, int cols, void* user) {
    (void)rows;
    (void)cols;
    (void)user;
    return 0;
}

int Terminal::onBell(void* user) {
    (void)user;
    std::cout << '\a' << std::flush;  // Terminal bell
    return 0;
}

} // namespace yetty
