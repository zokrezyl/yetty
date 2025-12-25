#include "Terminal.h"
#include "../PluginManager.h"

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

// Static callbacks for libvterm screen layer
static VTermScreenCallbacks screenCallbacks = {
    .damage = Terminal::onDamage,
    .moverect = Terminal::onMoverect,
    .movecursor = Terminal::onMoveCursor,
    .settermprop = nullptr,
    .bell = Terminal::onBell,
    .resize = Terminal::onResize,
    .sb_pushline = Terminal::onSbPushline,
    .sb_popline = Terminal::onSbPopline,
    .sb_clear = nullptr,
};

// Static fallback callbacks for unrecognized sequences (OSC for plugins)
static VTermStateFallbacks stateFallbacks = {
    .control = nullptr,
    .csi = nullptr,
    .osc = Terminal::onOSC,
    .dcs = nullptr,
    .apc = nullptr,
    .pm = nullptr,
    .sos = nullptr,
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

    // Set up fallback callbacks for unrecognized OSC sequences (plugins)
    VTermState* state = vterm_obtain_state(vterm_);
    vterm_state_set_unrecognised_fallbacks(state, &stateFallbacks, this);
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

Result<void> Terminal::start(const std::string& shell) {
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
        return Err<void>(std::string("forkpty failed: ") + strerror(errno));
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

    return Ok();
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

        // Flush any output libvterm generated (e.g., cursor position responses)
        size_t outlen = vterm_output_get_buffer_current(vterm_);
        if (outlen > 0) {
            char outbuf[256];
            while (outlen > 0) {
                size_t n = vterm_output_read(vterm_, outbuf, sizeof(outbuf));
                if (n > 0) {
                    (void)write(ptyMaster_, outbuf, n);
                }
                outlen = vterm_output_get_buffer_current(vterm_);
            }
        }

        // Sync based on damage tracking config
        // When scrolled back, always use full sync since damage rects don't apply to scrollback
        if (config_ && config_->useDamageTracking && !fullDamage_ && scrollOffset_ == 0) {
            syncDamageToGrid();
        } else {
            syncToGrid();
            // Note: don't clear fullDamage_ here - main loop clears after rendering
        }
    } else if (nread < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        running_ = false;
    } else if (fullDamage_) {
        // No new data but need to sync (e.g., after scrolling)
        syncToGrid();
        // Note: don't clear fullDamage_ here - main loop clears after rendering
    }
}

void Terminal::sendKey(uint32_t codepoint, VTermModifier mod) {
    if (!running_ || ptyMaster_ < 0) return;

    // Flush any accumulated output first (e.g., responses to escape sequences)
    size_t pending = vterm_output_get_buffer_current(vterm_);
    if (pending > 0) {
        std::cerr << "    Flushing " << pending << " pending bytes before key" << std::endl;
        char discard[256];
        while (pending > 0) {
            size_t n = vterm_output_read(vterm_, discard, sizeof(discard));
            if (n > 0) (void)write(ptyMaster_, discard, n);
            pending = vterm_output_get_buffer_current(vterm_);
        }
    }

    // Send to libvterm which will generate output
    vterm_keyboard_unichar(vterm_, codepoint, mod);

    // Read the keyboard output and write to PTY
    size_t buflen = vterm_output_get_buffer_current(vterm_);
    if (buflen > 0) {
        char buf[256];
        size_t len = vterm_output_read(vterm_, buf, sizeof(buf));
        if (len > 0) {
            std::cerr << "    PTY write:";
            for (size_t i = 0; i < len; i++) {
                std::cerr << " 0x" << std::hex << (int)(unsigned char)buf[i] << std::dec;
            }
            std::cerr << std::endl;
            (void)write(ptyMaster_, buf, len);
        }
    }
}

void Terminal::sendRaw(const char* data, size_t len) {
    if (!running_ || ptyMaster_ < 0 || len == 0) return;

    ssize_t written = write(ptyMaster_, data, len);
    if (written < 0) {
        std::cerr << "Failed to write to PTY: " << strerror(errno) << std::endl;
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

    int sbSize = static_cast<int>(scrollback_.size());
    int effectiveOffset = std::min(scrollOffset_, sbSize);

    for (uint32_t row = 0; row < rows_; row++) {
        // Calculate which line this row maps to
        int lineIndex = static_cast<int>(row) - effectiveOffset;

        if (lineIndex < 0) {
            // This row shows scrollback content
            int sbIndex = sbSize + lineIndex;  // Index from end of scrollback
            if (sbIndex >= 0 && sbIndex < sbSize) {
                const ScrollbackLine& sbLine = scrollback_[sbIndex];
                for (uint32_t col = 0; col < cols_; col++) {
                    uint32_t codepoint = ' ';
                    uint8_t fgR = 255, fgG = 255, fgB = 255;
                    uint8_t bgR = 0, bgG = 0, bgB = 0;

                    if (col < sbLine.chars.size()) {
                        codepoint = sbLine.chars[col];
                        fgR = sbLine.fgColors[col * 3 + 0];
                        fgG = sbLine.fgColors[col * 3 + 1];
                        fgB = sbLine.fgColors[col * 3 + 2];
                        bgR = sbLine.bgColors[col * 3 + 0];
                        bgG = sbLine.bgColors[col * 3 + 1];
                        bgB = sbLine.bgColors[col * 3 + 2];
                    }

                    uint16_t glyphIndex = font_ ? font_->getGlyphIndex(codepoint) : static_cast<uint16_t>(codepoint);
                    grid_.setCell(col, row, glyphIndex, fgR, fgG, fgB, bgR, bgG, bgB);
                }
            }
        } else {
            // This row shows vterm screen content
            for (uint32_t col = 0; col < cols_; col++) {
                pos.row = lineIndex;
                pos.col = col;

                vterm_screen_get_cell(vtermScreen_, pos, &cell);

                uint32_t codepoint = cell.chars[0];
                if (codepoint == 0) codepoint = ' ';

                uint16_t glyphIndex = font_ ? font_->getGlyphIndex(codepoint) : static_cast<uint16_t>(codepoint);

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
    // Note: don't clear damage here - main loop clears after rendering
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
    // visible: 1 = visible, 0 = invisible, -1 = no change (cursor hidden by DECCM)
    // Only hide cursor if explicitly set to 0
    if (visible == 0) {
        term->cursorVisible_ = false;
    } else if (visible > 0) {
        term->cursorVisible_ = true;
    }
    // If visible == -1, leave cursorVisible_ unchanged
    (void)oldpos;
    return 0;
}

Result<bool> Terminal::updateCursorBlink(double currentTime) {
    if (currentTime - lastBlinkTime_ >= blinkInterval_) {
        cursorBlink_ = !cursorBlink_;
        lastBlinkTime_ = currentTime;
        return Ok(true);  // Blink state changed
    }
    return Ok(false);
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

int Terminal::onSbPushline(int cols, const VTermScreenCell* cells, void* user) {
    Terminal* term = static_cast<Terminal*>(user);

    // Store line in scrollback buffer
    ScrollbackLine line;
    line.chars.resize(cols);
    line.fgColors.resize(cols * 3);
    line.bgColors.resize(cols * 3);

    for (int i = 0; i < cols; i++) {
        const VTermScreenCell& cell = cells[i];
        line.chars[i] = cell.chars[0] ? cell.chars[0] : ' ';

        // Convert colors
        VTermColor fg = cell.fg;
        VTermColor bg = cell.bg;
        vterm_screen_convert_color_to_rgb(term->vtermScreen_, &fg);
        vterm_screen_convert_color_to_rgb(term->vtermScreen_, &bg);

        line.fgColors[i * 3 + 0] = fg.rgb.red;
        line.fgColors[i * 3 + 1] = fg.rgb.green;
        line.fgColors[i * 3 + 2] = fg.rgb.blue;
        line.bgColors[i * 3 + 0] = bg.rgb.red;
        line.bgColors[i * 3 + 1] = bg.rgb.green;
        line.bgColors[i * 3 + 2] = bg.rgb.blue;
    }

    term->scrollback_.push_back(std::move(line));

    // Trim scrollback if too large
    uint32_t maxLines = term->config_ ? term->config_->scrollbackLines : 10000;
    while (term->scrollback_.size() > maxLines) {
        term->scrollback_.pop_front();
    }

    // Notify plugin manager that content scrolled up by 1 line
    if (term->pluginManager_) {
        term->pluginManager_->onScroll(1);
    }

    return 1;  // We stored the line
}

int Terminal::onSbPopline(int cols, VTermScreenCell* cells, void* user) {
    Terminal* term = static_cast<Terminal*>(user);

    if (term->scrollback_.empty()) {
        return 0;  // No lines to pop
    }

    ScrollbackLine& line = term->scrollback_.back();
    int lineCols = std::min(cols, static_cast<int>(line.chars.size()));

    for (int i = 0; i < lineCols; i++) {
        cells[i].chars[0] = line.chars[i];
        cells[i].chars[1] = 0;
        cells[i].width = 1;

        cells[i].fg.type = VTERM_COLOR_RGB;
        cells[i].fg.rgb.red = line.fgColors[i * 3 + 0];
        cells[i].fg.rgb.green = line.fgColors[i * 3 + 1];
        cells[i].fg.rgb.blue = line.fgColors[i * 3 + 2];

        cells[i].bg.type = VTERM_COLOR_RGB;
        cells[i].bg.rgb.red = line.bgColors[i * 3 + 0];
        cells[i].bg.rgb.green = line.bgColors[i * 3 + 1];
        cells[i].bg.rgb.blue = line.bgColors[i * 3 + 2];
    }

    // Fill remaining cells with spaces
    for (int i = lineCols; i < cols; i++) {
        cells[i].chars[0] = ' ';
        cells[i].chars[1] = 0;
        cells[i].width = 1;
        cells[i].fg.type = VTERM_COLOR_DEFAULT_FG;
        cells[i].bg.type = VTERM_COLOR_DEFAULT_BG;
    }

    term->scrollback_.pop_back();
    return 1;  // We restored the line
}

int Terminal::onMoverect(VTermRect dest, VTermRect src, void* user) {
    Terminal* term = static_cast<Terminal*>(user);

    // Mark as full damage for now - GPU texture shift optimization later
    term->fullDamage_ = true;

    return 0;  // Let libvterm handle it with damage
}

int Terminal::onOSC(int command, VTermStringFragment frag, void* user) {
    Terminal* term = static_cast<Terminal*>(user);

    // Check if this is our vendor ID
    if (command != YETTY_OSC_VENDOR_ID) {
        return 0;  // Not our sequence, let someone else handle it
    }

    // Handle multi-fragment sequences
    if (frag.initial) {
        term->oscBuffer_.clear();
        term->oscBuffer_.reserve(256 * 1024);  // Pre-allocate for large payloads
        term->oscCommand_ = command;
    }

    // Append fragment data
    if (frag.len > 0) {
        term->oscBuffer_.append(frag.str, frag.len);
    }

    // Process when complete
    if (frag.final) {
        if (term->pluginManager_) {
            // Build full sequence: command;rest
            std::string fullSeq = std::to_string(command) + ";" + term->oscBuffer_;
            std::string response;

            bool handled = term->pluginManager_->handleOSCSequence(
                fullSeq,
                &term->grid_,
                term->cursorCol_,
                term->cursorRow_,
                term->cellWidth_,
                term->cellHeight_,
                &response
            );

            if (handled) {
                term->fullDamage_ = true;  // Force full redraw
                // Write query response back to PTY if any
                if (!response.empty() && term->ptyMaster_ >= 0) {
                    (void)write(term->ptyMaster_, response.c_str(), response.size());
                }
            }
        }
        term->oscBuffer_.clear();
        term->oscCommand_ = -1;
    }

    return 1;  // We handled it
}

void Terminal::scrollUp(int lines) {
    int maxOffset = static_cast<int>(scrollback_.size());
    scrollOffset_ = std::min(scrollOffset_ + lines, maxOffset);
    fullDamage_ = true;  // Need to redraw with scrollback content
}

void Terminal::scrollDown(int lines) {
    scrollOffset_ = std::max(scrollOffset_ - lines, 0);
    fullDamage_ = true;  // Need to redraw
}

void Terminal::scrollToTop() {
    scrollOffset_ = static_cast<int>(scrollback_.size());
    fullDamage_ = true;
}

void Terminal::scrollToBottom() {
    scrollOffset_ = 0;
    fullDamage_ = true;
}

} // namespace yetty
