#pragma once

#include <yetty/result.hpp>
#include "grid.h"
#include "damage-rect.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <vterm.h>
}

namespace yetty {

// Forward declarations
class Font;

// Selection mode for mouse selection
enum class SelectionMode {
    None,
    Character,
    Word,
    Line
};

// Scrollback cell style (colors + attrs packed together for RLE)
struct ScrollbackStyle {
    uint8_t fgR, fgG, fgB;
    uint8_t bgR, bgG, bgB;
    uint16_t attrs;  // packed VTermScreenCellAttrs
    
    bool operator==(const ScrollbackStyle& o) const {
        return fgR == o.fgR && fgG == o.fgG && fgB == o.fgB &&
               bgR == o.bgR && bgG == o.bgG && bgB == o.bgB &&
               attrs == o.attrs;
    }
};

// RLE run for style (colors + attrs)
struct StyleRun {
    ScrollbackStyle style;
    uint16_t count;
};

// A single line in the scrollback buffer
struct ScrollbackLine {
    std::vector<uint32_t> chars;        // one per cell
    std::vector<StyleRun> styleRuns;    // RLE-compressed styles
};

//=============================================================================
// ITerminalBackend - interface for terminal state management
//
// This abstracts away whether the terminal runs in-process (LocalTerminalBackend)
// or in a separate server process (RemoteTerminalBackend with shared memory).
//
// The backend owns:
//   - VTerm state (or proxy to remote vterm)
//   - Scrollback buffer
//   - Grid data (either local or shared memory)
//   - PTY connection (local) or IPC to server (remote)
//
// The frontend (TerminalView) handles:
//   - Rendering via GridRenderer
//   - Input handling
//   - Cursor blinking (visual only)
//=============================================================================

class ITerminalBackend {
public:
    using Ptr = std::shared_ptr<ITerminalBackend>;

    virtual ~ITerminalBackend() = default;

    //=========================================================================
    // Lifecycle
    //=========================================================================
    
    // Start the backend (spawn shell for local, connect for remote)
    virtual Result<void> start(const std::string& shell = "") = 0;
    
    // Stop the backend
    virtual void stop() = 0;
    
    // Check if running
    virtual bool isRunning() const = 0;

    //=========================================================================
    // Input - send to PTY/server
    //=========================================================================
    
    virtual void sendKey(uint32_t codepoint, VTermModifier mod = VTERM_MOD_NONE) = 0;
    virtual void sendSpecialKey(VTermKey key, VTermModifier mod = VTERM_MOD_NONE) = 0;
    virtual void sendRaw(const char* data, size_t len) = 0;

    //=========================================================================
    // Terminal control
    //=========================================================================
    
    virtual void resize(uint32_t cols, uint32_t rows) = 0;
    
    //=========================================================================
    // Grid access - the Grid may be local or shared memory
    //=========================================================================
    
    virtual const Grid& getGrid() const = 0;
    virtual Grid& getGridMutable() = 0;
    
    virtual uint32_t getCols() const = 0;
    virtual uint32_t getRows() const = 0;

    //=========================================================================
    // Cursor state
    //=========================================================================
    
    virtual int getCursorRow() const = 0;
    virtual int getCursorCol() const = 0;
    virtual bool isCursorVisible() const = 0;

    //=========================================================================
    // Damage tracking
    //=========================================================================
    
    virtual const std::vector<DamageRect>& getDamageRects() const = 0;
    virtual void clearDamageRects() = 0;
    virtual bool hasDamage() const = 0;
    virtual bool hasFullDamage() const = 0;
    virtual void clearFullDamage() = 0;

    //=========================================================================
    // Scrollback navigation
    //=========================================================================
    
    virtual void scrollUp(int lines = 1) = 0;
    virtual void scrollDown(int lines = 1) = 0;
    virtual void scrollToTop() = 0;
    virtual void scrollToBottom() = 0;
    virtual int getScrollOffset() const = 0;
    virtual bool isScrolledBack() const = 0;
    virtual size_t getScrollbackSize() const = 0;

    //=========================================================================
    // Selection
    //=========================================================================
    
    virtual void startSelection(int row, int col, SelectionMode mode) = 0;
    virtual void extendSelection(int row, int col) = 0;
    virtual void clearSelection() = 0;
    virtual bool hasSelection() const = 0;
    virtual bool isInSelection(int row, int col) const = 0;
    virtual std::string getSelectedText() = 0;

    //=========================================================================
    // Terminal properties
    //=========================================================================
    
    virtual int getMouseMode() const = 0;
    virtual bool wantsMouseEvents() const = 0;
    virtual bool isAltScreen() const = 0;

    //=========================================================================
    // Sync grid from vterm (called before render)
    //=========================================================================
    
    virtual void syncToGrid() = 0;
};

//=============================================================================
// TerminalBackendCallbacks - callbacks from backend to frontend
//=============================================================================

struct TerminalBackendCallbacks {
    // Called when terminal properties change (title, icon, etc.)
    std::function<void(const std::string& title)> onTitleChange;
    
    // Called when bell is triggered
    std::function<void()> onBell;
    
    // Called when alt screen mode changes
    std::function<void(bool isAltScreen)> onAltScreenChange;
    
    // Called when OSC command is received
    std::function<void(int command, const std::string& data)> onOSC;
};

} // namespace yetty
