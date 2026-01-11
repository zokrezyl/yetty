#pragma once

#include <yetty/widget.h>
#include <yetty/font.h>
#include <yetty/config.h>
#include <yetty/result.hpp>
#include <yetty/osc-command.h>
#include "grid.h"
#include "terminal-backend.h"  // For SelectionMode, ScrollbackStyle, ScrollbackLine

extern "C" {
#include <vterm.h>
}

#include <uv.h>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// DamageRect is in separate header to avoid libuv dependency for grid-renderer on web
#include "damage-rect.h"

namespace yetty {

// Forward declarations
class WidgetFactory;
class EmojiAtlas;
class GridRenderer;

// Types are now defined in terminal-backend.h:
// - SelectionMode
// - ScrollbackStyle
// - StyleRun
// - ScrollbackLine

//=============================================================================
// Terminal - Widget-based terminal emulator with libuv-based async PTY I/O
//
// Threading model:
//   - Single-threaded: uses external libuv loop (from Yetty)
//   - PTY read events trigger vterm updates
//   - render() syncs grid from vterm damage
//=============================================================================

class Terminal : public Widget {
public:
    using Ptr = std::shared_ptr<Terminal>;

    // Factory - generic signature for creation through WidgetFactory
    static Result<WidgetPtr> create(WidgetFactory* factory,
                                    const WidgetParams& params,
                                    const std::string& payload) noexcept;

    // Legacy create (for direct creation during transition)
    static Result<Ptr> create(uint32_t id, uint32_t cols, uint32_t rows,
                              Font* font, uv_loop_t* loop) noexcept;

    ~Terminal() override;

    // Non-copyable
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    //=========================================================================
    // Widget interface overrides
    //=========================================================================
    void start() override;
    void stop() override;

    // Prepare frame - propagates RenderContext and prepareFrame to children
    void prepareFrame(WebGPUContext& ctx) override;

    // Batched render - renders grid and child widgets to provided pass
    bool render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

    // Standalone render (deprecated - use batched render)
    Result<void> render(WebGPUContext& ctx) override;

    //=========================================================================
    // Terminal-specific interface (single-threaded, call from main thread)
    //=========================================================================

    // Start with specific shell (called internally by start(), can override)
    Result<void> startShell(const std::string& shell = "");

    // Send keyboard input (direct write to PTY)
    void sendKey(uint32_t codepoint, VTermModifier mod = VTERM_MOD_NONE);
    void sendSpecialKey(VTermKey key, VTermModifier mod = VTERM_MOD_NONE);
    void sendRaw(const char* data, size_t len);

    // Resize terminal
    void resize(uint32_t cols, uint32_t rows);

    // Grid access
    const Grid& getGrid() const { return _grid; }
    Grid& getGridMutable() { return _grid; }

    // Cursor state
    int getCursorRow() const { return _cursorRow; }
    int getCursorCol() const { return _cursorCol; }
    bool isCursorVisible() const { return _cursorVisible && _cursorBlink; }

    // Damage tracking
    const std::vector<DamageRect>& getDamageRects() const { return _damageRects; }
    void clearDamageRects() { _damageRects.clear(); }
    bool hasDamage() const { return !_damageRects.empty() || _fullDamage; }
    bool hasFullDamage() const { return _fullDamage; }
    void clearFullDamage() { _fullDamage = false; }

    // Scrollback navigation
    void scrollUp(int lines = 1);
    void scrollDown(int lines = 1);
    void scrollToTop();
    void scrollToBottom();
    int getScrollOffset() const { return _scrollOffset; }
    bool isScrolledBack() const { return _scrollOffset > 0; }
    size_t getScrollbackSize() const { return _scrollback.size(); }

    // Selection
    void startSelection(int row, int col, SelectionMode mode = SelectionMode::Character);
    void extendSelection(int row, int col);
    void clearSelection();
    bool hasSelection() const { return _selectionMode != SelectionMode::None; }
    bool isInSelection(int row, int col) const;
    std::string getSelectedText();

    // Configuration
    void setConfig(const Config* config) { _config = config; }
    void setShell(const std::string& shell) { _shell = shell; }

    // Rendering support
    void setEmojiAtlas(EmojiAtlas* atlas) { _emojiAtlas = atlas; }
    void setRenderer(GridRenderer* renderer) { _renderer = renderer; }

    // Child widget management (Terminal owns its child widgets)
    void addChildWidget(WidgetPtr widget);
    Result<void> removeChildWidget(uint32_t id);
    Result<void> removeChildWidgetByHashId(const std::string& hashId);
    WidgetPtr getChildWidget(uint32_t id) const;
    WidgetPtr getChildWidgetByHashId(const std::string& hashId) const;
    const std::vector<WidgetPtr>& getChildWidgets() const { return _childWidgets; }

    // Grid cell marking for widget mouse hit testing
    void markWidgetGridCells(Widget* widget);
    void clearWidgetGridCells(Widget* widget);

    // OSC sequence handling (widget creation/management)
    bool handleOSCSequence(const std::string& sequence,
                           std::string* response = nullptr,
                           uint32_t* linesToAdvance = nullptr);

    // Cell size
    void setCellSize(uint32_t width, uint32_t height);
    uint32_t getCellWidth() const { return _cellWidth; }
    uint32_t getCellHeight() const { return _cellHeight; }

    void setBaseCellSize(float width, float height);
    float getBaseCellWidth() const { return _baseCellWidth; }
    float getBaseCellHeight() const { return _baseCellHeight; }

    void setZoomLevel(float zoom);
    float getZoomLevel() const { return _zoomLevel; }
    float getCellWidthF() const { return _baseCellWidth * _zoomLevel; }
    float getCellHeightF() const { return _baseCellHeight * _zoomLevel; }

    // Mouse mode
    int getMouseMode() const { return _mouseMode; }
    bool wantsMouseEvents() const { return _mouseMode != VTERM_PROP_MOUSE_NONE; }

    // VTerm access
    VTermScreen* getVTermScreen() const { return _vtermScreen; }
    bool isAltScreen() const { return _isAltScreen; }

    // libvterm callbacks (public for C callback access)
    static int onDamage(VTermRect rect, void* user);
    static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user);
    static int onSetTermProp(VTermProp prop, VTermValue* val, void* user);
    static int onResize(int rows, int cols, void* user);
    static int onBell(void* user);
    static int onOSC(int command, VTermStringFragment frag, void* user);
    static int onSbPushline(int cols, const VTermScreenCell* cells, void* user);
    static int onSbPopline(int cols, VTermScreenCell* cells, void* user);
    static int onMoverect(VTermRect dest, VTermRect src, void* user);

private:
    Terminal(uint32_t id, uint32_t cols, uint32_t rows, Font* font, uv_loop_t* loop) noexcept;
    Result<void> init() noexcept;

    // libuv callbacks
    static void onTimer(uv_timer_t* handle);
    static void onPtyPoll(uv_poll_t* handle, int status, int events);

    // PTY operations
    Result<void> readPty();
    Result<void> writeToPty(const char* data, size_t len);
    Result<void> flushVtermOutput();

    // Grid sync
    void syncToGrid();
    void syncDamageToGrid();
    void colorToRGB(const VTermColor& color, uint8_t& r, uint8_t& g, uint8_t& b);

    // Widget position update on scroll (called when lines are pushed/popped from scrollback)
    void updateWidgetPositionsOnScroll(int lines);

    // Update cursor blink
    void updateCursorBlink(double currentTime);

    //=========================================================================
    // libuv (external loop, not owned)
    //=========================================================================
    uv_loop_t* _loop = nullptr;
    uv_timer_t* _cursorTimer = nullptr;
    uv_poll_t* _ptyPoll = nullptr;


    //=========================================================================
    // Terminal state
    //=========================================================================
    VTerm* _vterm = nullptr;
    VTermScreen* _vtermScreen = nullptr;

    Grid _grid;
    Font* _font;
    std::string _shell;

#ifdef _WIN32
    HPCON _hPC = INVALID_HANDLE_VALUE;
    HANDLE _hPipeIn = INVALID_HANDLE_VALUE;
    HANDLE _hPipeOut = INVALID_HANDLE_VALUE;
    HANDLE _hProcess = INVALID_HANDLE_VALUE;
    HANDLE _hThread = INVALID_HANDLE_VALUE;
#else
    int _ptyMaster = -1;
    pid_t _childPid = -1;
#endif

    int _cursorRow = 0;
    int _cursorCol = 0;
    bool _cursorVisible = true;
    bool _cursorBlink = true;
    bool _isAltScreen = false;
    double _lastBlinkTime = 0.0;
    double _blinkInterval = 0.5;

    uint32_t _cols;
    uint32_t _rows;

    std::vector<DamageRect> _damageRects;
    bool _fullDamage = true;

    const Config* _config = nullptr;
    WidgetFactory* _widgetFactory = nullptr;  // For creating child widgets
    EmojiAtlas* _emojiAtlas = nullptr;
    GridRenderer* _renderer = nullptr;

    // Child widgets (Terminal owns its child widgets)
    std::vector<WidgetPtr> _childWidgets;
    std::unordered_map<std::string, WidgetPtr> _childWidgetsByHashId;
    OscCommandParser _oscParser;
    uint32_t _nextChildWidgetId = 1;

    uint32_t _cellWidth = 10;
    uint32_t _cellHeight = 20;
    float _baseCellWidth = 10.0f;
    float _baseCellHeight = 20.0f;
    float _zoomLevel = 1.0f;

    std::string _oscBuffer;
    int _oscCommand = -1;

    std::deque<ScrollbackLine> _scrollback;
    int _scrollOffset = 0;

    uint32_t _pendingNewlines = 0;

    VTermPos _selectionStart = {0, 0};
    VTermPos _selectionEnd = {0, 0};
    SelectionMode _selectionMode = SelectionMode::None;

    int _mouseMode = VTERM_PROP_MOUSE_NONE;

    static constexpr size_t PTY_READ_BUFFER_SIZE = 40960;  // 40KB (10x4KB)
    std::unique_ptr<char[]> _ptyReadBuffer;
};

} // namespace yetty
