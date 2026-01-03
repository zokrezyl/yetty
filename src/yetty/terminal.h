#pragma once

#include <yetty/renderable.h>
#include <yetty/font.h>
#include <yetty/config.h>
#include <yetty/result.hpp>
#include "grid.h"

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

namespace yetty {

// Forward declarations
class PluginManager;
class EmojiAtlas;
class GridRenderer;

// A single line in the scrollback buffer
struct ScrollbackLine {
    std::vector<uint32_t> _chars;
    std::vector<uint8_t> _fgColors;  // RGB per cell (3 bytes each)
    std::vector<uint8_t> _bgColors;
};

// Selection mode for mouse selection
enum class SelectionMode {
    None,
    Character,
    Word,
    Line
};

// Rectangle representing damaged (changed) cells
struct DamageRect {
    uint32_t _startCol, _startRow;
    uint32_t _endCol, _endRow;  // exclusive
};

//=============================================================================
// Terminal - Renderable terminal emulator with libuv-based async PTY I/O
//
// Threading model:
//   - Worker thread runs libuv event loop, reads from PTY, updates state
//   - Main thread calls get_command_queue() to get render commands
//   - Mutex protects shared state between worker and main threads
//
// Implements Renderable interface for the new command-based architecture.
//=============================================================================

class Terminal : public Renderable {
public:
    using Ptr = std::shared_ptr<Terminal>;

    // Factory - creates terminal with given grid size
    static Result<Ptr> create(uint32_t id, uint32_t cols, uint32_t rows, Font* font) noexcept;

    ~Terminal() override;

    // Non-copyable
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    //=========================================================================
    // Renderable interface
    //=========================================================================
    uint32_t id() const override { return id_; }
    uint32_t zOrder() const override { return zOrder_; }
    const std::string& name() const override { return name_; }

    void start() override;
    void stop() override;
    bool isRunning() const override { return running_.load(); }

    CommandQueue* get_command_queue(CommandQueue* old_queue) override;

    //=========================================================================
    // Terminal-specific interface (all thread-safe)
    //=========================================================================

    // Start with specific shell (called internally by start(), can override)
    Result<void> startShell(const std::string& shell = "");

    // Send keyboard input (queued for worker thread)
    void sendKey(uint32_t codepoint, VTermModifier mod = VTERM_MOD_NONE);
    void sendSpecialKey(VTermKey key, VTermModifier mod = VTERM_MOD_NONE);
    void sendRaw(const char* data, size_t len);

    // Resize terminal
    void resize(uint32_t cols, uint32_t rows);

    // Grid access (for rendering - use under lock or via command queue)
    const Grid& getGrid() const { return grid_; }
    Grid& getGridMutable() { return grid_; }

    // Cursor state
    int getCursorRow() const { return cursorRow_; }
    int getCursorCol() const { return cursorCol_; }
    bool isCursorVisible() const { return cursorVisible_ && cursorBlink_; }

    // Damage tracking
    const std::vector<DamageRect>& getDamageRects() const { return damageRects_; }
    void clearDamageRects() { damageRects_.clear(); }
    bool hasDamage() const { return !damageRects_.empty() || fullDamage_; }
    bool hasFullDamage() const { return fullDamage_; }
    void clearFullDamage() { fullDamage_ = false; }

    // Scrollback navigation
    void scrollUp(int lines = 1);
    void scrollDown(int lines = 1);
    void scrollToTop();
    void scrollToBottom();
    int getScrollOffset() const { return scrollOffset_; }
    bool isScrolledBack() const { return scrollOffset_ > 0; }
    size_t getScrollbackSize() const { return scrollback_.size(); }

    // Selection
    void startSelection(int row, int col, SelectionMode mode = SelectionMode::Character);
    void extendSelection(int row, int col);
    void clearSelection();
    bool hasSelection() const { return selectionMode_ != SelectionMode::None; }
    bool isInSelection(int row, int col) const;
    std::string getSelectedText();

    // Configuration
    void setConfig(const Config* config) { config_ = config; }
    void setShell(const std::string& shell) { shell_ = shell; }

    // Plugin support
    void setPluginManager(PluginManager* mgr) { pluginManager_ = mgr; }
    PluginManager* getPluginManager() const { return pluginManager_; }
    void setEmojiAtlas(EmojiAtlas* atlas) { emojiAtlas_ = atlas; }
    void setRenderer(GridRenderer* renderer) { renderer_ = renderer; }

    // Cell size
    void setCellSize(uint32_t width, uint32_t height);
    uint32_t getCellWidth() const { return cellWidth_; }
    uint32_t getCellHeight() const { return cellHeight_; }

    void setBaseCellSize(float width, float height);
    float getBaseCellWidth() const { return baseCellWidth_; }
    float getBaseCellHeight() const { return baseCellHeight_; }

    void setZoomLevel(float zoom);
    float getZoomLevel() const { return zoomLevel_; }
    float getCellWidthF() const { return baseCellWidth_ * zoomLevel_; }
    float getCellHeightF() const { return baseCellHeight_ * zoomLevel_; }

    // Mouse mode
    int getMouseMode() const { return mouseMode_; }
    bool wantsMouseEvents() const { return mouseMode_ != VTERM_PROP_MOUSE_NONE; }

    // VTerm access
    VTermScreen* getVTermScreen() const { return vtermScreen_; }
    bool isAltScreen() const { return isAltScreen_; }

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
    Terminal(uint32_t id, uint32_t cols, uint32_t rows, Font* font) noexcept;
    Result<void> init() noexcept;

    // Worker thread
    void workerLoop();
    void processPendingInput();
    static void onAsync(uv_async_t* handle);
    static void onTimer(uv_timer_t* handle);
    static void onPtyPoll(uv_poll_t* handle, int status, int events);

    // PTY operations (called on worker thread)
    Result<void> readPty();
    Result<void> writeToPty(const char* data, size_t len);
    Result<void> flushVtermOutput();

    // Grid sync
    void syncToGrid();
    void syncDamageToGrid();
    void colorToRGB(const VTermColor& color, uint8_t& r, uint8_t& g, uint8_t& b);

    // Update cursor blink
    void updateCursorBlink(double currentTime);

    //=========================================================================
    // Identity
    //=========================================================================
    uint32_t id_;
    uint32_t zOrder_ = 0;
    std::string name_;

    //=========================================================================
    // Thread synchronization
    //=========================================================================
    mutable std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::thread workerThread_;

    // libuv handles
    uv_loop_t* loop_ = nullptr;
    uv_async_t* asyncHandle_ = nullptr;
    uv_timer_t* cursorTimer_ = nullptr;
    uv_poll_t* ptyPoll_ = nullptr;

    // Pending input from main thread
    std::mutex inputMutex_;
    struct PendingKey {
        uint32_t codepoint;
        VTermModifier mod;
        bool isSpecial;
        VTermKey specialKey;
    };
    std::vector<PendingKey> pendingKeys_;
    std::vector<char> pendingRaw_;
    bool pendingResize_ = false;
    uint32_t pendingCols_ = 0;
    uint32_t pendingRows_ = 0;

    // Command queue recycling
    std::unique_ptr<CommandQueue> recycledQueue_;
    bool shaderRegistered_ = false;  // Track if shader was registered with Yetty

    //=========================================================================
    // Terminal state (protected by mutex_)
    //=========================================================================
    VTerm* vterm_ = nullptr;
    VTermScreen* vtermScreen_ = nullptr;

    Grid grid_;
    Font* font_;
    std::string shell_;

#ifdef _WIN32
    HPCON hPC_ = INVALID_HANDLE_VALUE;
    HANDLE hPipeIn_ = INVALID_HANDLE_VALUE;
    HANDLE hPipeOut_ = INVALID_HANDLE_VALUE;
    HANDLE hProcess_ = INVALID_HANDLE_VALUE;
    HANDLE hThread_ = INVALID_HANDLE_VALUE;
#else
    int ptyMaster_ = -1;
    pid_t childPid_ = -1;
#endif

    int cursorRow_ = 0;
    int cursorCol_ = 0;
    bool cursorVisible_ = true;
    bool cursorBlink_ = true;
    bool isAltScreen_ = false;
    double lastBlinkTime_ = 0.0;
    double blinkInterval_ = 0.5;

    uint32_t cols_;
    uint32_t rows_;

    std::vector<DamageRect> damageRects_;
    bool fullDamage_ = true;

    const Config* config_ = nullptr;
    PluginManager* pluginManager_ = nullptr;
    EmojiAtlas* emojiAtlas_ = nullptr;
    GridRenderer* renderer_ = nullptr;

    uint32_t cellWidth_ = 10;
    uint32_t cellHeight_ = 20;
    float baseCellWidth_ = 10.0f;
    float baseCellHeight_ = 20.0f;
    float zoomLevel_ = 1.0f;

    std::string oscBuffer_;
    int oscCommand_ = -1;

    std::deque<ScrollbackLine> scrollback_;
    int scrollOffset_ = 0;

    uint32_t pendingNewlines_ = 0;

    VTermPos selectionStart_ = {0, 0};
    VTermPos selectionEnd_ = {0, 0};
    SelectionMode selectionMode_ = SelectionMode::None;

    int mouseMode_ = VTERM_PROP_MOUSE_NONE;

    static constexpr size_t PTY_READ_BUFFER_SIZE = 256 * 1024;
    std::unique_ptr<char[]> ptyReadBuffer_;
};

} // namespace yetty
