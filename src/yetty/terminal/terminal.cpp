#include "terminal.h"
#include "../plugin-manager.h"

#include <spdlog/spdlog.h>
#include <sstream>
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
    .settermprop = Terminal::onSetTermProp,
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
    vterm_screen_enable_altscreen(vtermScreen_, 1);  // Enable alternate screen support
    vterm_screen_enable_reflow(vtermScreen_, true);  // Enable text reflow on resize
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

    spdlog::debug("Starting shell: {}", shellPath);
    spdlog::debug("Terminal size: {}x{}", cols_, rows_);

    // Create PTY
    struct winsize ws;
    ws.ws_row = rows_;
    ws.ws_col = cols_;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    spdlog::debug("Calling forkpty...");
    childPid_ = forkpty(&ptyMaster_, nullptr, nullptr, &ws);

    if (childPid_ < 0) {
        spdlog::error("forkpty failed: {} (errno={})", strerror(errno), errno);
        return Err<void>(std::string("forkpty failed: ") + strerror(errno));
    }

    if (childPid_ == 0) {
        // Child process - exec the shell
        setenv("TERM", "xterm-256color", 1);

        // Close all file descriptors except stdin/stdout/stderr
        for (int fd = 3; fd < 1024; fd++) {
            close(fd);
        }

        // Parse shell command into program and arguments
        // e.g., "/path/to/busybox ash" -> program="/path/to/busybox", argv=["ash", NULL]
        // For busybox: argv[0] must be the applet name, not the path
        std::vector<std::string> parts;
        std::istringstream iss(shellPath);
        std::string part;
        while (iss >> part) {
            parts.push_back(part);
        }

        if (parts.empty()) {
            fprintf(stderr, "exec failed: empty shell command\n");
            _exit(1);
        }

        std::string program = parts[0];

        // Build argv array
        // For busybox-style multi-call binaries: if there are multiple args,
        // use args[1..] as argv (so argv[0] is the applet name like "ash")
        std::vector<char*> argv;
        if (parts.size() > 1) {
            // Multi-call binary: argv[0] = applet name (e.g., "ash")
            for (size_t i = 1; i < parts.size(); i++) {
                argv.push_back(const_cast<char*>(parts[i].c_str()));
            }
        } else {
            // Single command: argv[0] = program path
            argv.push_back(const_cast<char*>(parts[0].c_str()));
        }
        argv.push_back(nullptr);

        // Execute
        execv(program.c_str(), argv.data());

        // If exec fails, write error to stderr (which goes to PTY)
        fprintf(stderr, "exec failed: %s\n", strerror(errno));
        _exit(1);  // Only reached if exec fails
    }

    // Parent process
    spdlog::debug("forkpty succeeded: ptyMaster={}, childPid={}", ptyMaster_, childPid_);

    // Set PTY to non-blocking
    int flags = fcntl(ptyMaster_, F_GETFL, 0);
    fcntl(ptyMaster_, F_SETFL, flags | O_NONBLOCK);

    running_ = true;
    spdlog::info("Terminal started with shell: {}, PTY fd: {}, child PID: {}",
                 shellPath, ptyMaster_, childPid_);

    return Ok();
}

Result<void> Terminal::writeToPty(const char* data, size_t len) {
    if (ptyMaster_ < 0) {
        return Err("PTY not open");
    }
    if (len == 0) {
        return Ok();
    }

    ssize_t written = write(ptyMaster_, data, len);
    if (written < 0) {
        return Err("PTY write failed: " + std::string(strerror(errno)));
    }
    if (static_cast<size_t>(written) != len) {
        return Err("PTY write incomplete: wrote " + std::to_string(written) +
                   " of " + std::to_string(len) + " bytes");
    }
    return Ok();
}

Result<void> Terminal::flushVtermOutput() {
    size_t outlen = vterm_output_get_buffer_current(vterm_);
    if (outlen == 0) {
        return Ok();
    }

    char outbuf[256];
    while (outlen > 0) {
        size_t n = vterm_output_read(vterm_, outbuf, sizeof(outbuf));
        if (n > 0) {
            auto result = writeToPty(outbuf, n);
            if (!result) {
                return result;
            }
        }
        outlen = vterm_output_get_buffer_current(vterm_);
    }
    return Ok();
}

Result<void> Terminal::update() {
    if (!running_ || ptyMaster_ < 0) {
        return Ok();  // Not an error, just nothing to do
    }

    // Check if child is still running
    int status;
    pid_t waitResult = waitpid(childPid_, &status, WNOHANG);
    if (waitResult > 0) {
        running_ = false;
        spdlog::info("Shell exited with status: {}", WEXITSTATUS(status));
        return Ok();  // Shell exited, not an error
    }

    // Read from PTY
    char buf[4096];
    ssize_t nread = read(ptyMaster_, buf, sizeof(buf));

    if (nread > 0) {
        // Feed data to libvterm
        vterm_input_write(vterm_, buf, nread);
        vterm_screen_flush_damage(vtermScreen_);

        // Process any deferred newlines from plugin activation
        // (can't call vterm_input_write during OSC callback)
        if (pendingNewlines_ > 0) {
            std::string newlines(pendingNewlines_, '\n');
            vterm_input_write(vterm_, newlines.c_str(), newlines.size());
            vterm_screen_flush_damage(vtermScreen_);
            pendingNewlines_ = 0;
        }

        // Flush any output libvterm generated (e.g., cursor position responses)
        auto flushResult = flushVtermOutput();
        if (!flushResult) {
            return flushResult;
        }

        // Sync based on damage tracking config
        // When scrolled back, always use full sync since damage rects don't apply to scrollback
        if (config_ && config_->_useDamageTracking && !fullDamage_ && scrollOffset_ == 0) {
            syncDamageToGrid();
        } else {
            syncToGrid();
        }
    } else if (nread < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        spdlog::error("PTY read error: {} (errno={})", strerror(errno), errno);
        running_ = false;
        return Err("PTY read failed: " + std::string(strerror(errno)));
    } else if (nread < 0) {
        // EAGAIN/EWOULDBLOCK - no data available (normal for non-blocking)
        if (fullDamage_) {
            syncToGrid();
        }
    } else if (fullDamage_) {
        // nread == 0 (EOF) but need to sync
        syncToGrid();
    }

    return Ok();
}

Result<void> Terminal::sendKey(uint32_t codepoint, VTermModifier mod) {
    if (!running_ || ptyMaster_ < 0) {
        return Ok();  // Nothing to do
    }

    // Flush any accumulated output first (e.g., responses to escape sequences)
    auto flushResult = flushVtermOutput();
    if (!flushResult) {
        return flushResult;
    }

    // Send to libvterm which will generate output
    vterm_keyboard_unichar(vterm_, codepoint, mod);

    // Flush the keyboard output to PTY
    return flushVtermOutput();
}

Result<void> Terminal::sendRaw(const char* data, size_t len) {
    if (!running_ || ptyMaster_ < 0 || len == 0) {
        return Ok();
    }
    return writeToPty(data, len);
}

Result<void> Terminal::sendSpecialKey(VTermKey key, VTermModifier mod) {
    if (!running_ || ptyMaster_ < 0) {
        return Ok();
    }

    vterm_keyboard_key(vterm_, key, mod);

    // Flush vterm output to PTY
    return flushVtermOutput();
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
    static int syncCount = 0;
    syncCount++;

    if (syncCount <= 5) {
        spdlog::debug("syncToGrid #{}: grid {}x{}", syncCount, cols_, rows_);
    }

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

                    if (col < sbLine._chars.size()) {
                        codepoint = sbLine._chars[col];
                        fgR = sbLine._fgColors[col * 3 + 0];
                        fgG = sbLine._fgColors[col * 3 + 1];
                        fgB = sbLine._fgColors[col * 3 + 2];
                        bgR = sbLine._bgColors[col * 3 + 0];
                        bgG = sbLine._bgColors[col * 3 + 1];
                        bgB = sbLine._bgColors[col * 3 + 2];
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

                // Handle wide character continuation cells (chars[0] == -1 or 0xFFFFFFFF)
                // These are placeholder cells for the right half of wide characters
                if (codepoint == 0xFFFFFFFF || codepoint == static_cast<uint32_t>(-1)) {
                    // Wide char continuation - mark with special glyph so shader can look left
                    spdlog::debug("SyncToGrid ({},{}) wide continuation", col, row);
                    uint8_t fgR, fgG, fgB, bgR, bgG, bgB;
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
                    // Extract text attributes for wide char continuation too
                    CellAttrs wideAttrs;
                    wideAttrs._bold = cell.attrs.bold ? 1 : 0;
                    wideAttrs._italic = cell.attrs.italic ? 1 : 0;
                    wideAttrs._strikethrough = cell.attrs.strike ? 1 : 0;
                    wideAttrs._reserved = 0;
                    wideAttrs._underline = static_cast<uint8_t>(cell.attrs.underline & 0x3);
                    grid_.setCell(col, row, GLYPH_WIDE_CONT, fgR, fgG, fgB, bgR, bgG, bgB, wideAttrs);
                    continue;
                }
                if (codepoint == 0) codepoint = ' ';

                // Extract text attributes from libvterm (needed for glyph selection)
                bool isBold = cell.attrs.bold != 0;
                bool isItalic = cell.attrs.italic != 0;

                // Get glyph index with style (uses bold/italic font variant if available)
                uint16_t glyphIndex = font_ ? font_->getGlyphIndex(codepoint, isBold, isItalic)
                                            : static_cast<uint16_t>(codepoint);

                // Check if this codepoint has a custom glyph plugin
                if (pluginManager_) {
                    uint32_t widthCells = (cell.width > 0) ? cell.width : 1;
                    uint16_t customIdx = pluginManager_->onCellSync(col, row, codepoint, widthCells);
                    if (customIdx != 0) {
                        glyphIndex = customIdx;
                        spdlog::debug("SyncToGrid ({},{}) U+{:04X} -> custom glyph 0x{:04X}",
                                     col, row, codepoint, glyphIndex);
                    }
                }

                // Log high codepoints (emojis, etc.)
                if (codepoint > 0x1000 && !isCustomGlyph(glyphIndex)) {
                    spdlog::debug("SyncToGrid ({},{}) U+{:04X} -> glyph {}", col, row, codepoint, glyphIndex);
                }

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

                // Apply selection highlight (invert colors)
                if (isInSelection(lineIndex, col)) {
                    std::swap(fgR, bgR);
                    std::swap(fgG, bgG);
                    std::swap(fgB, bgB);
                }

                // Pack text attributes for GPU
                CellAttrs attrs;
                attrs._bold = isBold ? 1 : 0;
                attrs._italic = isItalic ? 1 : 0;
                attrs._strikethrough = cell.attrs.strike ? 1 : 0;
                attrs._reserved = 0;
                // Map libvterm underline values: 0=none, 1=single, 2=double, 3=curly
                attrs._underline = static_cast<uint8_t>(cell.attrs.underline & 0x3);

                grid_.setCell(col, row, glyphIndex, fgR, fgG, fgB, bgR, bgG, bgB, attrs);
            }
        }
    }

    // Log sync completion with content info
    if (syncCount <= 5) {
        // Count non-space glyphs in first row
        int nonSpaceInRow0 = 0;
        const uint16_t* glyphData = grid_.getGlyphData();
        uint16_t spaceGlyph = font_ ? font_->getGlyphIndex(' ') : 0;
        for (uint32_t col = 0; col < cols_; col++) {
            if (glyphData[col] != spaceGlyph && glyphData[col] != 0) {
                nonSpaceInRow0++;
            }
        }
        spdlog::debug("syncToGrid #{} done: {} non-space glyphs in row 0", syncCount, nonSpaceInRow0);
    }

    // Note: don't clear damage here - main loop clears after rendering
}

void Terminal::syncDamageToGrid() {
    if (damageRects_.empty()) return;

    spdlog::debug("syncDamageToGrid: {} damage rects", damageRects_.size());

    VTermScreenCell cell;
    VTermPos pos;

    for (const auto& damage : damageRects_) {
        for (uint32_t row = damage._startRow; row < damage._endRow && row < rows_; row++) {
            for (uint32_t col = damage._startCol; col < damage._endCol && col < cols_; col++) {
                pos.row = row;
                pos.col = col;

                vterm_screen_get_cell(vtermScreen_, pos, &cell);

                // Get glyph index
                uint32_t codepoint = cell.chars[0];

                // Handle wide character continuation cells
                if (codepoint == 0xFFFFFFFF || codepoint == static_cast<uint32_t>(-1)) {
                    // Wide char continuation - mark with special glyph so shader can look left
                    spdlog::debug("DamageSync ({},{}) wide continuation", col, row);
                    // Still need to set colors from the cell
                    uint8_t fgR, fgG, fgB, bgR, bgG, bgB;
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
                    grid_.setCell(col, row, GLYPH_WIDE_CONT, fgR, fgG, fgB, bgR, bgG, bgB);
                    continue;
                }
                if (codepoint == 0) codepoint = ' ';

                uint16_t glyphIndex = font_ ? font_->getGlyphIndex(codepoint) : static_cast<uint16_t>(codepoint);

                // Check if this codepoint has a custom glyph plugin
                if (pluginManager_) {
                    uint32_t widthCells = (cell.width > 0) ? cell.width : 1;
                    uint16_t customIdx = pluginManager_->onCellSync(col, row, codepoint, widthCells);
                    if (customIdx != 0) {
                        glyphIndex = customIdx;
                        spdlog::debug("DamageSync ({},{}) U+{:04X} -> custom glyph 0x{:04X}",
                                     col, row, codepoint, glyphIndex);
                    }
                }

                // Log emoji cells
                if (codepoint > 0x1F000 && !isCustomGlyph(glyphIndex)) {
                    spdlog::debug("DamageSync ({},{}) U+{:04X} -> glyph {}", col, row, codepoint, glyphIndex);
                }

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

                // Apply selection highlight (invert colors)
                if (isInSelection(row, col)) {
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
    damage._startCol = static_cast<uint32_t>(rect.start_col);
    damage._startRow = static_cast<uint32_t>(rect.start_row);
    damage._endCol = static_cast<uint32_t>(rect.end_col);
    damage._endRow = static_cast<uint32_t>(rect.end_row);
    term->damageRects_.push_back(damage);

    if (term->config_ && term->config_->_debugDamageRects) {
        spdlog::debug("Damage: [{},{}] -> [{},{}]",
                      damage._startCol, damage._startRow, damage._endCol, damage._endRow);
    }

    return 0;
}

int Terminal::onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user) {
    Terminal* term = static_cast<Terminal*>(user);
    term->cursorRow_ = pos.row;
    term->cursorCol_ = pos.col;
    // Note: visible param is NOT used for cursor visibility - that's handled by
    // VTERM_PROP_CURSORVISIBLE in onSetTermProp. The visible param here indicates
    // whether cursor moved to a visible area of the screen, but we always want to
    // show cursor if DECTCEM is enabled.
    (void)visible;
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

int Terminal::onSetTermProp(VTermProp prop, VTermValue* val, void* user) {
    Terminal* term = static_cast<Terminal*>(user);

    switch (prop) {
        case VTERM_PROP_ALTSCREEN:
            term->isAltScreen_ = val->boolean;
            spdlog::debug("Terminal: alternate screen {}", term->isAltScreen_ ? "ON" : "OFF");
            // Notify plugin manager about screen change
            if (term->pluginManager_) {
                term->pluginManager_->onAltScreenChange(term->isAltScreen_);
            }
            break;
        case VTERM_PROP_CURSORVISIBLE:
            term->cursorVisible_ = val->boolean;
            break;
        case VTERM_PROP_MOUSE:
            term->mouseMode_ = val->number;
            spdlog::debug("Terminal: mouse mode set to {}", term->mouseMode_);
            break;
        default:
            break;
    }
    return 0;
}

int Terminal::onBell(void* user) {
    (void)user;
    // Ring the bell by writing to stdout (the actual terminal)
    if (write(STDOUT_FILENO, "\a", 1) < 0) {
        // Ignore bell errors - not critical
    }
    return 0;
}

int Terminal::onSbPushline(int cols, const VTermScreenCell* cells, void* user) {
    Terminal* term = static_cast<Terminal*>(user);

    // Store line in scrollback buffer
    ScrollbackLine line;
    line._chars.resize(cols);
    line._fgColors.resize(cols * 3);
    line._bgColors.resize(cols * 3);

    for (int i = 0; i < cols; i++) {
        const VTermScreenCell& cell = cells[i];
        uint32_t ch = cell.chars[0];
        // Handle wide character continuation cells
        if (ch == 0xFFFFFFFF || ch == static_cast<uint32_t>(-1) || ch == 0) {
            ch = ' ';
        }
        line._chars[i] = ch;

        // Convert colors
        VTermColor fg = cell.fg;
        VTermColor bg = cell.bg;
        vterm_screen_convert_color_to_rgb(term->vtermScreen_, &fg);
        vterm_screen_convert_color_to_rgb(term->vtermScreen_, &bg);

        line._fgColors[i * 3 + 0] = fg.rgb.red;
        line._fgColors[i * 3 + 1] = fg.rgb.green;
        line._fgColors[i * 3 + 2] = fg.rgb.blue;
        line._bgColors[i * 3 + 0] = bg.rgb.red;
        line._bgColors[i * 3 + 1] = bg.rgb.green;
        line._bgColors[i * 3 + 2] = bg.rgb.blue;
    }

    term->scrollback_.push_back(std::move(line));

    // Trim scrollback if too large
    uint32_t maxLines = term->config_ ? term->config_->_scrollbackLines : 10000;
    while (term->scrollback_.size() > maxLines) {
        term->scrollback_.pop_front();
    }

    // Notify plugin manager that content scrolled up by 1 line
    // Pass grid so plugin cell markers can be updated
    if (term->pluginManager_) {
        term->pluginManager_->onScroll(1, &term->grid_);
    }

    return 1;  // We stored the line
}

int Terminal::onSbPopline(int cols, VTermScreenCell* cells, void* user) {
    Terminal* term = static_cast<Terminal*>(user);

    if (term->scrollback_.empty()) {
        return 0;  // No lines to pop
    }

    ScrollbackLine& line = term->scrollback_.back();
    int lineCols = std::min(cols, static_cast<int>(line._chars.size()));

    for (int i = 0; i < lineCols; i++) {
        cells[i].chars[0] = line._chars[i];
        cells[i].chars[1] = 0;
        cells[i].width = 1;

        cells[i].fg.type = VTERM_COLOR_RGB;
        cells[i].fg.rgb.red = line._fgColors[i * 3 + 0];
        cells[i].fg.rgb.green = line._fgColors[i * 3 + 1];
        cells[i].fg.rgb.blue = line._fgColors[i * 3 + 2];

        cells[i].bg.type = VTERM_COLOR_RGB;
        cells[i].bg.rgb.red = line._bgColors[i * 3 + 0];
        cells[i].bg.rgb.green = line._bgColors[i * 3 + 1];
        cells[i].bg.rgb.blue = line._bgColors[i * 3 + 2];
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

    // Notify plugin manager that content scrolled down by 1 line
    // (opposite of pushline - plugins move down)
    if (term->pluginManager_) {
        term->pluginManager_->onScroll(-1, &term->grid_);
    }

    return 1;  // We restored the line
}

int Terminal::onMoverect(VTermRect dest, VTermRect src, void* user) {
    Terminal* term = static_cast<Terminal*>(user);

    // Calculate vertical scroll delta from source to destination
    // Positive delta = content moved UP (like sb_pushline)
    // Negative delta = content moved DOWN (like sb_popline)
    int scrollDelta = src.start_row - dest.start_row;

    if (scrollDelta != 0 && term->pluginManager_) {
        term->pluginManager_->onScroll(scrollDelta, &term->grid_);
    }

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
            uint32_t linesToAdvance = 0;

            bool handled = term->pluginManager_->handleOSCSequence(
                fullSeq,
                &term->grid_,
                term->cursorCol_,
                term->cursorRow_,
                term->cellWidth_,
                term->cellHeight_,
                &response,
                &linesToAdvance
            );

            if (handled) {
                term->fullDamage_ = true;  // Force full redraw
                // Write query response back to PTY if any
                if (!response.empty() && term->ptyMaster_ >= 0) {
                    auto result = term->writeToPty(response.c_str(), response.size());
                    if (!result) {
                        spdlog::error("Failed to write OSC response: {}", result.error().message());
                    }
                }

                // Defer cursor advance - can't call vterm_input_write during callback
                // Will be processed after current vterm_input_write completes
                if (linesToAdvance > 0) {
                    term->pendingNewlines_ = linesToAdvance;
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

//-----------------------------------------------------------------------------
// Selection support
//-----------------------------------------------------------------------------

void Terminal::startSelection(int row, int col, SelectionMode mode) {
    selectionStart_.row = row;
    selectionStart_.col = col;
    selectionEnd_.row = row;
    selectionEnd_.col = col;
    selectionMode_ = mode;

    // For word/line mode, expand selection immediately
    if (mode == SelectionMode::Word) {
        // Expand to word boundaries
        VTermPos pos = {row, col};
        VTermScreenCell cell;

        // Find word start (go left while not space/punctuation)
        while (pos.col > 0) {
            VTermPos testPos = {pos.row, pos.col - 1};
            vterm_screen_get_cell(vtermScreen_, testPos, &cell);
            uint32_t ch = cell.chars[0];
            if (ch == 0 || ch == ' ' || ch == '\t') break;
            pos.col--;
        }
        selectionStart_ = pos;

        // Find word end (go right while not space/punctuation)
        pos = {row, col};
        while (pos.col < static_cast<int>(cols_) - 1) {
            vterm_screen_get_cell(vtermScreen_, pos, &cell);
            uint32_t ch = cell.chars[0];
            if (ch == 0 || ch == ' ' || ch == '\t') break;
            pos.col++;
        }
        selectionEnd_ = pos;
    } else if (mode == SelectionMode::Line) {
        selectionStart_.col = 0;
        selectionEnd_.col = static_cast<int>(cols_);
    }

    fullDamage_ = true;  // Need redraw for selection highlight
}

void Terminal::extendSelection(int row, int col) {
    if (selectionMode_ == SelectionMode::None) return;

    selectionEnd_.row = row;
    selectionEnd_.col = col;

    // For line mode, always extend to full lines
    if (selectionMode_ == SelectionMode::Line) {
        if (row >= selectionStart_.row) {
            selectionEnd_.col = static_cast<int>(cols_);
        } else {
            selectionEnd_.col = 0;
        }
    }

    fullDamage_ = true;  // Need redraw for selection highlight
}

void Terminal::clearSelection() {
    selectionMode_ = SelectionMode::None;
    selectionStart_ = {0, 0};
    selectionEnd_ = {0, 0};
    fullDamage_ = true;  // Need redraw to remove highlight
}

bool Terminal::isInSelection(int row, int col) const {
    if (selectionMode_ == SelectionMode::None) return false;

    // Normalize start/end so start <= end in document order
    VTermPos start = selectionStart_;
    VTermPos end = selectionEnd_;

    if (vterm_pos_cmp(start, end) > 0) {
        std::swap(start, end);
    }

    VTermPos pos = {row, col};

    // Check if pos is between start and end
    if (vterm_pos_cmp(pos, start) < 0) return false;
    if (vterm_pos_cmp(pos, end) > 0) return false;

    return true;
}

std::string Terminal::getSelectedText() const {
    if (selectionMode_ == SelectionMode::None) return "";

    // Normalize start/end
    VTermPos start = selectionStart_;
    VTermPos end = selectionEnd_;

    if (vterm_pos_cmp(start, end) > 0) {
        std::swap(start, end);
    }

    std::string result;

    // Build text line by line
    for (int row = start.row; row <= end.row; row++) {
        int startCol = (row == start.row) ? start.col : 0;
        // end_col is exclusive in VTermRect, so add 1 to include the last character
        int endCol = (row == end.row) ? end.col + 1 : static_cast<int>(cols_);

        // Use vterm_screen_get_text for this row segment
        VTermRect rect;
        rect.start_row = row;
        rect.end_row = row + 1;
        rect.start_col = startCol;
        rect.end_col = endCol;

        // Allocate buffer (worst case: 4 bytes per cell for UTF-8)
        size_t bufSize = (endCol - startCol + 1) * 4 + 1;
        std::vector<char> buf(bufSize);

        size_t len = vterm_screen_get_text(vtermScreen_, buf.data(), bufSize, rect);

        if (len > 0) {
            result.append(buf.data(), len);
        }

        // Add newline between lines (but not after last line)
        if (row < end.row) {
            result += '\n';
        }
    }

    return result;
}

} // namespace yetty
