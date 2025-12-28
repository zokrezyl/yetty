//=============================================================================
// Terminal Test Harness - Implementation
//=============================================================================

#include "terminal_harness.h"
#include <cstring>

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

} // namespace yetty::test
