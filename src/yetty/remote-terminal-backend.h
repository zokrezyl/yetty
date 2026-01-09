#pragma once

#include "terminal-backend.h"
#include "shared-grid.h"
#include <yetty/result.hpp>

#include <uv.h>
#include <memory>
#include <string>

namespace yetty {

//=============================================================================
// RemoteTerminalBackend - connects to yetty-server via shared memory
//
// This backend connects to a yetty-server process that manages the PTY and
// vterm state. The Grid is stored in shared memory (shm_open/mmap) so the
// client can read it directly without copying.
//
// Communication:
//   - Unix socket for commands (input, resize) and notifications (damage)
//   - Shared memory for Grid data (zero-copy rendering)
//
// Architecture:
//   Client (yetty --mux)           Server (yetty-server)
//   ├─ RemoteTerminalBackend       ├─ VTerm + PTY
//   ├─ Grid (mmap'd shared mem) ◄──┼─ Grid (shared mem owner)
//   ├─ GridRenderer                ├─ Damage tracking
//   └─ Input → Unix socket ────────┴─ Input handling
//
//=============================================================================

class RemoteTerminalBackend : public ITerminalBackend {
public:
    using Ptr = std::shared_ptr<RemoteTerminalBackend>;

    // Factory - connects to existing server or spawns one
    static Result<Ptr> create(uint32_t cols, uint32_t rows, uv_loop_t* loop,
                              const std::string& serverSocketPath = "") noexcept;

    ~RemoteTerminalBackend() override;

    // Non-copyable
    RemoteTerminalBackend(const RemoteTerminalBackend&) = delete;
    RemoteTerminalBackend& operator=(const RemoteTerminalBackend&) = delete;

    //=========================================================================
    // ITerminalBackend interface
    //=========================================================================
    
    Result<void> start(const std::string& shell = "") override;
    void stop() override;
    bool isRunning() const override { return running_; }

    void sendKey(uint32_t codepoint, VTermModifier mod = VTERM_MOD_NONE) override;
    void sendSpecialKey(VTermKey key, VTermModifier mod = VTERM_MOD_NONE) override;
    void sendRaw(const char* data, size_t len) override;

    void resize(uint32_t cols, uint32_t rows) override;

    const Grid& getGrid() const override { return *grid_; }
    Grid& getGridMutable() override { return *grid_; }
    
    uint32_t getCols() const override { return cols_; }
    uint32_t getRows() const override { return rows_; }

    int getCursorRow() const override { return cursorRow_; }
    int getCursorCol() const override { return cursorCol_; }
    bool isCursorVisible() const override { return cursorVisible_; }

    const std::vector<DamageRect>& getDamageRects() const override { return damageRects_; }
    void clearDamageRects() override { damageRects_.clear(); }
    bool hasDamage() const override { return !damageRects_.empty() || fullDamage_; }
    bool hasFullDamage() const override { return fullDamage_; }
    void clearFullDamage() override { fullDamage_ = false; }

    void scrollUp(int lines = 1) override;
    void scrollDown(int lines = 1) override;
    void scrollToTop() override;
    void scrollToBottom() override;
    int getScrollOffset() const override { return scrollOffset_; }
    bool isScrolledBack() const override { return scrollOffset_ > 0; }
    size_t getScrollbackSize() const override { return scrollbackSize_; }

    void startSelection(int row, int col, SelectionMode mode) override;
    void extendSelection(int row, int col) override;
    void clearSelection() override;
    bool hasSelection() const override { return selectionMode_ != SelectionMode::None; }
    bool isInSelection(int row, int col) const override;
    std::string getSelectedText() override;

    int getMouseMode() const override { return mouseMode_; }
    bool wantsMouseEvents() const override { return mouseMode_ != 0; }
    bool isAltScreen() const override { return isAltScreen_; }

    void syncToGrid() override;

    //=========================================================================
    // Remote-specific interface
    //=========================================================================
    
    void setCallbacks(const TerminalBackendCallbacks& callbacks) { callbacks_ = callbacks; }
    
    // Set Font for codepoint-to-glyph conversion
    void setFont(class Font* font);
    
    // Server socket path
    std::string getSocketPath() const { return socketPath_; }

private:
    RemoteTerminalBackend(uint32_t cols, uint32_t rows, uv_loop_t* loop) noexcept;
    Result<void> init(const std::string& serverSocketPath) noexcept;
    
    // Connect to server
    Result<void> connectToServer() noexcept;
    
    // Map shared memory Grid
    Result<void> mapSharedGrid() noexcept;
    
    // IPC message handling
    void sendCommand(const std::string& cmd);
    void onServerMessage(const char* data, size_t len);
    
    // Server spawning
    Result<void> spawnServer(const std::string& shell) noexcept;
    
    // libuv callbacks
    static void onConnect(uv_connect_t* req, int status);
    static void onRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void allocBuffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);

    //=========================================================================
    // State
    //=========================================================================
    
    uv_loop_t* loop_ = nullptr;
    uv_pipe_t* socket_ = nullptr;
    bool running_ = false;
    bool connected_ = false;
    pid_t serverPid_ = -1;  // PID of spawned server (if we spawned it)

    std::string socketPath_;
    std::string shmName_;  // Shared memory name for Grid
    
    // Grid - SharedGridView wraps SharedGrid for zero-copy access
    std::unique_ptr<Grid> localGrid_;           // Fallback before connection
    std::unique_ptr<SharedGrid, void(*)(SharedGrid*)> sharedGrid_{nullptr, [](SharedGrid* p) { delete p; }};
    std::unique_ptr<SharedGridView> sharedGridView_;  // View into shared memory
    Grid* grid_ = nullptr;                      // Points to localGrid_ or sharedGridView_

    uint32_t cols_;
    uint32_t rows_;

    int cursorRow_ = 0;
    int cursorCol_ = 0;
    bool cursorVisible_ = true;
    bool isAltScreen_ = false;

    std::vector<DamageRect> damageRects_;
    bool fullDamage_ = true;

    int scrollOffset_ = 0;
    size_t scrollbackSize_ = 0;

    VTermPos selectionStart_ = {0, 0};
    VTermPos selectionEnd_ = {0, 0};
    SelectionMode selectionMode_ = SelectionMode::None;

    int mouseMode_ = 0;

    TerminalBackendCallbacks callbacks_;
    
    // Font for codepoint-to-glyph conversion (not owned)
    Font* font_ = nullptr;
    
    // Read buffer for IPC
    static constexpr size_t READ_BUFFER_SIZE = 65536;
    std::unique_ptr<char[]> readBuffer_;
};

} // namespace yetty
