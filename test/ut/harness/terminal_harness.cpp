//=============================================================================
// Terminal Test Harness - Implementation
//=============================================================================

#include "terminal_harness.h"
#include <cstring>
#include <vector>
#include <algorithm>

namespace yetty::test {

// Screen callbacks structure
static VTermScreenCallbacks screen_callbacks = {
    .damage = TerminalHarness::onDamage,
    .moverect = TerminalHarness::onMoverect,
    .movecursor = TerminalHarness::onMoveCursor,
    .settermprop = nullptr,
    .bell = nullptr,
    .resize = nullptr,
    .sb_pushline = TerminalHarness::onSbPushline,
    .sb_popline = TerminalHarness::onSbPopline,
#if VTERM_VERSION_MAJOR > 0 || VTERM_VERSION_MINOR >= 3
    .sb_clear = nullptr,
#endif
};

TerminalHarness::TerminalHarness(int cols, int rows)
    : _cols(cols), _rows(rows)
{
    _vterm = vterm_new(rows, cols);
    vterm_set_utf8(_vterm, 1);

    _screen = vterm_obtain_screen(_vterm);
    vterm_screen_set_callbacks(_screen, &screen_callbacks, this);
    vterm_screen_reset(_screen, 1);
}

TerminalHarness::~TerminalHarness() {
    if (_vterm) {
        vterm_free(_vterm);
    }
}

void TerminalHarness::feed(const std::string& data) {
    feed(data.data(), data.size());
}

void TerminalHarness::feed(const char* data, size_t len) {
    vterm_input_write(_vterm, data, len);
}

void TerminalHarness::reset() {
    vterm_screen_reset(_screen, 1);
    clearEvents();
    _cursor_row = 0;
    _cursor_col = 0;
    _sb_pushline_count = 0;
    _sb_popline_count = 0;
}

void TerminalHarness::clearEvents() {
    _scroll_events.clear();
    _moverect_events.clear();
    _damage_events.clear();
}

uint32_t TerminalHarness::getChar(int row, int col) const {
    VTermPos pos = { row, col };
    VTermScreenCell cell;
    vterm_screen_get_cell(_screen, pos, &cell);
    return cell.chars[0];
}

//-----------------------------------------------------------------------------
// libvterm callbacks
//-----------------------------------------------------------------------------

int TerminalHarness::onDamage(VTermRect rect, void* user) {
    auto* self = static_cast<TerminalHarness*>(user);
    self->_damage_events.push_back({ rect });
    return 0;
}

int TerminalHarness::onMoverect(VTermRect dest, VTermRect src, void* user) {
    auto* self = static_cast<TerminalHarness*>(user);
    self->_moverect_events.push_back({ dest, src });
    // Note: We don't add to _scroll_events here - that's only for sb_pushline/popline
    return 0;
}

int TerminalHarness::onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user) {
    auto* self = static_cast<TerminalHarness*>(user);
    self->_cursor_row = pos.row;
    self->_cursor_col = pos.col;
    return 0;
}

int TerminalHarness::onSbPushline(int cols, const VTermScreenCell* cells, void* user) {
    auto* self = static_cast<TerminalHarness*>(user);
    self->_sb_pushline_count++;

    // Record as scroll event (content moved UP)
    ScrollEvent scroll;
    scroll.start_row = 0;
    scroll.end_row = self->_rows;
    scroll.start_col = 0;
    scroll.end_col = self->_cols;
    scroll.delta_row = 1;  // Content moved up by 1
    scroll.delta_col = 0;
    self->_scroll_events.push_back(scroll);

    return 0;
}

int TerminalHarness::onSbPopline(int cols, VTermScreenCell* cells, void* user) {
    auto* self = static_cast<TerminalHarness*>(user);
    self->_sb_popline_count++;

    // Record as scroll event (content moved DOWN)
    ScrollEvent scroll;
    scroll.start_row = 0;
    scroll.end_row = self->_rows;
    scroll.start_col = 0;
    scroll.end_col = self->_cols;
    scroll.delta_row = -1;  // Content moved down by 1
    scroll.delta_col = 0;
    self->_scroll_events.push_back(scroll);

    return 0;
}

//-----------------------------------------------------------------------------
// Selection support
//-----------------------------------------------------------------------------

void TerminalHarness::startSelection(int row, int col, SelectionMode mode) {
    _selection_start.row = row;
    _selection_start.col = col;
    _selection_end.row = row;
    _selection_end.col = col;
    _selection_mode = mode;

    // For word mode, expand to word boundaries
    if (mode == SelectionMode::Word) {
        VTermPos pos = {row, col};
        VTermScreenCell cell;

        // Find word start (go left while not space)
        while (pos.col > 0) {
            VTermPos testPos = {pos.row, pos.col - 1};
            vterm_screen_get_cell(_screen, testPos, &cell);
            uint32_t ch = cell.chars[0];
            if (ch == 0 || ch == ' ' || ch == '\t') break;
            pos.col--;
        }
        _selection_start = pos;

        // Find word end (go right while not space)
        pos = {row, col};
        while (pos.col < _cols - 1) {
            vterm_screen_get_cell(_screen, pos, &cell);
            uint32_t ch = cell.chars[0];
            if (ch == 0 || ch == ' ' || ch == '\t') break;
            pos.col++;
        }
        _selection_end = pos;
    } else if (mode == SelectionMode::Line) {
        _selection_start.col = 0;
        _selection_end.col = _cols;
    }
}

void TerminalHarness::extendSelection(int row, int col) {
    if (_selection_mode == SelectionMode::None) return;

    _selection_end.row = row;
    _selection_end.col = col;

    // For line mode, always extend to full lines
    if (_selection_mode == SelectionMode::Line) {
        if (row >= _selection_start.row) {
            _selection_end.col = _cols;
        } else {
            _selection_end.col = 0;
        }
    }
}

void TerminalHarness::clearSelection() {
    _selection_mode = SelectionMode::None;
    _selection_start = {0, 0};
    _selection_end = {0, 0};
}

bool TerminalHarness::isInSelection(int row, int col) const {
    if (_selection_mode == SelectionMode::None) return false;

    // Normalize start/end so start <= end in document order
    VTermPos start = _selection_start;
    VTermPos end = _selection_end;

    if (vterm_pos_cmp(start, end) > 0) {
        std::swap(start, end);
    }

    VTermPos pos = {row, col};

    // Check if pos is between start and end
    if (vterm_pos_cmp(pos, start) < 0) return false;
    if (vterm_pos_cmp(pos, end) > 0) return false;

    return true;
}

std::string TerminalHarness::getSelectedText() const {
    if (_selection_mode == SelectionMode::None) return "";

    // Normalize start/end
    VTermPos start = _selection_start;
    VTermPos end = _selection_end;

    if (vterm_pos_cmp(start, end) > 0) {
        std::swap(start, end);
    }

    std::string result;

    // Build text line by line
    for (int row = start.row; row <= end.row; row++) {
        int startCol = (row == start.row) ? start.col : 0;
        // end_col is exclusive in VTermRect, so add 1 to include the last character
        int endCol = (row == end.row) ? end.col + 1 : _cols;

        // Use vterm_screen_get_text for this row segment
        VTermRect rect;
        rect.start_row = row;
        rect.end_row = row + 1;
        rect.start_col = startCol;
        rect.end_col = endCol;

        // Allocate buffer (worst case: 4 bytes per cell for UTF-8)
        size_t bufSize = (endCol - startCol + 1) * 4 + 1;
        std::vector<char> buf(bufSize);

        size_t len = vterm_screen_get_text(_screen, buf.data(), bufSize, rect);

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

} // namespace yetty::test
