#pragma once

//=============================================================================
// Terminal Test Harness
//
// Lightweight wrapper around libvterm for testing terminal behavior
// without PTY, rendering, or other heavyweight dependencies.
//=============================================================================

extern "C" {
#include <vterm.h>
}

#include <string>
#include <vector>
#include <functional>

namespace yetty::test {

// Captured scroll event
struct ScrollEvent {
    int start_row;
    int end_row;
    int start_col;
    int end_col;
    int delta_row;
    int delta_col;
};

// Captured moverect event
struct MoverectEvent {
    VTermRect dest;
    VTermRect src;

    // Convenience: calculated scroll delta (src.start_row - dest.start_row)
    int scrollDelta() const { return src.start_row - dest.start_row; }
};

// Captured damage event
struct DamageEvent {
    VTermRect rect;
};

//-----------------------------------------------------------------------------
// TerminalHarness - Test wrapper for libvterm
//-----------------------------------------------------------------------------
class TerminalHarness {
public:
    TerminalHarness(int cols = 80, int rows = 25);
    ~TerminalHarness();

    // Feed escape sequence or text to the terminal
    void feed(const std::string& data);
    void feed(const char* data, size_t len);

    // Reset terminal state
    void reset();

    // Get current cursor position
    int cursorRow() const { return _cursor_row; }
    int cursorCol() const { return _cursor_col; }

    // Get terminal dimensions
    int cols() const { return _cols; }
    int rows() const { return _rows; }

    // Access captured events
    const std::vector<ScrollEvent>& scrollEvents() const { return _scroll_events; }
    const std::vector<MoverectEvent>& moverectEvents() const { return _moverect_events; }
    const std::vector<DamageEvent>& damageEvents() const { return _damage_events; }

    // Clear captured events
    void clearEvents();

    // Get cell content at position
    uint32_t getChar(int row, int col) const;

    // Get raw vterm for advanced testing
    VTerm* vterm() { return _vterm; }
    VTermScreen* screen() { return _screen; }

    // Selection support (for testing Terminal selection logic)
    enum class SelectionMode { None, Character, Word, Line };

    void startSelection(int row, int col, SelectionMode mode = SelectionMode::Character);
    void extendSelection(int row, int col);
    void clearSelection();
    bool hasSelection() const { return _selection_mode != SelectionMode::None; }
    bool isInSelection(int row, int col) const;
    std::string getSelectedText() const;

    // libvterm callbacks (public for C callback access)
    static int onDamage(VTermRect rect, void* user);
    static int onMoverect(VTermRect dest, VTermRect src, void* user);
    static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user);
    static int onSbPushline(int cols, const VTermScreenCell* cells, void* user);
    static int onSbPopline(int cols, VTermScreenCell* cells, void* user);

private:
    VTerm* _vterm = nullptr;
    VTermScreen* _screen = nullptr;

    int _cols;
    int _rows;
    int _cursor_row = 0;
    int _cursor_col = 0;

    // Captured events
    std::vector<ScrollEvent> _scroll_events;
    std::vector<MoverectEvent> _moverect_events;
    std::vector<DamageEvent> _damage_events;

    // Scrollback tracking
    int _sb_pushline_count = 0;
    int _sb_popline_count = 0;

    // Selection state
    VTermPos _selection_start = {0, 0};
    VTermPos _selection_end = {0, 0};
    SelectionMode _selection_mode = SelectionMode::None;
};

} // namespace yetty::test
