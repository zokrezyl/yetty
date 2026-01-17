#include "terminal.h"
#include "widget-factory.h"
#include "emoji-atlas.h"
#include "grid-renderer.h"
#include "yetty/emoji.h"
#include <yetty/font-manager.h>
#include <ytrace/ytrace.hpp>

#include <algorithm>
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

// Generic create (for WidgetFactory)
Result<WidgetPtr> Terminal::create(WidgetFactory* factory,
                                    const WidgetParams& params,
                                    const std::string& payload) noexcept {
    yfunc();
    yinfo("Terminal::create(generic) factory={} payload.size={}", (void*)factory, payload.size());

    if (!factory) {
        ywarn("Terminal::create: null factory");
        return Err<WidgetPtr>("Terminal::create: null factory");
    }

    auto* fontManager = factory->getFontManager();
    yinfo("Terminal::create: fontManager={}", (void*)fontManager);
    if (!fontManager) {
        ywarn("Terminal::create: null FontManager");
        return Err<WidgetPtr>("Terminal::create: null FontManager");
    }
    auto* font = fontManager->getDefaultFont();
    yinfo("Terminal::create: font={}", (void*)font);
    if (!font) {
        ywarn("Terminal::create: null Font");
        return Err<WidgetPtr>("Terminal::create: null Font");
    }

    auto* loop = params.loop;
    yinfo("Terminal::create: loop={}", (void*)loop);
    if (!loop) {
        ywarn("Terminal::create: null libuv loop in params");
        return Err<WidgetPtr>("Terminal::create: null libuv loop in params");
    }

    // Use params dimensions or defaults
    uint32_t cols = params.widthCells > 0 ? params.widthCells : 80;
    uint32_t rows = params.heightCells > 0 ? params.heightCells : 24;
    yinfo("Terminal::create: cols={} rows={}", cols, rows);

    // Create with id=0, Widget base constructor will assign the actual ID
    auto term = std::shared_ptr<Terminal>(new Terminal(0, cols, rows, font, loop));
    yinfo("Terminal::create: terminal constructed id={}", term->id());

    // Store WidgetFactory for child widget creation
    term->_widgetFactory = factory;

    // Store config if available
    term->_config = factory->getConfig();
    yinfo("Terminal::create: config={}", (void*)term->_config);

    if (auto res = term->init(); !res) {
        ywarn("Terminal::create: init failed: {}", error_msg(res));
        return Err<WidgetPtr>("Failed to initialize Terminal", res);
    }
    yinfo("Terminal::create: init succeeded");

    // Parse shell from payload if provided
    if (!payload.empty()) {
        term->_shell = payload;
        yinfo("Terminal::create: shell={}", term->_shell);
    }

    yinfo("Terminal::create(generic): SUCCESS id={}", term->id());
    return Ok(std::static_pointer_cast<Widget>(term));
}

// Legacy create (for direct creation)
Result<Terminal::Ptr> Terminal::create(uint32_t id, uint32_t cols, uint32_t rows, Font* font, uv_loop_t* loop) noexcept {
    yfunc();
    yinfo("Terminal::create(legacy) id={} cols={} rows={} font={} loop={}", id, cols, rows, (void*)font, (void*)loop);

    if (!font) {
        ywarn("Terminal::create: null Font");
        return Err<Ptr>("Terminal::create: null Font");
    }
    if (!loop) {
        ywarn("Terminal::create: null libuv loop");
        return Err<Ptr>("Terminal::create: null libuv loop");
    }
    auto term = Ptr(new Terminal(id, cols, rows, font, loop));
    yinfo("Terminal::create: terminal constructed id={}", term->id());

    if (auto res = term->init(); !res) {
        ywarn("Terminal::create: init failed: {}", error_msg(res));
        return Err<Ptr>("Failed to initialize Terminal", res);
    }
    yinfo("Terminal::create(legacy): SUCCESS id={}", term->id());
    return Ok(std::move(term));
}

Terminal::Terminal(uint32_t id, uint32_t cols, uint32_t rows, Font* font, uv_loop_t* loop) noexcept
    : Widget()  // Widget base constructor assigns id from nextId_
    , _loop(loop)
    , _grid(cols, rows)
    , _font(font)
    , _cols(cols)
    , _rows(rows)
{
    // If explicit id provided, use it; otherwise keep Widget's auto-assigned id
    if (id > 0) {
        setId(id);
    }
    setName("terminal-" + std::to_string(_id));
}

Result<void> Terminal::init() noexcept {
    yfunc();
    yinfo("Terminal::init cols={} rows={}", _cols, _rows);

    _vterm = vterm_new(_rows, _cols);
    if (!_vterm) {
        ywarn("Terminal::init: Failed to create VTerm");
        return Err<void>("Failed to create VTerm");
    }
    yinfo("Terminal::init: vterm created");
    vterm_set_utf8(_vterm, 1);

    _vtermScreen = vterm_obtain_screen(_vterm);
    vterm_screen_set_callbacks(_vtermScreen, &screenCallbacks, this);
    vterm_screen_enable_altscreen(_vtermScreen, 1);
    vterm_screen_enable_reflow(_vtermScreen, true);
    vterm_screen_reset(_vtermScreen, 1);
    yinfo("Terminal::init: vterm screen configured");

    VTermState* state = vterm_obtain_state(_vterm);
    vterm_state_set_unrecognised_fallbacks(state, &stateFallbacks, this);

    _ptyReadBuffer = std::make_unique<char[]>(PTY_READ_BUFFER_SIZE);

    yinfo("Terminal::init: SUCCESS");
    return Ok();
}

Terminal::~Terminal() {
    stop();

#ifndef _WIN32
    if (_ptyMaster >= 0) {
        close(_ptyMaster);
    }
    if (_childPid > 0) {
        kill(_childPid, SIGTERM);
        waitpid(_childPid, nullptr, 0);
    }
#endif

    if (_vterm) {
        vterm_free(_vterm);
    }
}

//=============================================================================
// Widget interface
//=============================================================================

void Terminal::start() {
    yfunc();
    yinfo("Terminal::start _running={} _shell={}", _running.load(), _shell);

    if (_running) {
        yinfo("Terminal::start: already running, skipping");
        return;
    }

    // Start shell
    yinfo("Terminal::start: calling startShell");
    if (auto res = startShell(_shell); !res) {
        ywarn("Terminal::start: startShell failed: {}", res.error().to_string());
        yerror("Terminal: failed to start shell: {}", res.error().to_string());
        return;
    }
    yinfo("Terminal::start: shell started successfully");

    _running = true;

    // Set up cursor blink timer (500ms)
    yinfo("Terminal::start: setting up cursor timer");
    _cursorTimer = new uv_timer_t;
    uv_timer_init(_loop, _cursorTimer);
    _cursorTimer->data = this;
    uv_timer_start(_cursorTimer, onTimer, 500, 500);

#ifndef _WIN32
    // Set up PTY poll handle on external loop
    yinfo("Terminal::start: setting up PTY poll, _ptyMaster={}", _ptyMaster);
    if (_ptyMaster >= 0) {
        _ptyPoll = new uv_poll_t;
        uv_poll_init(_loop, _ptyPoll, _ptyMaster);
        _ptyPoll->data = this;
        uv_poll_start(_ptyPoll, UV_READABLE, onPtyPoll);
        yinfo("Terminal::start: PTY poll started");
    }
#endif
    yinfo("Terminal::start: SUCCESS");
}

void Terminal::stop() {
    if (!_running) return;

    _running = false;

#ifndef _WIN32
    if (_ptyPoll) {
        uv_poll_stop(_ptyPoll);
        uv_close(reinterpret_cast<uv_handle_t*>(_ptyPoll), [](uv_handle_t* h) {
            delete reinterpret_cast<uv_poll_t*>(h);
        });
        _ptyPoll = nullptr;
    }
#endif

    if (_cursorTimer) {
        uv_timer_stop(_cursorTimer);
        uv_close(reinterpret_cast<uv_handle_t*>(_cursorTimer), [](uv_handle_t* h) {
            delete reinterpret_cast<uv_timer_t*>(h);
        });
        _cursorTimer = nullptr;
    }
}

void Terminal::prepareFrame(WebGPUContext& ctx, bool on) {
    (void)on;  // Terminal is always on when called

    // Get current screen type for filtering
    ScreenType currentScreen = _isAltScreen ? ScreenType::Alternate : ScreenType::Main;

    // Propagate RenderContext and prepareFrame to all child widgets
    for (const auto& widget : _childWidgets) {
        if (!widget->isVisible()) continue;
        if (widget->getScreenType() != currentScreen) continue;

        // Propagate our RenderContext to children
        widget->setRenderContext(_renderCtx);

        // TODO: Calculate if widget is visible based on scroll position
        bool widgetOn = true;

        // Call prepareFrame on child (for texture-based widgets)
        widget->prepareFrame(ctx, widgetOn);
    }
}

Result<void> Terminal::render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on) {
    (void)ctx;
    (void)on;  // Terminal is always on when called

    if (!_running) {
        return Ok();
    }

    // Sync grid from vterm when there's damage
    if (_fullDamage) {
        syncToGrid();
    } else if (!_damageRects.empty()) {
        syncDamageToGrid();
    }

    // Render grid - renderToPass uploads to GPU only when damage exists
    if (_renderer) {
        _renderer->renderToPass(pass, _grid, _damageRects, _fullDamage,
                                _cursorCol, _cursorRow,
                                _cursorVisible && _cursorBlink);
    }

    // Clear damage after renderToPass has used it
    _damageRects.clear();
    _fullDamage = false;

    // Child widgets decide themselves if they need to render
    ScreenType currentScreen = _isAltScreen ? ScreenType::Alternate : ScreenType::Main;
    for (const auto& widget : _childWidgets) {
        if (!widget->isVisible()) continue;
        if (widget->getScreenType() != currentScreen) continue;

        // TODO: Calculate if widget is visible based on scroll position
        bool widgetOn = true;

        if (auto res = widget->render(pass, ctx, widgetOn); !res) {
            yerror("Terminal: widget '{}' render failed: {}", widget->name(), res.error().message());
        }
    }

    return Ok();
}

//=============================================================================
// Shell startup
//=============================================================================

Result<void> Terminal::startShell(const std::string& shell) {
    yfunc();
    yinfo("Terminal::startShell shell={}", shell);

#ifndef _WIN32
    std::string shellPath = shell.empty() ? (getenv("SHELL") ? getenv("SHELL") : "/bin/sh") : shell;
    yinfo("Terminal::startShell: shellPath={}", shellPath);

    ydebug("Starting shell: {}", shellPath);

    struct winsize ws;
    ws.ws_row = _rows;
    ws.ws_col = _cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    yinfo("Terminal::startShell: winsize rows={} cols={}", ws.ws_row, ws.ws_col);

    yinfo("Terminal::startShell: calling forkpty");
    _childPid = forkpty(&_ptyMaster, nullptr, nullptr, &ws);
    yinfo("Terminal::startShell: forkpty returned _childPid={} _ptyMaster={}", _childPid, _ptyMaster);

    if (_childPid < 0) {
        ywarn("Terminal::startShell: forkpty failed: {}", strerror(errno));
        return Err<void>(std::string("forkpty failed: ") + strerror(errno));
    }

    if (_childPid == 0) {
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
    int flags = fcntl(_ptyMaster, F_GETFL, 0);
    fcntl(_ptyMaster, F_SETFL, flags | O_NONBLOCK);
    yinfo("Terminal::startShell: PTY set to non-blocking");

    yinfo("Terminal::startShell: SUCCESS _ptyMaster={} _childPid={}", _ptyMaster, _childPid);
    yinfo("Terminal started: PTY fd={}, PID={}", _ptyMaster, _childPid);
    return Ok();
#else
    ywarn("Terminal::startShell: Windows not implemented yet");
    return Err<void>("Windows not implemented yet");
#endif
}

//=============================================================================
// libuv callbacks
//=============================================================================

void Terminal::onTimer(uv_timer_t* handle) {
    auto* self = static_cast<Terminal*>(handle->data);
    double now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    self->updateCursorBlink(now);
}

void Terminal::onPtyPoll(uv_poll_t* handle, int status, int events) {
    auto* self = static_cast<Terminal*>(handle->data);

    if (status < 0) {
        yerror("Terminal[{}]: PTY poll error: {}", self->_id, uv_strerror(status));
        return;
    }

    if (events & UV_READABLE) {
        if (auto res = self->readPty(); !res) {
            yerror("Terminal[{}]: PTY read error: {}", self->_id, res.error().to_string());
        }
    }
}

//=============================================================================
// Keyboard input (direct write, no queueing)
//=============================================================================

void Terminal::sendKey(uint32_t codepoint, VTermModifier mod) {
    if (_scrollOffset != 0) {
        _scrollOffset = 0;
        _fullDamage = true;
    }
    vterm_keyboard_unichar(_vterm, codepoint, mod);
    flushVtermOutput();
}

void Terminal::sendSpecialKey(VTermKey key, VTermModifier mod) {
    if (_scrollOffset != 0) {
        _scrollOffset = 0;
        _fullDamage = true;
    }
    vterm_keyboard_key(_vterm, key, mod);
    flushVtermOutput();
}

void Terminal::sendRaw(const char* data, size_t len) {
    if (_scrollOffset != 0) {
        _scrollOffset = 0;
        _fullDamage = true;
    }
    writeToPty(data, len);
}

void Terminal::resize(uint32_t cols, uint32_t rows) {
    _cols = cols;
    _rows = rows;
    vterm_set_size(_vterm, _rows, _cols);
    _grid.resize(_cols, _rows);
#ifndef _WIN32
    if (_ptyMaster >= 0) {
        struct winsize ws = {static_cast<unsigned short>(_rows), static_cast<unsigned short>(_cols), 0, 0};
        ioctl(_ptyMaster, TIOCSWINSZ, &ws);
    }
#endif
    _fullDamage = true;
}

//=============================================================================
// PTY I/O
//=============================================================================

Result<void> Terminal::readPty() {
#ifndef _WIN32
    // Check child status
    int status;
    if (waitpid(_childPid, &status, WNOHANG) > 0) {
        _running = false;
        yinfo("Shell exited");
        return Ok();
    }

    // Don't try to read if not running
    if (!_running) {
        return Ok();
    }

    // Drain PTY - read as much as available up to 40KB
    size_t totalRead = 0;
    ssize_t n;
    while ((n = read(_ptyMaster, _ptyReadBuffer.get() + totalRead, PTY_READ_BUFFER_SIZE - totalRead)) > 0) {
        totalRead += n;
        if (totalRead >= PTY_READ_BUFFER_SIZE) break;
    }
    
    // Handle read errors
    if (totalRead == 0 && n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No data available, this is normal for non-blocking read
            return Ok();
        }
        if (errno == EIO) {
            // EIO means the slave side was closed (shell exited)
            _running = false;
            yinfo("Shell exited (PTY closed)");
            return Ok();
        }
        // Other errors are real problems
        return Err<void>(std::string("PTY read error: ") + strerror(errno));
    }

    if (totalRead > 0) {
        vterm_input_write(_vterm, _ptyReadBuffer.get(), totalRead);
        vterm_screen_flush_damage(_vtermScreen);
        flushVtermOutput();
        // NOTE: syncToGrid is NOT called here - render() will sync based on damage
    }

    return Ok();
#else
    return Err<void>("Windows not implemented");
#endif
}

Result<void> Terminal::writeToPty(const char* data, size_t len) {
    if (len == 0) return Ok();
#ifndef _WIN32
    if (_ptyMaster < 0) return Err("PTY not open");
    ssize_t written = write(_ptyMaster, data, len);
    if (written < 0) {
        return Err("PTY write failed: " + std::string(strerror(errno)));
    }
#endif
    return Ok();
}

Result<void> Terminal::flushVtermOutput() {
    size_t outlen = vterm_output_get_buffer_current(_vterm);
    char buf[65536];
    while (outlen > 0) {
        size_t n = vterm_output_read(_vterm, buf, sizeof(buf));
        if (n > 0) {
            if (auto res = writeToPty(buf, n); !res) return res;
        }
        outlen = vterm_output_get_buffer_current(_vterm);
    }
    return Ok();
}

//=============================================================================
// Scrollback
//=============================================================================

void Terminal::scrollUp(int lines) {
    int oldOffset = _scrollOffset;
    int sbSize = static_cast<int>(_scrollback.size());
    _scrollOffset = std::min(_scrollOffset + lines, sbSize);
    ydebug("Terminal::scrollUp: lines={}, _scrollback.size()={}, oldOffset={}, newOffset={}",
                  lines, sbSize, oldOffset, _scrollOffset);
    _fullDamage = true;
}

void Terminal::scrollDown(int lines) {
    int oldOffset = _scrollOffset;
    _scrollOffset = std::max(_scrollOffset - lines, 0);
    ydebug("Terminal::scrollDown: lines={}, oldOffset={}, newOffset={}",
                  lines, oldOffset, _scrollOffset);
    _fullDamage = true;
}

void Terminal::scrollToTop() {
    _scrollOffset = static_cast<int>(_scrollback.size());
    _fullDamage = true;
}

void Terminal::scrollToBottom() {
    _scrollOffset = 0;
    _fullDamage = true;
}

//=============================================================================
// Selection
//=============================================================================

void Terminal::startSelection(int row, int col, SelectionMode mode) {
    _selectionStart = {row, col};
    _selectionEnd = {row, col};
    _selectionMode = mode;
    _fullDamage = true;
}

void Terminal::extendSelection(int row, int col) {
    if (_selectionMode == SelectionMode::None) return;
    _selectionEnd = {row, col};
    _fullDamage = true;
}

void Terminal::clearSelection() {
    _selectionMode = SelectionMode::None;
    _fullDamage = true;
}

bool Terminal::isInSelection(int row, int col) const {
    if (_selectionMode == SelectionMode::None) return false;

    VTermPos start = _selectionStart, end = _selectionEnd;
    if (vterm_pos_cmp(start, end) > 0) std::swap(start, end);

    VTermPos pos = {row, col};
    return vterm_pos_cmp(pos, start) >= 0 && vterm_pos_cmp(pos, end) <= 0;
}

std::string Terminal::getSelectedText() {
    if (_selectionMode == SelectionMode::None) return "";

    VTermPos start = _selectionStart, end = _selectionEnd;
    if (vterm_pos_cmp(start, end) > 0) std::swap(start, end);

    std::string result;
    for (int row = start.row; row <= end.row; row++) {
        int sc = (row == start.row) ? start.col : 0;
        int ec = (row == end.row) ? end.col + 1 : static_cast<int>(_cols);

        VTermRect rect = {row, row + 1, sc, ec};
        char buf[1024];
        size_t len = vterm_screen_get_text(_vtermScreen, buf, sizeof(buf), rect);
        if (len > 0) result.append(buf, len);
        if (row < end.row) result += '\n';
    }
    return result;
}

//=============================================================================
// Cell size / zoom
//=============================================================================

void Terminal::setCellSize(uint32_t width, uint32_t height) {
    _cellWidth = width;
    _cellHeight = height;
}

void Terminal::setBaseCellSize(float width, float height) {
    _baseCellWidth = width;
    _baseCellHeight = height;
}

void Terminal::setZoomLevel(float zoom) {
    _zoomLevel = zoom;
    if (_renderer) {
        _renderer->setCellSize(_baseCellWidth * zoom, _baseCellHeight * zoom);
        _renderer->setScale(zoom);
    }
}

//=============================================================================
// Cursor blink
//=============================================================================

void Terminal::updateCursorBlink(double currentTime) {
    if (currentTime - _lastBlinkTime >= _blinkInterval) {
        _cursorBlink = !_cursorBlink;
        _lastBlinkTime = currentTime;
        // Mark cursor cell as damaged so it gets re-rendered
        if (_cursorVisible) {
            DamageRect d;
            d._startRow = static_cast<uint32_t>(_cursorRow);
            d._startCol = static_cast<uint32_t>(_cursorCol);
            d._endRow = static_cast<uint32_t>(_cursorRow + 1);  // exclusive
            d._endCol = static_cast<uint32_t>(_cursorCol + 1);  // exclusive
            _damageRects.push_back(d);
        }
    }
}

//=============================================================================
// Grid sync
//=============================================================================

void Terminal::syncToGrid() {
    VTermScreenCell cell;
    VTermPos pos;

    int sbSize = static_cast<int>(_scrollback.size());
    int effectiveOffset = std::min(_scrollOffset, sbSize);

    for (uint32_t row = 0; row < _rows; row++) {
        int lineIndex = static_cast<int>(row) - effectiveOffset;

        if (lineIndex < 0) {
            // Scrollback
            int sbIndex = sbSize + lineIndex;
            if (sbIndex >= 0 && sbIndex < sbSize) {
                const auto& sbLine = _scrollback[sbIndex];
                
                // Decode RLE styles for this line
                size_t runIdx = 0;
                int currentRunRemaining = 0;
                ScrollbackStyle currentStyle = {};
                if (!sbLine.styleRuns.empty()) {
                    currentStyle = sbLine.styleRuns[0].style;
                    currentRunRemaining = sbLine.styleRuns[0].count;
                }
                
                for (uint32_t col = 0; col < _cols; col++) {
                    uint32_t cp = (col < sbLine.chars.size()) ? sbLine.chars[col] : ' ';
                    uint8_t fgR = 255, fgG = 255, fgB = 255;
                    uint8_t bgR = 0, bgG = 0, bgB = 0;
                    
                    // Get style from RLE
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
                    
                    uint16_t gi = _font ? _font->getGlyphIndex(cp, isBold, isItalic) : static_cast<uint16_t>(cp);
                    CellAttrs cellAttrs;
                    cellAttrs._emoji = isEmoji(cp) ? 1 : 0;
                    cellAttrs._bold = isBold ? 1 : 0;
                    cellAttrs._italic = isItalic ? 1 : 0;
                    cellAttrs._underline = isUnderline ? 1 : 0;
                    cellAttrs._strikethrough = isStrike ? 1 : 0;
                    _grid.setCell(col, row, gi, fgR, fgG, fgB, bgR, bgG, bgB, cellAttrs);
                }
            }
        } else {
            // VTerm screen
            for (uint32_t col = 0; col < _cols; col++) {
                pos.row = lineIndex;
                pos.col = col;
                vterm_screen_get_cell(_vtermScreen, pos, &cell);

                uint32_t cp = cell.chars[0];
                if (cp == 0xFFFFFFFF || cp == static_cast<uint32_t>(-1)) {
                    // Wide char continuation
                    uint8_t fgR, fgG, fgB, bgR, bgG, bgB;
                    VTermColor fg = cell.fg, bg = cell.bg;
                    vterm_screen_convert_color_to_rgb(_vtermScreen, &fg);
                    vterm_screen_convert_color_to_rgb(_vtermScreen, &bg);
                    colorToRGB(fg, fgR, fgG, fgB);
                    colorToRGB(bg, bgR, bgG, bgB);
                    if (cell.attrs.reverse) {
                        std::swap(fgR, bgR); std::swap(fgG, bgG); std::swap(fgB, bgB);
                    }
                    _grid.setCell(col, row, GLYPH_WIDE_CONT, fgR, fgG, fgB, bgR, bgG, bgB);
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

                if (isEmoji(cp) && _emojiAtlas) {
                    // Check if already loaded in EmojiAtlas
                    emojiIdx = _emojiAtlas->getEmojiIndex(cp);
                    if (emojiIdx < 0) {
                        // Try to load it dynamically
                        auto result = _emojiAtlas->loadEmoji(cp);
                        if (result && *result >= 0) {
                            emojiIdx = *result;
                        }
                    }

                    if (emojiIdx >= 0) {
                        gi = static_cast<uint16_t>(emojiIdx);
                    } else {
                        // Emoji not available in font, use space as fallback
                        gi = _font ? _font->getGlyphIndex(' ') : static_cast<uint16_t>(' ');
                    }
                } else {
                    // Regular text glyph - use MSDF font
                    gi = _font ? _font->getGlyphIndex(cp, isBold, isItalic) : static_cast<uint16_t>(cp);
                }

                // Plugin-based custom glyph rendering removed - widgets handle their own rendering

                uint8_t fgR, fgG, fgB, bgR, bgG, bgB;
                VTermColor fg = cell.fg, bg = cell.bg;
                vterm_screen_convert_color_to_rgb(_vtermScreen, &fg);
                vterm_screen_convert_color_to_rgb(_vtermScreen, &bg);
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

                _grid.setCell(col, row, gi, fgR, fgG, fgB, bgR, bgG, bgB, attrs);
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

void Terminal::updateWidgetPositionsOnScroll(int lines) {
    for (auto& widget : _childWidgets) {
        if (widget->getPositionMode() == PositionMode::Relative) {
            // Clear old grid cell marks
            clearWidgetGridCells(widget.get());
            // Update position (content moved up = widget y decreases)
            widget->setPosition(widget->getX(), widget->getY() - lines);
            // Re-mark grid cells at new position
            markWidgetGridCells(widget.get());
        }
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
    term->_damageRects.push_back(d);
    return 0;
}

int Terminal::onMoveCursor(VTermPos pos, VTermPos, int, void* user) {
    auto* term = static_cast<Terminal*>(user);
    term->_cursorRow = pos.row;
    term->_cursorCol = pos.col;
    return 0;
}

int Terminal::onSetTermProp(VTermProp prop, VTermValue* val, void* user) {
    auto* term = static_cast<Terminal*>(user);
    switch (prop) {
        case VTERM_PROP_ALTSCREEN:
            term->_isAltScreen = val->boolean;
            // Alt screen change notification removed - widgets track via RenderContext
            break;
        case VTERM_PROP_CURSORVISIBLE:
            term->_cursorVisible = val->boolean;
            break;
        case VTERM_PROP_MOUSE:
            term->_mouseMode = val->number;
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

    // Helper to pack attrs into uint16_t
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
        vterm_screen_convert_color_to_rgb(term->_vtermScreen, &fg);
        vterm_screen_convert_color_to_rgb(term->_vtermScreen, &bg);

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

    term->_scrollback.push_back(std::move(line));

    uint32_t maxLines = term->_config ? term->_config->scrollbackLines() : 10000;
    while (term->_scrollback.size() > maxLines) {
        term->_scrollback.pop_front();
    }

    // Update widget positions - content scrolled up by 1 line
    term->updateWidgetPositionsOnScroll(1);

    return 1;
}

int Terminal::onSbPopline(int cols, VTermScreenCell* cells, void* user) {
    auto* term = static_cast<Terminal*>(user);
    if (term->_scrollback.empty()) return 0;

    auto& line = term->_scrollback.back();
    int lineCols = std::min(cols, static_cast<int>(line.chars.size()));

    // Helper to unpack attrs from uint16_t
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

    // Decode RLE styles
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

    term->_scrollback.pop_back();

    // Update widget positions - content scrolled down by 1 line
    term->updateWidgetPositionsOnScroll(-1);

    return 1;
}

int Terminal::onMoverect(VTermRect, VTermRect, void* user) {
    auto* term = static_cast<Terminal*>(user);
    term->_fullDamage = true;
    return 0;
}

int Terminal::onOSC(int command, VTermStringFragment frag, void* user) {
    auto* term = static_cast<Terminal*>(user);

    ydebug("onOSC: command={} initial={} final={} len={}",
                  command, (bool)frag.initial, (bool)frag.final, (size_t)frag.len);

    if (command != YETTY_OSC_VENDOR_ID) {
        ydebug("onOSC: ignoring non-yetty command {}", command);
        return 0;
    }

    if (frag.initial) {
        term->_oscBuffer.clear();
        term->_oscCommand = command;
        ydebug("onOSC: started new OSC buffer");
    }

    if (frag.len > 0) {
        term->_oscBuffer.append(frag.str, frag.len);
        ydebug("onOSC: appended {} bytes, total={}", (size_t)frag.len, term->_oscBuffer.size());
    }

    if (frag.final && term->_widgetFactory) {
        std::string fullSeq = std::to_string(command) + ";" + term->_oscBuffer;

        std::string response;
        uint32_t linesToAdvance = 0;

        // Use Terminal's own OSC handling (via WidgetFactory)
        bool handled = term->handleOSCSequence(fullSeq, &response, &linesToAdvance);

        ydebug("onOSC: handled={} response_len={} linesToAdvance={}",
                      handled, response.size(), linesToAdvance);

        if (handled) {
            term->_fullDamage = true;
            if (!response.empty() && term->_ptyMaster >= 0) {
                term->writeToPty(response.c_str(), response.size());
            }
            // Inject newlines immediately to advance cursor past the widget
            // This must happen during OSC processing, not deferred, otherwise
            // any prompt text that follows in the same PTY read gets displayed
            // before the cursor advances
            if (linesToAdvance > 0) {
                std::string nl(linesToAdvance, '\n');
                vterm_input_write(term->_vterm, nl.c_str(), nl.size());
                vterm_screen_flush_damage(term->_vtermScreen);
            }
        }
        term->_oscBuffer.clear();
        term->_oscCommand = -1;
    } else if (frag.final) {
        ywarn("onOSC: FINAL but no widgetFactory!");
    }

    return 1;
}

//=============================================================================
// Widget ownership (new architecture)
//=============================================================================

void Terminal::addChildWidget(WidgetPtr widget) {
    if (!widget) return;
    _childWidgets.push_back(widget);
    // Sort by zOrder for correct render order
    std::sort(_childWidgets.begin(), _childWidgets.end(),
              [](const WidgetPtr& a, const WidgetPtr& b) {
                  return a->zOrder() < b->zOrder();
              });
}

void Terminal::markWidgetGridCells(Widget* widget) {
    if (!widget) return;

    int32_t startX = widget->getX();
    int32_t startY = widget->getY();
    uint32_t w = widget->getWidthCells();
    uint32_t h = widget->getHeightCells();
    uint16_t id = static_cast<uint16_t>(widget->id());

    ydebug("markWidgetGridCells: widget {} at ({},{}) size {}x{} id={}",
           widget->name(), startX, startY, w, h, id);

    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            int32_t gridCol = startX + col;
            int32_t gridRow = startY + row;

            if (gridCol >= 0 && gridCol < (int32_t)_grid.getCols() &&
                gridRow >= 0 && gridRow < (int32_t)_grid.getRows()) {
                _grid.setWidgetId(gridCol, gridRow, id);
            }
        }
    }
}

void Terminal::clearWidgetGridCells(Widget* widget) {
    if (!widget) return;

    int32_t startX = widget->getX();
    int32_t startY = widget->getY();
    uint32_t w = widget->getWidthCells();
    uint32_t h = widget->getHeightCells();

    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            int32_t gridCol = startX + col;
            int32_t gridRow = startY + row;

            if (gridCol >= 0 && gridCol < (int32_t)_grid.getCols() &&
                gridRow >= 0 && gridRow < (int32_t)_grid.getRows()) {
                _grid.clearWidgetId(gridCol, gridRow);
            }
        }
    }
}

Result<void> Terminal::removeChildWidget(uint32_t id) {
    for (auto it = _childWidgets.begin(); it != _childWidgets.end(); ++it) {
        if ((*it)->id() == id) {
            clearWidgetGridCells(it->get());
            if (auto res = (*it)->dispose(); !res) {
                return Err<void>("Failed to dispose widget " + std::to_string(id), res);
            }
            _childWidgets.erase(it);
            return Ok();
        }
    }
    return Err<void>("Widget not found: " + std::to_string(id));
}

WidgetPtr Terminal::getChildWidget(uint32_t id) const {
    for (const auto& widget : _childWidgets) {
        if (widget->id() == id) return widget;
    }
    return nullptr;
}

WidgetPtr Terminal::getChildWidgetByHashId(const std::string& hashId) const {
    auto it = _childWidgetsByHashId.find(hashId);
    return (it != _childWidgetsByHashId.end()) ? it->second : nullptr;
}

Result<void> Terminal::removeChildWidgetByHashId(const std::string& hashId) {
    auto it = _childWidgetsByHashId.find(hashId);
    if (it == _childWidgetsByHashId.end()) {
        return Err<void>("Widget not found: " + hashId);
    }

    WidgetPtr widget = it->second;
    _childWidgetsByHashId.erase(it);

    // Clear grid cells and remove from _childWidgets vector
    clearWidgetGridCells(widget.get());
    for (auto vit = _childWidgets.begin(); vit != _childWidgets.end(); ++vit) {
        if ((*vit)->id() == widget->id()) {
            if (auto res = (*vit)->dispose(); !res) {
                return Err<void>("Failed to dispose widget", res);
            }
            _childWidgets.erase(vit);
            break;
        }
    }
    return Ok();
}

//=============================================================================
// OSC Sequence Handling
//=============================================================================

bool Terminal::handleOSCSequence(const std::string& sequence,
                                  std::string* response,
                                  uint32_t* linesToAdvance) {
    if (!_widgetFactory) {
        yerror("Terminal::handleOSCSequence: no WidgetFactory!");
        if (response) *response = OscResponse::error("No WidgetFactory");
        return false;
    }

    auto parseResult = _oscParser.parse(sequence);
    if (!parseResult) {
        if (response) *response = OscResponse::error(error_msg(parseResult));
        return false;
    }

    OscCommand cmd = *parseResult;
    if (!cmd.isValid()) {
        if (response) *response = OscResponse::error(cmd.error);
        return false;
    }

    switch (cmd.type) {
        case OscCommandType::Create: {
            int32_t x = cmd.create.x;
            int32_t y = cmd.create.y;

            if (cmd.create.relative) {
                x += _cursorCol;
                y += _cursorRow;
            }

            // Build widget name: "plugin" or "plugin.type"
            std::string widgetName = cmd.create.plugin;

            // Build plugin args string - include both position args and user-provided args
            std::string pluginArgs = cmd.pluginArgs;
            if (cmd.create.relative) {
                if (!pluginArgs.empty()) {
                    pluginArgs += " ";
                }
                pluginArgs += "--relative";
            }

            auto result = _widgetFactory->createWidget(
                widgetName,
                x, y,
                static_cast<uint32_t>(cmd.create.width),
                static_cast<uint32_t>(cmd.create.height),
                pluginArgs,
                cmd.payload
            );
            if (!result) {
                yerror("Terminal: createWidget failed: {}", error_msg(result));
                if (response) *response = OscResponse::error(error_msg(result));
                return false;
            }

            WidgetPtr widget = *result;

            // Configure widget
            widget->setId(_nextChildWidgetId++);
            widget->setHashId(_oscParser.generateId());
            widget->setScreenType(_isAltScreen ? ScreenType::Alternate : ScreenType::Main);
            if (cmd.create.relative) {
                widget->setPositionMode(PositionMode::Relative);
            }

            // Add to Terminal
            addChildWidget(widget);
            _childWidgetsByHashId[widget->hashId()] = widget;

            // Mark grid cells with widget ID for mouse hit testing
            markWidgetGridCells(widget.get());

            yinfo("Terminal: Created widget {} plugin={} at ({},{}) size {}x{}",
                         widget->hashId(), cmd.create.plugin, x, y,
                         widget->getWidthCells(), widget->getHeightCells());

            if (linesToAdvance && cmd.create.relative) {
                *linesToAdvance = std::abs(static_cast<int>(widget->getHeightCells()));
            }
            return true;
        }

        case OscCommandType::List: {
            if (response) {
                std::vector<std::tuple<std::string, std::string, int, int, int, int, bool>> widgetList;
                for (const auto& widget : _childWidgets) {
                    if (!cmd.list.all && !widget->isRunning()) continue;
                    widgetList.emplace_back(
                        widget->hashId(),
                        widget->name(),
                        widget->getX(),
                        widget->getY(),
                        widget->getWidthCells(),
                        widget->getHeightCells(),
                        widget->isRunning()
                    );
                }
                *response = OscResponse::widgetList(widgetList);
            }
            return true;
        }

        case OscCommandType::Kill: {
            if (!cmd.target.id.empty()) {
                auto res = removeChildWidgetByHashId(cmd.target.id);
                if (!res) {
                    if (response) *response = OscResponse::error(error_msg(res));
                    return false;
                }
            } else {
                if (response) *response = OscResponse::error("kill: --id required");
                return false;
            }
            return true;
        }

        case OscCommandType::Stop: {
            if (!cmd.target.id.empty()) {
                auto widget = getChildWidgetByHashId(cmd.target.id);
                if (widget) {
                    widget->stop();
                } else {
                    if (response) *response = OscResponse::error("Widget not found: " + cmd.target.id);
                    return false;
                }
            } else {
                if (response) *response = OscResponse::error("stop: --id required");
                return false;
            }
            return true;
        }

        case OscCommandType::Start: {
            if (!cmd.target.id.empty()) {
                auto widget = getChildWidgetByHashId(cmd.target.id);
                if (widget) {
                    widget->start();
                } else {
                    if (response) *response = OscResponse::error("Widget not found: " + cmd.target.id);
                    return false;
                }
            } else {
                if (response) *response = OscResponse::error("start: --id required");
                return false;
            }
            return true;
        }

        case OscCommandType::Update: {
            if (cmd.target.id.empty()) {
                if (response) *response = OscResponse::error("update: --id required");
                return false;
            }
            auto widget = getChildWidgetByHashId(cmd.target.id);
            if (!widget) {
                if (response) *response = OscResponse::error("Widget not found: " + cmd.target.id);
                return false;
            }
            if (auto res = widget->dispose(); !res) {
                if (response) *response = OscResponse::error("Failed to dispose widget");
                return false;
            }
            widget->setPayload(cmd.payload);
            if (auto res = widget->reinit(); !res) {
                if (response) *response = OscResponse::error("Widget re-init failed");
                return false;
            }
            return true;
        }

        case OscCommandType::Plugins: {
            if (response) {
                *response = OscResponse::pluginList(_widgetFactory->getAvailableWidgets());
            }
            return true;
        }

        default:
            if (response) *response = OscResponse::error("unknown command");
            return false;
    }
}

} // namespace yetty
