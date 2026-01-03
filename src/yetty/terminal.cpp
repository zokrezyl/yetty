#include "terminal.h"
#include "emoji-atlas.h"
#include "grid-renderer.h"
#include "yetty/emoji.h"
#include "yetty/plugin-manager.h"

#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>

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

// libvterm callbacks
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

static VTermStateFallbacks stateFallbacks = {
    .control = nullptr,
    .csi = nullptr,
    .osc = Terminal::onOSC,
    .dcs = nullptr,
    .apc = nullptr,
    .pm = nullptr,
    .sos = nullptr,
};

//=============================================================================
// Factory
//=============================================================================

Result<Terminal::Ptr> Terminal::create(uint32_t id, uint32_t cols, uint32_t rows, Font* font) noexcept {
    if (!font) {
        return Err<Ptr>("Terminal::create: null Font");
    }
    auto term = Ptr(new Terminal(id, cols, rows, font));
    if (auto res = term->init(); !res) {
        return Err<Ptr>("Failed to initialize Terminal", res);
    }
    return Ok(std::move(term));
}

Terminal::Terminal(uint32_t id, uint32_t cols, uint32_t rows, Font* font) noexcept
    : id_(id)
    , name_("terminal-" + std::to_string(id))
    , grid_(cols, rows)
    , font_(font)
    , cols_(cols)
    , rows_(rows) {}

Result<void> Terminal::init() noexcept {
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

Terminal::~Terminal() {
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
// Renderable interface
//=============================================================================

void Terminal::start() {
    if (running_.load()) return;

    // Start shell
    if (auto res = startShell(shell_); !res) {
        spdlog::error("Terminal: failed to start shell: {}", res.error().to_string());
        return;
    }

    running_.store(true);
    stopRequested_.store(false);

    // Start worker thread
    workerThread_ = std::thread(&Terminal::workerLoop, this);
}

void Terminal::stop() {
    if (!running_.load()) return;

    stopRequested_.store(true);

    if (asyncHandle_) {
        uv_async_send(asyncHandle_);
    }

    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    running_.store(false);
}

Result<void> Terminal::render(WebGPUContext& ctx) {
    (void)ctx;  // We use renderer_ directly

    // Try to acquire lock - don't block main thread
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        return Ok();  // Busy, skip this frame
    }

    if (!running_.load()) {
        return Ok();
    }

    if (!renderer_) {
        return Ok();
    }

    // No damage = nothing to render
    if (!hasDamage()) {
        return Ok();
    }

    // Render the grid with damage tracking
    renderer_->render(grid_, damageRects_, fullDamage_,
                      cursorCol_, cursorRow_,
                      cursorVisible_ && cursorBlink_);

    // Clear damage after rendering
    damageRects_.clear();
    fullDamage_ = false;

    return Ok();
}

//=============================================================================
// Shell startup
//=============================================================================

Result<void> Terminal::startShell(const std::string& shell) {
#ifndef _WIN32
    std::string shellPath = shell.empty() ? (getenv("SHELL") ? getenv("SHELL") : "/bin/sh") : shell;

    spdlog::debug("Starting shell: {}", shellPath);

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

    spdlog::info("Terminal started: PTY fd={}, PID={}", ptyMaster_, childPid_);
    return Ok();
#else
    return Err<void>("Windows not implemented yet");
#endif
}

//=============================================================================
// Worker thread
//=============================================================================

void Terminal::workerLoop() {
    spdlog::debug("Terminal[{}]: worker started", id_);

    loop_ = new uv_loop_t;
    uv_loop_init(loop_);

    // Async handle for cross-thread signaling
    asyncHandle_ = new uv_async_t;
    uv_async_init(loop_, asyncHandle_, onAsync);
    asyncHandle_->data = this;

    // Cursor blink timer
    cursorTimer_ = new uv_timer_t;
    uv_timer_init(loop_, cursorTimer_);
    cursorTimer_->data = this;
    uv_timer_start(cursorTimer_, onTimer, 500, 500);

#ifndef _WIN32
    // PTY poll handle
    if (ptyMaster_ >= 0) {
        ptyPoll_ = new uv_poll_t;
        uv_poll_init(loop_, ptyPoll_, ptyMaster_);
        ptyPoll_->data = this;
        uv_poll_start(ptyPoll_, UV_READABLE, onPtyPoll);
    }
#endif

    // Run event loop
    while (!stopRequested_.load()) {
        uv_run(loop_, UV_RUN_ONCE);
    }

    // Cleanup
#ifndef _WIN32
    if (ptyPoll_) {
        uv_poll_stop(ptyPoll_);
        uv_close(reinterpret_cast<uv_handle_t*>(ptyPoll_), [](uv_handle_t* h) {
            delete reinterpret_cast<uv_poll_t*>(h);
        });
        ptyPoll_ = nullptr;
    }
#endif

    uv_timer_stop(cursorTimer_);
    uv_close(reinterpret_cast<uv_handle_t*>(cursorTimer_), [](uv_handle_t* h) {
        delete reinterpret_cast<uv_timer_t*>(h);
    });
    cursorTimer_ = nullptr;

    uv_close(reinterpret_cast<uv_handle_t*>(asyncHandle_), [](uv_handle_t* h) {
        delete reinterpret_cast<uv_async_t*>(h);
    });
    asyncHandle_ = nullptr;

    uv_run(loop_, UV_RUN_DEFAULT);
    uv_loop_close(loop_);
    delete loop_;
    loop_ = nullptr;

    spdlog::debug("Terminal[{}]: worker stopped", id_);
}

void Terminal::onAsync(uv_async_t* handle) {
    auto* self = static_cast<Terminal*>(handle->data);
    self->processPendingInput();
}

void Terminal::onTimer(uv_timer_t* handle) {
    auto* self = static_cast<Terminal*>(handle->data);
    double now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    std::lock_guard<std::mutex> lock(self->mutex_);
    self->updateCursorBlink(now);
}

void Terminal::onPtyPoll(uv_poll_t* handle, int status, int events) {
    auto* self = static_cast<Terminal*>(handle->data);

    if (status < 0) {
        spdlog::error("Terminal[{}]: PTY poll error: {}", self->id_, uv_strerror(status));
        return;
    }

    if (events & UV_READABLE) {
        std::lock_guard<std::mutex> lock(self->mutex_);
        if (auto res = self->readPty(); !res) {
            spdlog::error("Terminal[{}]: PTY read error: {}", self->id_, res.error().to_string());
        }
    }
}

void Terminal::processPendingInput() {
    std::vector<PendingKey> keys;
    std::vector<char> raw;
    bool doResize = false;
    uint32_t newCols = 0, newRows = 0;

    {
        std::lock_guard<std::mutex> lock(inputMutex_);
        keys = std::move(pendingKeys_);
        pendingKeys_.clear();
        raw = std::move(pendingRaw_);
        pendingRaw_.clear();
        doResize = pendingResize_;
        newCols = pendingCols_;
        newRows = pendingRows_;
        pendingResize_ = false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (doResize) {
        cols_ = newCols;
        rows_ = newRows;
        vterm_set_size(vterm_, rows_, cols_);
        grid_.resize(cols_, rows_);
#ifndef _WIN32
        if (ptyMaster_ >= 0) {
            struct winsize ws = {static_cast<unsigned short>(rows_), static_cast<unsigned short>(cols_), 0, 0};
            ioctl(ptyMaster_, TIOCSWINSZ, &ws);
        }
#endif
        syncToGrid();
    }

    for (const auto& key : keys) {
        if (key.isSpecial) {
            vterm_keyboard_key(vterm_, key.specialKey, key.mod);
        } else {
            vterm_keyboard_unichar(vterm_, key.codepoint, key.mod);
        }
        flushVtermOutput();
    }

    if (!raw.empty()) {
        writeToPty(raw.data(), raw.size());
    }
}

//=============================================================================
// PTY I/O
//=============================================================================

Result<void> Terminal::readPty() {
#ifndef _WIN32
    // Check child status
    int status;
    if (waitpid(childPid_, &status, WNOHANG) > 0) {
        running_.store(false);
        stopRequested_.store(true);
        spdlog::info("Shell exited");
        return Ok();
    }

    // Drain PTY
    size_t totalRead = 0;
    ssize_t n;
    while ((n = read(ptyMaster_, ptyReadBuffer_.get() + totalRead, PTY_READ_BUFFER_SIZE - totalRead)) > 0) {
        totalRead += n;
        if (totalRead >= PTY_READ_BUFFER_SIZE) break;
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
        syncToGrid();
    }

    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        return Err<void>(std::string("PTY read error: ") + strerror(errno));
    }

    return Ok();
#else
    return Err<void>("Windows not implemented");
#endif
}

Result<void> Terminal::writeToPty(const char* data, size_t len) {
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

Result<void> Terminal::flushVtermOutput() {
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
// Input API (thread-safe, queue for worker)
//=============================================================================

void Terminal::sendKey(uint32_t codepoint, VTermModifier mod) {
    std::lock_guard<std::mutex> lock(inputMutex_);
    pendingKeys_.push_back({codepoint, mod, false, VTERM_KEY_NONE});
    if (asyncHandle_) uv_async_send(asyncHandle_);
}

void Terminal::sendSpecialKey(VTermKey key, VTermModifier mod) {
    std::lock_guard<std::mutex> lock(inputMutex_);
    pendingKeys_.push_back({0, mod, true, key});
    if (asyncHandle_) uv_async_send(asyncHandle_);
}

void Terminal::sendRaw(const char* data, size_t len) {
    std::lock_guard<std::mutex> lock(inputMutex_);
    pendingRaw_.insert(pendingRaw_.end(), data, data + len);
    if (asyncHandle_) uv_async_send(asyncHandle_);
}

void Terminal::resize(uint32_t cols, uint32_t rows) {
    std::lock_guard<std::mutex> lock(inputMutex_);
    pendingResize_ = true;
    pendingCols_ = cols;
    pendingRows_ = rows;
    if (asyncHandle_) uv_async_send(asyncHandle_);
}

//=============================================================================
// Scrollback
//=============================================================================

void Terminal::scrollUp(int lines) {
    std::lock_guard<std::mutex> lock(mutex_);
    scrollOffset_ = std::min(scrollOffset_ + lines, static_cast<int>(scrollback_.size()));
    fullDamage_ = true;
}

void Terminal::scrollDown(int lines) {
    std::lock_guard<std::mutex> lock(mutex_);
    scrollOffset_ = std::max(scrollOffset_ - lines, 0);
    fullDamage_ = true;
}

void Terminal::scrollToTop() {
    std::lock_guard<std::mutex> lock(mutex_);
    scrollOffset_ = static_cast<int>(scrollback_.size());
    fullDamage_ = true;
}

void Terminal::scrollToBottom() {
    std::lock_guard<std::mutex> lock(mutex_);
    scrollOffset_ = 0;
    fullDamage_ = true;
}

//=============================================================================
// Selection
//=============================================================================

void Terminal::startSelection(int row, int col, SelectionMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    selectionStart_ = {row, col};
    selectionEnd_ = {row, col};
    selectionMode_ = mode;
    fullDamage_ = true;
}

void Terminal::extendSelection(int row, int col) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (selectionMode_ == SelectionMode::None) return;
    selectionEnd_ = {row, col};
    fullDamage_ = true;
}

void Terminal::clearSelection() {
    std::lock_guard<std::mutex> lock(mutex_);
    selectionMode_ = SelectionMode::None;
    fullDamage_ = true;
}

bool Terminal::isInSelection(int row, int col) const {
    if (selectionMode_ == SelectionMode::None) return false;

    VTermPos start = selectionStart_, end = selectionEnd_;
    if (vterm_pos_cmp(start, end) > 0) std::swap(start, end);

    VTermPos pos = {row, col};
    return vterm_pos_cmp(pos, start) >= 0 && vterm_pos_cmp(pos, end) <= 0;
}

std::string Terminal::getSelectedText() {
    std::lock_guard<std::mutex> lock(mutex_);
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
// Cell size / zoom
//=============================================================================

void Terminal::setCellSize(uint32_t width, uint32_t height) {
    cellWidth_ = width;
    cellHeight_ = height;
}

void Terminal::setBaseCellSize(float width, float height) {
    baseCellWidth_ = width;
    baseCellHeight_ = height;
}

void Terminal::setZoomLevel(float zoom) {
    zoomLevel_ = zoom;
    if (renderer_) {
        renderer_->setCellSize(baseCellWidth_ * zoom, baseCellHeight_ * zoom);
        renderer_->setScale(zoom);
    }
}

//=============================================================================
// Cursor blink
//=============================================================================

void Terminal::updateCursorBlink(double currentTime) {
    if (currentTime - lastBlinkTime_ >= blinkInterval_) {
        cursorBlink_ = !cursorBlink_;
        lastBlinkTime_ = currentTime;
    }
}

//=============================================================================
// Grid sync
//=============================================================================

void Terminal::syncToGrid() {
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
                for (uint32_t col = 0; col < cols_; col++) {
                    uint32_t cp = (col < sbLine._chars.size()) ? sbLine._chars[col] : ' ';
                    uint8_t fgR = 255, fgG = 255, fgB = 255;
                    uint8_t bgR = 0, bgG = 0, bgB = 0;
                    if (col < sbLine._chars.size()) {
                        fgR = sbLine._fgColors[col * 3];
                        fgG = sbLine._fgColors[col * 3 + 1];
                        fgB = sbLine._fgColors[col * 3 + 2];
                        bgR = sbLine._bgColors[col * 3];
                        bgG = sbLine._bgColors[col * 3 + 1];
                        bgB = sbLine._bgColors[col * 3 + 2];
                    }
                    uint16_t gi = font_ ? font_->getGlyphIndex(cp) : static_cast<uint16_t>(cp);
                    CellAttrs attrs;
                    attrs._emoji = isEmoji(cp) ? 1 : 0;
                    grid_.setCell(col, row, gi, fgR, fgG, fgB, bgR, bgG, bgB, attrs);
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

                // Determine glyph index:
                // 1. For emojis, use EmojiAtlas (dynamically load if needed)
                // 2. For regular text, use Font (MSDF)
                uint16_t gi;
                int emojiIdx = -1;

                if (isEmoji(cp) && emojiAtlas_) {
                    // Check if already loaded in EmojiAtlas
                    emojiIdx = emojiAtlas_->getEmojiIndex(cp);
                    if (emojiIdx < 0) {
                        // Try to load it dynamically
                        auto result = emojiAtlas_->loadEmoji(cp);
                        if (result && *result >= 0) {
                            emojiIdx = *result;
                        }
                    }

                    if (emojiIdx >= 0) {
                        gi = static_cast<uint16_t>(emojiIdx);
                    } else {
                        // Emoji not available in font, use space as fallback
                        gi = font_ ? font_->getGlyphIndex(' ') : static_cast<uint16_t>(' ');
                    }
                } else {
                    // Regular text glyph - use MSDF font
                    gi = font_ ? font_->getGlyphIndex(cp, isBold, isItalic) : static_cast<uint16_t>(cp);
                }

                if (pluginManager_) {
                    uint32_t w = cell.width > 0 ? cell.width : 1;
                    uint16_t customIdx = pluginManager_->onCellSync(col, row, cp, w);
                    if (customIdx != 0) gi = customIdx;
                }

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

void Terminal::syncDamageToGrid() {
    // Simplified - just use syncToGrid for now
    syncToGrid();
}

void Terminal::colorToRGB(const VTermColor& color, uint8_t& r, uint8_t& g, uint8_t& b) {
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

int Terminal::onDamage(VTermRect rect, void* user) {
    auto* term = static_cast<Terminal*>(user);
    DamageRect d;
    d._startCol = rect.start_col;
    d._startRow = rect.start_row;
    d._endCol = rect.end_col;
    d._endRow = rect.end_row;
    term->damageRects_.push_back(d);
    return 0;
}

int Terminal::onMoveCursor(VTermPos pos, VTermPos, int, void* user) {
    auto* term = static_cast<Terminal*>(user);
    term->cursorRow_ = pos.row;
    term->cursorCol_ = pos.col;
    return 0;
}

int Terminal::onSetTermProp(VTermProp prop, VTermValue* val, void* user) {
    auto* term = static_cast<Terminal*>(user);
    switch (prop) {
        case VTERM_PROP_ALTSCREEN:
            term->isAltScreen_ = val->boolean;
            if (term->pluginManager_) term->pluginManager_->onAltScreenChange(term->isAltScreen_);
            break;
        case VTERM_PROP_CURSORVISIBLE:
            term->cursorVisible_ = val->boolean;
            break;
        case VTERM_PROP_MOUSE:
            term->mouseMode_ = val->number;
            break;
        default: break;
    }
    return 0;
}

int Terminal::onResize(int, int, void*) { return 0; }

int Terminal::onBell(void*) {
#ifndef _WIN32
    write(STDOUT_FILENO, "\a", 1);
#endif
    return 0;
}

int Terminal::onSbPushline(int cols, const VTermScreenCell* cells, void* user) {
    auto* term = static_cast<Terminal*>(user);

    ScrollbackLine line;
    line._chars.resize(cols);
    line._fgColors.resize(cols * 3);
    line._bgColors.resize(cols * 3);

    for (int i = 0; i < cols; i++) {
        uint32_t ch = cells[i].chars[0];
        if (ch == 0xFFFFFFFF || ch == static_cast<uint32_t>(-1) || ch == 0) ch = ' ';
        line._chars[i] = ch;

        VTermColor fg = cells[i].fg, bg = cells[i].bg;
        vterm_screen_convert_color_to_rgb(term->vtermScreen_, &fg);
        vterm_screen_convert_color_to_rgb(term->vtermScreen_, &bg);
        line._fgColors[i * 3] = fg.rgb.red;
        line._fgColors[i * 3 + 1] = fg.rgb.green;
        line._fgColors[i * 3 + 2] = fg.rgb.blue;
        line._bgColors[i * 3] = bg.rgb.red;
        line._bgColors[i * 3 + 1] = bg.rgb.green;
        line._bgColors[i * 3 + 2] = bg.rgb.blue;
    }

    term->scrollback_.push_back(std::move(line));

    uint32_t maxLines = term->config_ ? term->config_->scrollbackLines() : 10000;
    while (term->scrollback_.size() > maxLines) {
        term->scrollback_.pop_front();
    }

    if (term->pluginManager_) {
        term->pluginManager_->onScroll(1, &term->grid_);
    }

    return 1;
}

int Terminal::onSbPopline(int cols, VTermScreenCell* cells, void* user) {
    auto* term = static_cast<Terminal*>(user);
    if (term->scrollback_.empty()) return 0;

    auto& line = term->scrollback_.back();
    int lineCols = std::min(cols, static_cast<int>(line._chars.size()));

    for (int i = 0; i < lineCols; i++) {
        cells[i].chars[0] = line._chars[i];
        cells[i].chars[1] = 0;
        cells[i].width = 1;
        cells[i].fg.type = VTERM_COLOR_RGB;
        cells[i].fg.rgb.red = line._fgColors[i * 3];
        cells[i].fg.rgb.green = line._fgColors[i * 3 + 1];
        cells[i].fg.rgb.blue = line._fgColors[i * 3 + 2];
        cells[i].bg.type = VTERM_COLOR_RGB;
        cells[i].bg.rgb.red = line._bgColors[i * 3];
        cells[i].bg.rgb.green = line._bgColors[i * 3 + 1];
        cells[i].bg.rgb.blue = line._bgColors[i * 3 + 2];
    }

    for (int i = lineCols; i < cols; i++) {
        cells[i].chars[0] = ' ';
        cells[i].chars[1] = 0;
        cells[i].width = 1;
        cells[i].fg.type = VTERM_COLOR_DEFAULT_FG;
        cells[i].bg.type = VTERM_COLOR_DEFAULT_BG;
    }

    term->scrollback_.pop_back();

    if (term->pluginManager_) {
        term->pluginManager_->onScroll(-1, &term->grid_);
    }

    return 1;
}

int Terminal::onMoverect(VTermRect, VTermRect, void* user) {
    auto* term = static_cast<Terminal*>(user);
    term->fullDamage_ = true;
    return 0;
}

int Terminal::onOSC(int command, VTermStringFragment frag, void* user) {
    auto* term = static_cast<Terminal*>(user);

    spdlog::debug("onOSC: command={} initial={} final={} len={}",
                  command, (bool)frag.initial, (bool)frag.final, (size_t)frag.len);

    if (command != YETTY_OSC_VENDOR_ID) {
        spdlog::debug("onOSC: ignoring non-yetty command {}", command);
        return 0;
    }

    if (frag.initial) {
        term->oscBuffer_.clear();
        term->oscCommand_ = command;
        spdlog::debug("onOSC: started new OSC buffer");
    }

    if (frag.len > 0) {
        term->oscBuffer_.append(frag.str, frag.len);
        spdlog::debug("onOSC: appended {} bytes, total={}", (size_t)frag.len, term->oscBuffer_.size());
    }

    if (frag.final && term->pluginManager_) {
        std::string fullSeq = std::to_string(command) + ";" + term->oscBuffer_;

        std::string response;
        uint32_t linesToAdvance = 0;

        bool handled = term->pluginManager_->handleOSCSequence(
            fullSeq, &term->grid_, term->cursorCol_, term->cursorRow_,
            term->cellWidth_, term->cellHeight_, &response, &linesToAdvance);

        spdlog::debug("onOSC: handled={} response_len={} linesToAdvance={}",
                      handled, response.size(), linesToAdvance);

        if (handled) {
            term->fullDamage_ = true;
            if (!response.empty() && term->ptyMaster_ >= 0) {
                term->writeToPty(response.c_str(), response.size());
            }
            if (linesToAdvance > 0) {
                term->pendingNewlines_ = linesToAdvance;
            }
        }
        term->oscBuffer_.clear();
        term->oscCommand_ = -1;
    } else if (frag.final) {
        spdlog::warn("onOSC: FINAL but no pluginManager!");
    }

    return 1;
}

} // namespace yetty
