#include "local-terminal-backend.h"

// Optional dependencies - may not be available in server build
#ifndef YETTY_SERVER_BUILD
#include "emoji-atlas.h"
#include "yetty/emoji.h"
#include <yetty/font.h>
#else
// Stubs for server build
namespace yetty {
class EmojiAtlas;
class Font {
public:
    uint16_t getGlyphIndex(uint32_t cp, bool = false, bool = false) const { return static_cast<uint16_t>(cp); }
};
inline bool isEmoji(uint32_t) { return false; }
}
#endif

#include <chrono>
#include <cstring>
#include <ytrace/ytrace.hpp>

#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif
#endif

namespace yetty {

// Forward declare from terminal.h (we need the full definitions)
// These are already declared in terminal.h, but we need them here too
// for the scrollback operations

// libvterm callbacks
static VTermScreenCallbacks screenCallbacks = {
    .damage = LocalTerminalBackend::onDamage,
    .moverect = LocalTerminalBackend::onMoverect,
    .movecursor = LocalTerminalBackend::onMoveCursor,
    .settermprop = LocalTerminalBackend::onSetTermProp,
    .bell = LocalTerminalBackend::onBell,
    .resize = LocalTerminalBackend::onResize,
    .sb_pushline = LocalTerminalBackend::onSbPushline,
    .sb_popline = LocalTerminalBackend::onSbPopline,
    .sb_clear = nullptr,
};

static VTermStateFallbacks stateFallbacks = {
    .control = nullptr,
    .csi = nullptr,
    .osc = LocalTerminalBackend::onOSC,
    .dcs = nullptr,
    .apc = nullptr,
    .pm = nullptr,
    .sos = nullptr,
};

//=============================================================================
// Factory
//=============================================================================

Result<LocalTerminalBackend::Ptr> LocalTerminalBackend::create(
    uint32_t cols, uint32_t rows, Font* font, uv_loop_t* loop) noexcept {
    
#ifndef YETTY_SERVER_BUILD
    if (!font) {
        return Err<Ptr>("LocalTerminalBackend::create: null Font");
    }
#endif
    if (!loop) {
        return Err<Ptr>("LocalTerminalBackend::create: null libuv loop");
    }
    auto backend = Ptr(new LocalTerminalBackend(cols, rows, font, loop));
    if (auto res = backend->init(); !res) {
        return Err<Ptr>("Failed to initialize LocalTerminalBackend", res);
    }
    return Ok(std::move(backend));
}

LocalTerminalBackend::LocalTerminalBackend(
    uint32_t cols, uint32_t rows, Font* font, uv_loop_t* loop) noexcept
    : loop_(loop)
    , grid_(cols, rows)
    , font_(font)
    , cols_(cols)
    , rows_(rows) {}

Result<void> LocalTerminalBackend::init() noexcept {
    vterm_ = vterm_new(rows_, cols_);
    if (!vterm_) {
        return Err<void>("Failed to create VTerm");
    }
    vterm_set_utf8(vterm_, 1);

    vtermScreen_ = vterm_obtain_screen(vterm_);
    vterm_screen_set_callbacks(vtermScreen_, &screenCallbacks, this);
    vterm_screen_enable_altscreen(vtermScreen_, 1);
    vterm_screen_enable_reflow(vtermScreen_, true);
    vterm_screen_reset(vtermScreen_, 1);

    VTermState* state = vterm_obtain_state(vterm_);
    vterm_state_set_unrecognised_fallbacks(state, &stateFallbacks, this);

    ptyReadBuffer_ = std::make_unique<char[]>(PTY_READ_BUFFER_SIZE);

    return Ok();
}

LocalTerminalBackend::~LocalTerminalBackend() {
    stop();

#ifndef _WIN32
    if (ptyMaster_ >= 0) {
        close(ptyMaster_);
    }
    if (childPid_ > 0) {
        kill(childPid_, SIGTERM);
        waitpid(childPid_, nullptr, 0);
    }
#endif

    if (vterm_) {
        vterm_free(vterm_);
    }
}

//=============================================================================
// Lifecycle
//=============================================================================

Result<void> LocalTerminalBackend::start(const std::string& shell) {
    if (running_) return Ok();

#ifndef _WIN32
    std::string shellPath = shell.empty() ? (getenv("SHELL") ? getenv("SHELL") : "/bin/sh") : shell;

    ydebug("Starting shell: {}", shellPath);

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
        // Child
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);

        for (int fd = 3; fd < 1024; fd++) close(fd);

        if (shellPath.find(' ') != std::string::npos) {
            const char* sh = getenv("SHELL") ? getenv("SHELL") : "/bin/sh";
            execl(sh, sh, "-c", shellPath.c_str(), nullptr);
        } else {
            execl(shellPath.c_str(), shellPath.c_str(), nullptr);
        }
        _exit(1);
    }

    // Parent - set non-blocking
    int flags = fcntl(ptyMaster_, F_GETFL, 0);
    fcntl(ptyMaster_, F_SETFL, flags | O_NONBLOCK);

    // Set up PTY poll handle
    ptyPoll_ = new uv_poll_t;
    uv_poll_init(loop_, ptyPoll_, ptyMaster_);
    ptyPoll_->data = this;
    uv_poll_start(ptyPoll_, UV_READABLE, onPtyPoll);

    yinfo("LocalTerminalBackend started: PTY fd={}, PID={}", ptyMaster_, childPid_);
    running_ = true;
    return Ok();
#else
    return Err<void>("Windows not implemented yet");
#endif
}

void LocalTerminalBackend::stop() {
    if (!running_) return;

    running_ = false;

#ifndef _WIN32
    if (ptyPoll_) {
        uv_poll_stop(ptyPoll_);
        uv_close(reinterpret_cast<uv_handle_t*>(ptyPoll_), [](uv_handle_t* h) {
            delete reinterpret_cast<uv_poll_t*>(h);
        });
        ptyPoll_ = nullptr;
    }
#endif
}

//=============================================================================
// Input
//=============================================================================

void LocalTerminalBackend::sendKey(uint32_t codepoint, VTermModifier mod) {
    if (scrollOffset_ != 0) {
        scrollOffset_ = 0;
        fullDamage_ = true;
    }
    vterm_keyboard_unichar(vterm_, codepoint, mod);
    flushVtermOutput();
}

void LocalTerminalBackend::sendSpecialKey(VTermKey key, VTermModifier mod) {
    if (scrollOffset_ != 0) {
        scrollOffset_ = 0;
        fullDamage_ = true;
    }
    vterm_keyboard_key(vterm_, key, mod);
    flushVtermOutput();
}

void LocalTerminalBackend::sendRaw(const char* data, size_t len) {
    if (scrollOffset_ != 0) {
        scrollOffset_ = 0;
        fullDamage_ = true;
    }
    writeToPty(data, len);
}

void LocalTerminalBackend::resize(uint32_t cols, uint32_t rows) {
    cols_ = cols;
    rows_ = rows;
    vterm_set_size(vterm_, rows_, cols_);
    grid_.resize(cols_, rows_);
#ifndef _WIN32
    if (ptyMaster_ >= 0) {
        struct winsize ws = {static_cast<unsigned short>(rows_), static_cast<unsigned short>(cols_), 0, 0};
        ioctl(ptyMaster_, TIOCSWINSZ, &ws);
    }
#endif
    fullDamage_ = true;
}

//=============================================================================
// Scrollback
//=============================================================================

void LocalTerminalBackend::scrollUp(int lines) {
    int oldOffset = scrollOffset_;
    int sbSize = static_cast<int>(scrollback_.size());
    scrollOffset_ = std::min(scrollOffset_ + lines, sbSize);
    ydebug("LocalTerminalBackend::scrollUp: lines={}, scrollback_.size()={}, oldOffset={}, newOffset={}", 
                  lines, sbSize, oldOffset, scrollOffset_);
    fullDamage_ = true;
}

void LocalTerminalBackend::scrollDown(int lines) {
    int oldOffset = scrollOffset_;
    scrollOffset_ = std::max(scrollOffset_ - lines, 0);
    ydebug("LocalTerminalBackend::scrollDown: lines={}, oldOffset={}, newOffset={}", 
                  lines, oldOffset, scrollOffset_);
    fullDamage_ = true;
}

void LocalTerminalBackend::scrollToTop() {
    scrollOffset_ = static_cast<int>(scrollback_.size());
    fullDamage_ = true;
}

void LocalTerminalBackend::scrollToBottom() {
    scrollOffset_ = 0;
    fullDamage_ = true;
}

//=============================================================================
// Selection
//=============================================================================

void LocalTerminalBackend::startSelection(int row, int col, SelectionMode mode) {
    selectionStart_ = {row, col};
    selectionEnd_ = {row, col};
    selectionMode_ = mode;
    fullDamage_ = true;
}

void LocalTerminalBackend::extendSelection(int row, int col) {
    if (selectionMode_ == SelectionMode::None) return;
    selectionEnd_ = {row, col};
    fullDamage_ = true;
}

void LocalTerminalBackend::clearSelection() {
    selectionMode_ = SelectionMode::None;
    fullDamage_ = true;
}

bool LocalTerminalBackend::isInSelection(int row, int col) const {
    if (selectionMode_ == SelectionMode::None) return false;

    VTermPos start = selectionStart_, end = selectionEnd_;
    if (vterm_pos_cmp(start, end) > 0) std::swap(start, end);

    VTermPos pos = {row, col};
    return vterm_pos_cmp(pos, start) >= 0 && vterm_pos_cmp(pos, end) <= 0;
}

std::string LocalTerminalBackend::getSelectedText() {
    if (selectionMode_ == SelectionMode::None) return "";

    VTermPos start = selectionStart_, end = selectionEnd_;
    if (vterm_pos_cmp(start, end) > 0) std::swap(start, end);

    std::string result;
    for (int row = start.row; row <= end.row; row++) {
        int sc = (row == start.row) ? start.col : 0;
        int ec = (row == end.row) ? end.col + 1 : static_cast<int>(cols_);

        VTermRect rect = {row, row + 1, sc, ec};
        char buf[1024];
        size_t len = vterm_screen_get_text(vtermScreen_, buf, sizeof(buf), rect);
        if (len > 0) result.append(buf, len);
        if (row < end.row) result += '\n';
    }
    return result;
}

//=============================================================================
// PTY I/O
//=============================================================================

void LocalTerminalBackend::onPtyPoll(uv_poll_t* handle, int status, int events) {
    auto* self = static_cast<LocalTerminalBackend*>(handle->data);

    if (status < 0) {
        yerror("LocalTerminalBackend: PTY poll error: {}", uv_strerror(status));
        return;
    }

    if (events & UV_READABLE) {
        if (auto res = self->readPty(); !res) {
            yerror("LocalTerminalBackend: PTY read error: {}", res.error().to_string());
        }
    }
}

Result<void> LocalTerminalBackend::readPty() {
#ifndef _WIN32
    // Check child status
    int status;
    if (waitpid(childPid_, &status, WNOHANG) > 0) {
        running_ = false;
        yinfo("Shell exited");
        return Ok();
    }

    if (!running_) {
        return Ok();
    }

    // Drain PTY
    size_t totalRead = 0;
    ssize_t n;
    while ((n = read(ptyMaster_, ptyReadBuffer_.get() + totalRead, PTY_READ_BUFFER_SIZE - totalRead)) > 0) {
        totalRead += n;
        if (totalRead >= PTY_READ_BUFFER_SIZE) break;
    }
    
    if (totalRead == 0 && n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return Ok();
        }
        if (errno == EIO) {
            running_ = false;
            yinfo("Shell exited (PTY closed)");
            return Ok();
        }
        return Err<void>(std::string("PTY read error: ") + strerror(errno));
    }

    if (totalRead > 0) {
        vterm_input_write(vterm_, ptyReadBuffer_.get(), totalRead);
        vterm_screen_flush_damage(vtermScreen_);

        if (pendingNewlines_ > 0) {
            std::string nl(pendingNewlines_, '\n');
            vterm_input_write(vterm_, nl.c_str(), nl.size());
            vterm_screen_flush_damage(vtermScreen_);
            pendingNewlines_ = 0;
        }

        flushVtermOutput();
    }

    return Ok();
#else
    return Err<void>("Windows not implemented");
#endif
}

Result<void> LocalTerminalBackend::writeToPty(const char* data, size_t len) {
    if (len == 0) return Ok();
#ifndef _WIN32
    if (ptyMaster_ < 0) return Err("PTY not open");
    ssize_t written = write(ptyMaster_, data, len);
    if (written < 0) {
        return Err("PTY write failed: " + std::string(strerror(errno)));
    }
#endif
    return Ok();
}

Result<void> LocalTerminalBackend::flushVtermOutput() {
    size_t outlen = vterm_output_get_buffer_current(vterm_);
    char buf[65536];
    while (outlen > 0) {
        size_t n = vterm_output_read(vterm_, buf, sizeof(buf));
        if (n > 0) {
            if (auto res = writeToPty(buf, n); !res) return res;
        }
        outlen = vterm_output_get_buffer_current(vterm_);
    }
    return Ok();
}

//=============================================================================
// Grid sync
//=============================================================================

void LocalTerminalBackend::syncToGrid() {
    VTermScreenCell cell;
    VTermPos pos;

    int sbSize = static_cast<int>(scrollback_.size());
    int effectiveOffset = std::min(scrollOffset_, sbSize);

    for (uint32_t row = 0; row < rows_; row++) {
        int lineIndex = static_cast<int>(row) - effectiveOffset;

        if (lineIndex < 0) {
            // Scrollback
            int sbIndex = sbSize + lineIndex;
            if (sbIndex >= 0 && sbIndex < sbSize) {
                const auto& sbLine = scrollback_[sbIndex];
                
                // Decode RLE styles for this line
                size_t runIdx = 0;
                int currentRunRemaining = 0;
                ScrollbackStyle currentStyle = {};
                if (!sbLine.styleRuns.empty()) {
                    currentStyle = sbLine.styleRuns[0].style;
                    currentRunRemaining = sbLine.styleRuns[0].count;
                }
                
                for (uint32_t col = 0; col < cols_; col++) {
                    uint32_t cp = (col < sbLine.chars.size()) ? sbLine.chars[col] : ' ';
                    uint8_t fgR = 255, fgG = 255, fgB = 255;
                    uint8_t bgR = 0, bgG = 0, bgB = 0;
                    
                    bool isBold = false, isItalic = false, isUnderline = false, isStrike = false, isReverse = false;
                    if (currentRunRemaining > 0) {
                        fgR = currentStyle.fgR;
                        fgG = currentStyle.fgG;
                        fgB = currentStyle.fgB;
                        bgR = currentStyle.bgR;
                        bgG = currentStyle.bgG;
                        bgB = currentStyle.bgB;
                        
                        uint16_t attrs = currentStyle.attrs;
                        isBold = attrs & 1;
                        isUnderline = (attrs >> 1) & 3;
                        isItalic = (attrs >> 3) & 1;
                        isReverse = (attrs >> 5) & 1;
                        isStrike = (attrs >> 7) & 1;
                        
                        currentRunRemaining--;
                        if (currentRunRemaining == 0 && runIdx + 1 < sbLine.styleRuns.size()) {
                            runIdx++;
                            currentStyle = sbLine.styleRuns[runIdx].style;
                            currentRunRemaining = sbLine.styleRuns[runIdx].count;
                        }
                    }
                    
                    if (isReverse) {
                        std::swap(fgR, bgR); std::swap(fgG, bgG); std::swap(fgB, bgB);
                    }
                    
                    uint16_t gi = font_ ? font_->getGlyphIndex(cp, isBold, isItalic) : static_cast<uint16_t>(cp);
                    CellAttrs cellAttrs;
                    cellAttrs._emoji = isEmoji(cp) ? 1 : 0;
                    cellAttrs._bold = isBold ? 1 : 0;
                    cellAttrs._italic = isItalic ? 1 : 0;
                    cellAttrs._underline = isUnderline ? 1 : 0;
                    cellAttrs._strikethrough = isStrike ? 1 : 0;
                    grid_.setCell(col, row, gi, fgR, fgG, fgB, bgR, bgG, bgB, cellAttrs);
                }
            }
        } else {
            // VTerm screen
            for (uint32_t col = 0; col < cols_; col++) {
                pos.row = lineIndex;
                pos.col = col;
                vterm_screen_get_cell(vtermScreen_, pos, &cell);

                uint32_t cp = cell.chars[0];
                if (cp == 0xFFFFFFFF || cp == static_cast<uint32_t>(-1)) {
                    // Wide char continuation
                    uint8_t fgR, fgG, fgB, bgR, bgG, bgB;
                    VTermColor fg = cell.fg, bg = cell.bg;
                    vterm_screen_convert_color_to_rgb(vtermScreen_, &fg);
                    vterm_screen_convert_color_to_rgb(vtermScreen_, &bg);
                    colorToRGB(fg, fgR, fgG, fgB);
                    colorToRGB(bg, bgR, bgG, bgB);
                    if (cell.attrs.reverse) {
                        std::swap(fgR, bgR); std::swap(fgG, bgG); std::swap(fgB, bgB);
                    }
                    grid_.setCell(col, row, GLYPH_WIDE_CONT, fgR, fgG, fgB, bgR, bgG, bgB);
                    continue;
                }
                if (cp == 0) cp = ' ';

                bool isBold = cell.attrs.bold != 0;
                bool isItalic = cell.attrs.italic != 0;

                uint16_t gi;
                int emojiIdx = -1;

#ifndef YETTY_SERVER_BUILD
                if (isEmoji(cp) && emojiAtlas_) {
                    emojiIdx = emojiAtlas_->getEmojiIndex(cp);
                    if (emojiIdx < 0) {
                        auto result = emojiAtlas_->loadEmoji(cp);
                        if (result && *result >= 0) {
                            emojiIdx = *result;
                        }
                    }

                    if (emojiIdx >= 0) {
                        gi = static_cast<uint16_t>(emojiIdx);
                    } else {
                        gi = font_ ? font_->getGlyphIndex(' ') : static_cast<uint16_t>(' ');
                    }
                } else {
                    gi = font_ ? font_->getGlyphIndex(cp, isBold, isItalic) : static_cast<uint16_t>(cp);
                }
#else
                // Server build: just use codepoint directly
                gi = static_cast<uint16_t>(cp);
#endif

                uint8_t fgR, fgG, fgB, bgR, bgG, bgB;
                VTermColor fg = cell.fg, bg = cell.bg;
                vterm_screen_convert_color_to_rgb(vtermScreen_, &fg);
                vterm_screen_convert_color_to_rgb(vtermScreen_, &bg);
                colorToRGB(fg, fgR, fgG, fgB);
                colorToRGB(bg, bgR, bgG, bgB);

                if (cell.attrs.reverse) {
                    std::swap(fgR, bgR); std::swap(fgG, bgG); std::swap(fgB, bgB);
                }
                if (isInSelection(lineIndex, col)) {
                    std::swap(fgR, bgR); std::swap(fgG, bgG); std::swap(fgB, bgB);
                }

                CellAttrs attrs;
                attrs._bold = isBold ? 1 : 0;
                attrs._italic = isItalic ? 1 : 0;
                attrs._strikethrough = cell.attrs.strike ? 1 : 0;
                attrs._underline = static_cast<uint8_t>(cell.attrs.underline & 0x3);
                attrs._emoji = (emojiIdx >= 0) ? 1 : 0;

                grid_.setCell(col, row, gi, fgR, fgG, fgB, bgR, bgG, bgB, attrs);
            }
        }
    }
}

void LocalTerminalBackend::syncDamageToGrid() {
    syncToGrid();
}

void LocalTerminalBackend::colorToRGB(const VTermColor& color, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (VTERM_COLOR_IS_DEFAULT_FG(&color)) {
        r = 255; g = 255; b = 255;
    } else if (VTERM_COLOR_IS_DEFAULT_BG(&color)) {
        r = 0; g = 0; b = 0;
    } else if (VTERM_COLOR_IS_RGB(&color)) {
        r = color.rgb.red; g = color.rgb.green; b = color.rgb.blue;
    } else {
        r = 128; g = 128; b = 128;
    }
}

//=============================================================================
// libvterm callbacks
//=============================================================================

int LocalTerminalBackend::onDamage(VTermRect rect, void* user) {
    auto* backend = static_cast<LocalTerminalBackend*>(user);
    DamageRect d;
    d._startCol = rect.start_col;
    d._startRow = rect.start_row;
    d._endCol = rect.end_col;
    d._endRow = rect.end_row;
    backend->damageRects_.push_back(d);
    return 0;
}

int LocalTerminalBackend::onMoveCursor(VTermPos pos, VTermPos, int, void* user) {
    auto* backend = static_cast<LocalTerminalBackend*>(user);
    backend->cursorRow_ = pos.row;
    backend->cursorCol_ = pos.col;
    return 0;
}

int LocalTerminalBackend::onSetTermProp(VTermProp prop, VTermValue* val, void* user) {
    auto* backend = static_cast<LocalTerminalBackend*>(user);
    switch (prop) {
        case VTERM_PROP_ALTSCREEN:
            backend->isAltScreen_ = val->boolean;
            if (backend->callbacks_.onAltScreenChange) {
                backend->callbacks_.onAltScreenChange(backend->isAltScreen_);
            }
            break;
        case VTERM_PROP_CURSORVISIBLE:
            backend->cursorVisible_ = val->boolean;
            break;
        case VTERM_PROP_MOUSE:
            backend->mouseMode_ = val->number;
            break;
        default: break;
    }
    return 0;
}

int LocalTerminalBackend::onResize(int, int, void*) { return 0; }

int LocalTerminalBackend::onBell(void* user) {
    auto* backend = static_cast<LocalTerminalBackend*>(user);
    if (backend->callbacks_.onBell) {
        backend->callbacks_.onBell();
    } else {
#ifndef _WIN32
        write(STDOUT_FILENO, "\a", 1);
#endif
    }
    return 0;
}

int LocalTerminalBackend::onSbPushline(int cols, const VTermScreenCell* cells, void* user) {
    auto* backend = static_cast<LocalTerminalBackend*>(user);

    auto packAttrs = [](const VTermScreenCellAttrs& a) -> uint16_t {
        return (a.bold) | (a.underline << 1) | (a.italic << 3) |
               (a.blink << 4) | (a.reverse << 5) | (a.conceal << 6) |
               (a.strike << 7) | (a.font << 8) | (a.dwl << 12) |
               (a.dhl << 13) | (a.small << 15);
    };

    ScrollbackLine line;
    line.chars.resize(cols);

    ScrollbackStyle lastStyle = {};
    uint16_t runCount = 0;

    for (int i = 0; i < cols; i++) {
        uint32_t ch = cells[i].chars[0];
        if (ch == 0xFFFFFFFF || ch == static_cast<uint32_t>(-1) || ch == 0) ch = ' ';
        line.chars[i] = ch;

        VTermColor fg = cells[i].fg, bg = cells[i].bg;
        vterm_screen_convert_color_to_rgb(backend->vtermScreen_, &fg);
        vterm_screen_convert_color_to_rgb(backend->vtermScreen_, &bg);

        ScrollbackStyle style;
        style.fgR = fg.rgb.red;
        style.fgG = fg.rgb.green;
        style.fgB = fg.rgb.blue;
        style.bgR = bg.rgb.red;
        style.bgG = bg.rgb.green;
        style.bgB = bg.rgb.blue;
        style.attrs = packAttrs(cells[i].attrs);

        if (i == 0) {
            lastStyle = style;
            runCount = 1;
        } else if (style == lastStyle && runCount < 65535) {
            runCount++;
        } else {
            line.styleRuns.push_back({lastStyle, runCount});
            lastStyle = style;
            runCount = 1;
        }
    }
    if (runCount > 0) {
        line.styleRuns.push_back({lastStyle, runCount});
    }

    backend->scrollback_.push_back(std::move(line));

#ifndef YETTY_SERVER_BUILD
    uint32_t maxLines = backend->config_ ? backend->config_->scrollbackLines() : 10000;
#else
    uint32_t maxLines = 10000;
#endif
    while (backend->scrollback_.size() > maxLines) {
        backend->scrollback_.pop_front();
    }

    return 1;
}

int LocalTerminalBackend::onSbPopline(int cols, VTermScreenCell* cells, void* user) {
    auto* backend = static_cast<LocalTerminalBackend*>(user);
    if (backend->scrollback_.empty()) return 0;

    auto& line = backend->scrollback_.back();
    int lineCols = std::min(cols, static_cast<int>(line.chars.size()));

    auto unpackAttrs = [](uint16_t packed, VTermScreenCellAttrs& a) {
        memset(&a, 0, sizeof(a));
        a.bold = packed & 1;
        a.underline = (packed >> 1) & 3;
        a.italic = (packed >> 3) & 1;
        a.blink = (packed >> 4) & 1;
        a.reverse = (packed >> 5) & 1;
        a.conceal = (packed >> 6) & 1;
        a.strike = (packed >> 7) & 1;
        a.font = (packed >> 8) & 0xF;
        a.dwl = (packed >> 12) & 1;
        a.dhl = (packed >> 13) & 3;
        a.small = (packed >> 15) & 1;
    };

    size_t runIdx = 0;
    ScrollbackStyle currentStyle = {};
    int currentRunRemaining = 0;
    
    if (!line.styleRuns.empty()) {
        currentStyle = line.styleRuns[0].style;
        currentRunRemaining = line.styleRuns[0].count;
    }

    for (int i = 0; i < lineCols; i++) {
        cells[i].chars[0] = line.chars[i];
        cells[i].chars[1] = 0;
        cells[i].width = 1;
        
        if (currentRunRemaining > 0) {
            unpackAttrs(currentStyle.attrs, cells[i].attrs);
            cells[i].fg.type = VTERM_COLOR_RGB;
            cells[i].fg.rgb.red = currentStyle.fgR;
            cells[i].fg.rgb.green = currentStyle.fgG;
            cells[i].fg.rgb.blue = currentStyle.fgB;
            cells[i].bg.type = VTERM_COLOR_RGB;
            cells[i].bg.rgb.red = currentStyle.bgR;
            cells[i].bg.rgb.green = currentStyle.bgG;
            cells[i].bg.rgb.blue = currentStyle.bgB;
            
            currentRunRemaining--;
            if (currentRunRemaining == 0 && runIdx + 1 < line.styleRuns.size()) {
                runIdx++;
                currentStyle = line.styleRuns[runIdx].style;
                currentRunRemaining = line.styleRuns[runIdx].count;
            }
        } else {
            memset(&cells[i].attrs, 0, sizeof(cells[i].attrs));
            cells[i].fg.type = VTERM_COLOR_DEFAULT_FG;
            cells[i].bg.type = VTERM_COLOR_DEFAULT_BG;
        }
    }

    for (int i = lineCols; i < cols; i++) {
        cells[i].chars[0] = ' ';
        cells[i].chars[1] = 0;
        cells[i].width = 1;
        memset(&cells[i].attrs, 0, sizeof(cells[i].attrs));
        cells[i].fg.type = VTERM_COLOR_DEFAULT_FG;
        cells[i].bg.type = VTERM_COLOR_DEFAULT_BG;
    }

    backend->scrollback_.pop_back();

    return 1;
}

int LocalTerminalBackend::onMoverect(VTermRect, VTermRect, void* user) {
    auto* backend = static_cast<LocalTerminalBackend*>(user);
    backend->fullDamage_ = true;
    return 0;
}

int LocalTerminalBackend::onOSC(int command, VTermStringFragment frag, void* user) {
    auto* backend = static_cast<LocalTerminalBackend*>(user);

    ydebug("onOSC: command={} initial={} final={} len={}",
                  command, (bool)frag.initial, (bool)frag.final, (size_t)frag.len);

    // Handle custom OSC via callback
    if (backend->callbacks_.onOSC && frag.final) {
        std::string data;
        if (frag.initial) {
            data = std::string(frag.str, frag.len);
        } else {
            data = backend->oscBuffer_ + std::string(frag.str, frag.len);
        }
        backend->callbacks_.onOSC(command, data);
    }

    // Also handle via plugin manager for backward compatibility
    constexpr int YETTY_OSC_VENDOR_ID = 7777;
    if (command != YETTY_OSC_VENDOR_ID) {
        ydebug("onOSC: ignoring non-yetty command {}", command);
        return 0;
    }

    if (frag.initial) {
        backend->oscBuffer_.clear();
        backend->oscCommand_ = command;
    }

    if (frag.len > 0) {
        backend->oscBuffer_.append(frag.str, frag.len);
    }

    if (frag.final) {
        backend->oscBuffer_.clear();
        backend->oscCommand_ = -1;
    }

    return 1;
}

} // namespace yetty
