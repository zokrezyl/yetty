#include "gpu-screen.h"
#include <yetty/font.h>
#include <ytrace/ytrace.hpp>
#include "grid.h"  // For GLYPH_WIDE_CONT, GLYPH_PLUGIN constants
#include <algorithm>
#include <cstring>
#include <string>

#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace yetty {

// State callbacks struct
static VTermStateCallbacks stateCallbacks = {
    .putglyph = GPUScreen::onPutglyph,
    .movecursor = GPUScreen::onMoveCursor,
    .scrollrect = GPUScreen::onScrollRect,
    .moverect = GPUScreen::onMoveRect,
    .erase = GPUScreen::onErase,
    .initpen = GPUScreen::onInitPen,
    .setpenattr = GPUScreen::onSetPenAttr,
    .settermprop = GPUScreen::onSetTermProp,
    .bell = GPUScreen::onBell,
    .resize = GPUScreen::onResize,
    .setlineinfo = GPUScreen::onSetLineInfo,
    .sb_clear = nullptr,
};

GPUScreen::GPUScreen(int rows, int cols, Font* font, size_t maxScrollback)
    : maxScrollback_(maxScrollback)
    , rows_(rows)
    , cols_(cols)
    , font_(font)
{
    // Initialize default colors
    vterm_color_rgb(&defaultFg_, 204, 204, 204);  // Light gray
    vterm_color_rgb(&defaultBg_, 15, 15, 35);     // Dark blue-ish

    pen_.fg = defaultFg_;
    pen_.bg = defaultBg_;

    // Cache space glyph index to avoid repeated lookups in hot paths
    cachedSpaceGlyph_ = font_ ? font_->getGlyphIndex(' ') : 0;

    // Allocate buffers
    resize(rows, cols);
}

GPUScreen::~GPUScreen() = default;

void GPUScreen::attach(VTerm* vt) {
    vterm_ = vt;
    state_ = vterm_obtain_state(vt);
    
    // Register our callbacks with State layer
    vterm_state_set_callbacks(state_, &stateCallbacks, this);
    
    // Get default colors from state
    vterm_state_get_default_colors(state_, &defaultFg_, &defaultBg_);
    pen_.fg = defaultFg_;
    pen_.bg = defaultBg_;

    // Reset state (triggers initpen, clears screen)
    vterm_state_reset(state_, 1);
}

void GPUScreen::resize(int rows, int cols) {
    // Check if this is a no-op (same size AND buffers already allocated)
    if (rows == rows_ && cols == cols_ && !visibleGlyphs_.empty()) {
        return;  // No change
    }
    
    int oldRows = rows_;
    int oldCols = cols_;
    bool hasOldContent = !visibleGlyphs_.empty();
    
    // Save old buffers
    auto oldGlyphs = std::move(visibleGlyphs_);
    auto oldFgColors = std::move(visibleFgColors_);
    auto oldBgColors = std::move(visibleBgColors_);
    auto oldAttrs = std::move(visibleAttrs_);

    rows_ = rows;
    cols_ = cols;

    size_t numCells = static_cast<size_t>(rows * cols);

    // Allocate new buffers (clear first to ensure clean state)
    visibleGlyphs_.clear();
    visibleFgColors_.clear();
    visibleBgColors_.clear();
    visibleAttrs_.clear();
    viewGlyphs_.clear();
    viewFgColors_.clear();
    viewBgColors_.clear();
    viewAttrs_.clear();

    visibleGlyphs_.resize(numCells);
    visibleFgColors_.resize(numCells * 4);
    visibleBgColors_.resize(numCells * 4);
    visibleAttrs_.resize(numCells);

    viewGlyphs_.resize(numCells);
    viewFgColors_.resize(numCells * 4);
    viewBgColors_.resize(numCells * 4);
    viewAttrs_.resize(numCells);

    // Pre-allocate scratch buffers for onMoveRect (one full row)
    scratchGlyphs_.resize(cols);
    scratchFgColors_.resize(cols * 4);
    scratchBgColors_.resize(cols * 4);
    scratchAttrs_.resize(cols);

    // Initialize with default background
    uint8_t fgR, fgG, fgB, bgR, bgG, bgB;
    colorToRGB(defaultFg_, fgR, fgG, fgB);
    colorToRGB(defaultBg_, bgR, bgG, bgB);

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            setCell(row, col, cachedSpaceGlyph_, fgR, fgG, fgB, bgR, bgG, bgB, 0);
        }
    }
    
    // Copy old content (as much as fits) - only if there was old content
    if (hasOldContent && oldRows > 0 && oldCols > 0) {
        int copyRows = std::min(oldRows, rows);
        int copyCols = std::min(oldCols, cols);

        for (int row = 0; row < copyRows; row++) {
            for (int col = 0; col < copyCols; col++) {
                size_t oldIdx = static_cast<size_t>(row * oldCols + col);
                size_t newIdx = cellIndex(row, col);

                // Bounds check on old buffers
                if (oldIdx < oldGlyphs.size()) {
                    visibleGlyphs_[newIdx] = oldGlyphs[oldIdx];
                }
                if (oldIdx * 4 + 3 < oldFgColors.size()) {
                    std::memcpy(&visibleFgColors_[newIdx * 4], &oldFgColors[oldIdx * 4], 4);
                }
                if (oldIdx * 4 + 3 < oldBgColors.size()) {
                    std::memcpy(&visibleBgColors_[newIdx * 4], &oldBgColors[oldIdx * 4], 4);
                }
                if (oldIdx < oldAttrs.size()) {
                    visibleAttrs_[newIdx] = oldAttrs[oldIdx];
                }
            }
        }
    }
    
    // Clamp cursor to new dimensions
    if (cursorRow_ >= rows) cursorRow_ = rows - 1;
    if (cursorCol_ >= cols) cursorCol_ = cols - 1;
    if (cursorRow_ < 0) cursorRow_ = 0;
    if (cursorCol_ < 0) cursorCol_ = 0;
    
    // Reset scroll offset on resize (back to live view)
    scrollOffset_ = 0;
    
    hasDamage_ = true;
    viewBufferDirty_ = true;
}

void GPUScreen::reset() {
    // Clear all cells with space and default colors
    uint8_t fgR, fgG, fgB, bgR, bgG, bgB;
    colorToRGB(defaultFg_, fgR, fgG, fgB);
    colorToRGB(defaultBg_, bgR, bgG, bgB);

    for (int row = 0; row < rows_; row++) {
        for (int col = 0; col < cols_; col++) {
            setCell(row, col, cachedSpaceGlyph_, fgR, fgG, fgB, bgR, bgG, bgB, 0);
        }
    }

    cursorRow_ = 0;
    cursorCol_ = 0;
    cursorVisible_ = true;
    hasDamage_ = true;
    viewBufferDirty_ = true;
    scrollOffset_ = 0;
}

//=============================================================================
// Buffer access - returns view buffer (handles scrollback blending)
//=============================================================================

const uint16_t* GPUScreen::getGlyphData() const {
    if (scrollOffset_ == 0) {
        return visibleGlyphs_.data();
    }
    // When scrolled back, we need to compose the view
    const_cast<GPUScreen*>(this)->composeViewBuffer();
    return viewGlyphs_.data();
}

const uint8_t* GPUScreen::getFgColorData() const {
    if (scrollOffset_ == 0) {
        return visibleFgColors_.data();
    }
    const_cast<GPUScreen*>(this)->composeViewBuffer();
    return viewFgColors_.data();
}

const uint8_t* GPUScreen::getBgColorData() const {
    if (scrollOffset_ == 0) {
        return visibleBgColors_.data();
    }
    const_cast<GPUScreen*>(this)->composeViewBuffer();
    return viewBgColors_.data();
}

const uint8_t* GPUScreen::getAttrsData() const {
    if (scrollOffset_ == 0) {
        return visibleAttrs_.data();
    }
    const_cast<GPUScreen*>(this)->composeViewBuffer();
    return viewAttrs_.data();
}

//=============================================================================
// Scrollback control
//=============================================================================

void GPUScreen::scrollUp(int lines) {
    int maxOffset = static_cast<int>(scrollback_.size());
    int newOffset = std::min(scrollOffset_ + lines, maxOffset);
    if (newOffset != scrollOffset_) {
        scrollOffset_ = newOffset;
        viewBufferDirty_ = true;
        hasDamage_ = true;
    }
}

void GPUScreen::scrollDown(int lines) {
    int newOffset = std::max(scrollOffset_ - lines, 0);
    if (newOffset != scrollOffset_) {
        scrollOffset_ = newOffset;
        viewBufferDirty_ = true;
        hasDamage_ = true;
    }
}

void GPUScreen::scrollToTop() {
    int maxOffset = static_cast<int>(scrollback_.size());
    if (scrollOffset_ != maxOffset) {
        scrollOffset_ = maxOffset;
        viewBufferDirty_ = true;
        hasDamage_ = true;
    }
}

void GPUScreen::scrollToBottom() {
    if (scrollOffset_ != 0) {
        scrollOffset_ = 0;
        viewBufferDirty_ = true;
        hasDamage_ = true;
    }
}

//=============================================================================
// View buffer composition (when scrolled back)
//=============================================================================

void GPUScreen::composeViewBuffer() {
    if (!viewBufferDirty_) return;
    
    // scrollOffset_ lines come from scrollback, rest from visible
    // scrollback_[size-1] is newest, scrollback_[0] is oldest
    // When scrollOffset_ = N, we show scrollback lines [size-N, size-1] at top
    // plus visible lines [0, rows-N-1] shifted down
    
    int sbSize = static_cast<int>(scrollback_.size());
    int sbLinesToShow = std::min(scrollOffset_, rows_);
    int visibleLinesToShow = rows_ - sbLinesToShow;
    
    // Fill top rows from scrollback (newest first in view)
    for (int viewRow = 0; viewRow < sbLinesToShow; viewRow++) {
        // scrollback index: newest lines are at end
        // viewRow 0 should get scrollback_[sbSize - scrollOffset_]
        int sbIndex = sbSize - scrollOffset_ + viewRow;
        if (sbIndex >= 0 && sbIndex < sbSize) {
            decompressLine(scrollback_[sbIndex], viewRow);
        }
    }
    
    // Fill bottom rows from visible buffer
    for (int viewRow = sbLinesToShow; viewRow < rows_; viewRow++) {
        int visRow = viewRow - sbLinesToShow;  // Source row in visible buffer
        size_t numCells = static_cast<size_t>(cols_);
        size_t srcOffset = static_cast<size_t>(visRow * cols_);
        size_t dstOffset = static_cast<size_t>(viewRow * cols_);

        std::memcpy(&viewGlyphs_[dstOffset], &visibleGlyphs_[srcOffset], numCells * sizeof(uint16_t));
        std::memcpy(&viewFgColors_[dstOffset * 4], &visibleFgColors_[srcOffset * 4], numCells * 4);
        std::memcpy(&viewBgColors_[dstOffset * 4], &visibleBgColors_[srcOffset * 4], numCells * 4);
        std::memcpy(&viewAttrs_[dstOffset], &visibleAttrs_[srcOffset], numCells);
    }

    viewBufferDirty_ = false;
}

void GPUScreen::decompressLine(const ScrollbackLineGPU& line, int viewRow) {
    size_t dstOffset = static_cast<size_t>(viewRow * cols_);

    // Copy glyphs only (no codepoints needed)
    int lineCols = std::min(static_cast<int>(line.glyphs.size()), cols_);
    for (int col = 0; col < lineCols; col++) {
        viewGlyphs_[dstOffset + col] = line.glyphs[col];
    }
    // Fill remainder with spaces
    for (int col = lineCols; col < cols_; col++) {
        viewGlyphs_[dstOffset + col] = cachedSpaceGlyph_;
    }

    // Decompress RLE styles
    int col = 0;
    for (const auto& run : line.styleRuns) {
        for (uint16_t i = 0; i < run.count && col < cols_; i++, col++) {
            size_t colorIdx = (dstOffset + col) * 4;
            viewFgColors_[colorIdx + 0] = run.style.fgR;
            viewFgColors_[colorIdx + 1] = run.style.fgG;
            viewFgColors_[colorIdx + 2] = run.style.fgB;
            viewFgColors_[colorIdx + 3] = 255;
            viewBgColors_[colorIdx + 0] = run.style.bgR;
            viewBgColors_[colorIdx + 1] = run.style.bgG;
            viewBgColors_[colorIdx + 2] = run.style.bgB;
            viewBgColors_[colorIdx + 3] = 255;
            viewAttrs_[dstOffset + col] = static_cast<uint8_t>(run.style.attrs);
        }
    }
    // Fill remainder with default colors
    uint8_t fgR = defaultFg_.rgb.red, fgG = defaultFg_.rgb.green, fgB = defaultFg_.rgb.blue;
    uint8_t bgR = defaultBg_.rgb.red, bgG = defaultBg_.rgb.green, bgB = defaultBg_.rgb.blue;
    for (; col < cols_; col++) {
        size_t colorIdx = (dstOffset + col) * 4;
        viewFgColors_[colorIdx + 0] = fgR;
        viewFgColors_[colorIdx + 1] = fgG;
        viewFgColors_[colorIdx + 2] = fgB;
        viewFgColors_[colorIdx + 3] = 255;
        viewBgColors_[colorIdx + 0] = bgR;
        viewBgColors_[colorIdx + 1] = bgG;
        viewBgColors_[colorIdx + 2] = bgB;
        viewBgColors_[colorIdx + 3] = 255;
        viewAttrs_[dstOffset + col] = 0;
    }
}

//=============================================================================
// Scrollback push (compress line and store)
//=============================================================================

void GPUScreen::pushLineToScrollback(int row) {
    ScrollbackLineGPU line;
    line.glyphs.resize(cols_);

    // Copy glyph data only (no codepoints needed)
    size_t srcOffset = static_cast<size_t>(row * cols_);
    for (int col = 0; col < cols_; col++) {
        line.glyphs[col] = visibleGlyphs_[srcOffset + col];
    }

    // Scan this row for widget markers - if found, track in scrolledOutWidgets_
    yinfo("GPUScreen::pushLineToScrollback: scanning row {} for markers", row);
    for (int col = 0; col < cols_; col++) {
        if (visibleGlyphs_[srcOffset + col] == GLYPH_PLUGIN) {
            size_t colorIdx = (srcOffset + col) * 4;
            yinfo("GPUScreen::pushLineToScrollback: found GLYPH_PLUGIN at row={} col={}, checking validation", row, col);
            // Validate marker pattern
            if (visibleFgColors_[colorIdx + 2] == 0xFF &&
                visibleFgColors_[colorIdx + 3] == 0xFF &&
                visibleBgColors_[colorIdx + 0] == 0xAA &&
                visibleBgColors_[colorIdx + 1] == 0xAA) {
                uint16_t widgetId = static_cast<uint16_t>(visibleFgColors_[colorIdx + 0]) |
                                   (static_cast<uint16_t>(visibleFgColors_[colorIdx + 1]) << 8);
                yinfo("GPUScreen::pushLineToScrollback: VALID marker! widget {} at row={} col={} -> scrolledOutWidgets_ with y=-1",
                       widgetId, row, col);
                // Add with Y = -1 (just scrolled out)
                scrolledOutWidgets_.push_back({widgetId, -1, col});
            } else {
                yinfo("GPUScreen::pushLineToScrollback: marker validation FAILED fg[2]={} fg[3]={} bg[0]={} bg[1]={}",
                      visibleFgColors_[colorIdx + 2], visibleFgColors_[colorIdx + 3],
                      visibleBgColors_[colorIdx + 0], visibleBgColors_[colorIdx + 1]);
            }
        }
    }

    // RLE compress styles
    ScrollbackStyle lastStyle;
    size_t colorIdx = srcOffset * 4;
    lastStyle.fgR = visibleFgColors_[colorIdx + 0];
    lastStyle.fgG = visibleFgColors_[colorIdx + 1];
    lastStyle.fgB = visibleFgColors_[colorIdx + 2];
    lastStyle.bgR = visibleBgColors_[colorIdx + 0];
    lastStyle.bgG = visibleBgColors_[colorIdx + 1];
    lastStyle.bgB = visibleBgColors_[colorIdx + 2];
    lastStyle.attrs = visibleAttrs_[srcOffset];
    uint16_t runCount = 1;

    for (int col = 1; col < cols_; col++) {
        colorIdx = (srcOffset + col) * 4;
        ScrollbackStyle style;
        style.fgR = visibleFgColors_[colorIdx + 0];
        style.fgG = visibleFgColors_[colorIdx + 1];
        style.fgB = visibleFgColors_[colorIdx + 2];
        style.bgR = visibleBgColors_[colorIdx + 0];
        style.bgG = visibleBgColors_[colorIdx + 1];
        style.bgB = visibleBgColors_[colorIdx + 2];
        style.attrs = visibleAttrs_[srcOffset + col];

        if (style == lastStyle && runCount < 65535) {
            runCount++;
        } else {
            line.styleRuns.push_back({lastStyle, runCount});
            lastStyle = style;
            runCount = 1;
        }
    }
    line.styleRuns.push_back({lastStyle, runCount});

    scrollback_.push_back(std::move(line));

    // If scrolled back, increment scroll offset to maintain view position
    // (otherwise the view would shift as new lines push into scrollback)
    if (scrollOffset_ > 0) {
        scrollOffset_++;
    }

    // Decrement Y for all scrolled-out widgets (they move up with each scroll)
    int maxNegativeY = -static_cast<int>(maxScrollback_);
    if (!scrolledOutWidgets_.empty()) {
        yinfo("GPUScreen::pushLineToScrollback: decrementing Y for {} scrolled-out widgets", scrolledOutWidgets_.size());
    }
    auto it = scrolledOutWidgets_.begin();
    while (it != scrolledOutWidgets_.end()) {
        int oldY = it->y;
        it->y--;
        yinfo("GPUScreen::pushLineToScrollback: widget {} y: {} -> {}", it->widgetId, oldY, it->y);
        if (it->y < maxNegativeY) {
            // This widget's marker has been removed from scrollback
            yinfo("GPUScreen: widget {} y={} below threshold {}, disposing", it->widgetId, it->y, maxNegativeY);
            if (widgetDisposalCallback_) {
                widgetDisposalCallback_(it->widgetId);
            }
            it = scrolledOutWidgets_.erase(it);
        } else {
            ++it;
        }
    }

    // Limit scrollback size
    while (scrollback_.size() > maxScrollback_) {
        scrollback_.pop_front();
        // Adjust scroll offset if we removed lines from the beginning
        if (scrollOffset_ > static_cast<int>(scrollback_.size())) {
            scrollOffset_ = static_cast<int>(scrollback_.size());
        }
    }

    // Notify callback (for widget position updates - but we already updated Y above)
    if (scrollCallback_) {
        scrollCallback_(1);
    }
}

//=============================================================================
// Cell manipulation (writes to visible buffer)
//=============================================================================

void GPUScreen::setCell(int row, int col, uint16_t glyph,
                        uint8_t fgR, uint8_t fgG, uint8_t fgB,
                        uint8_t bgR, uint8_t bgG, uint8_t bgB,
                        uint8_t attrsByte) {
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_) return;

    size_t idx = cellIndex(row, col);

    // Bounds check on buffers
    if (idx >= visibleGlyphs_.size()) return;

    visibleGlyphs_[idx] = glyph;

    size_t colorIdx = idx * 4;
    if (colorIdx + 3 >= visibleFgColors_.size()) return;

    visibleFgColors_[colorIdx + 0] = fgR;
    visibleFgColors_[colorIdx + 1] = fgG;
    visibleFgColors_[colorIdx + 2] = fgB;
    visibleFgColors_[colorIdx + 3] = 255;

    visibleBgColors_[colorIdx + 0] = bgR;
    visibleBgColors_[colorIdx + 1] = bgG;
    visibleBgColors_[colorIdx + 2] = bgB;
    visibleBgColors_[colorIdx + 3] = 255;

    visibleAttrs_[idx] = attrsByte;
}

void GPUScreen::clearCell(int row, int col) {
    uint8_t fgR, fgG, fgB, bgR, bgG, bgB;
    colorToRGB(pen_.fg, fgR, fgG, fgB);
    colorToRGB(pen_.bg, bgR, bgG, bgB);

    if (pen_.reverse) {
        std::swap(fgR, bgR);
        std::swap(fgG, bgG);
        std::swap(fgB, bgB);
    }

    setCell(row, col, cachedSpaceGlyph_, fgR, fgG, fgB, bgR, bgG, bgB, 0);
}

void GPUScreen::colorToRGB(const VTermColor& color, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (VTERM_COLOR_IS_DEFAULT_FG(&color)) {
        r = defaultFg_.rgb.red;
        g = defaultFg_.rgb.green;
        b = defaultFg_.rgb.blue;
    } else if (VTERM_COLOR_IS_DEFAULT_BG(&color)) {
        r = defaultBg_.rgb.red;
        g = defaultBg_.rgb.green;
        b = defaultBg_.rgb.blue;
    } else if (VTERM_COLOR_IS_INDEXED(&color)) {
        VTermColor rgb = color;
        if (state_) {
            vterm_state_convert_color_to_rgb(state_, &rgb);
        }
        r = rgb.rgb.red;
        g = rgb.rgb.green;
        b = rgb.rgb.blue;
    } else {
        r = color.rgb.red;
        g = color.rgb.green;
        b = color.rgb.blue;
    }
}

//=============================================================================
// State callbacks
//=============================================================================

int GPUScreen::onPutglyph(VTermGlyphInfo* info, VTermPos pos, void* user) {
    auto* self = static_cast<GPUScreen*>(user);

    uint32_t cp = info->chars[0];
    if (cp == 0) cp = ' ';

    // Get glyph index from font
    uint16_t glyphIdx = self->font_
        ? self->font_->getGlyphIndex(cp, self->pen_.bold, self->pen_.italic)
        : static_cast<uint16_t>(cp);

    // Get colors from current pen
    uint8_t fgR, fgG, fgB, bgR, bgG, bgB;
    self->colorToRGB(self->pen_.fg, fgR, fgG, fgB);
    self->colorToRGB(self->pen_.bg, bgR, bgG, bgB);

    if (self->pen_.reverse) {
        std::swap(fgR, bgR);
        std::swap(fgG, bgG);
        std::swap(fgB, bgB);
    }

    // Pack attributes
    uint8_t attrsByte = 0;
    if (self->pen_.bold) attrsByte |= 0x01;
    if (self->pen_.italic) attrsByte |= 0x02;
    attrsByte |= (self->pen_.underline & 0x03) << 2;
    if (self->pen_.strike) attrsByte |= 0x10;

    self->setCell(pos.row, pos.col, glyphIdx, fgR, fgG, fgB, bgR, bgG, bgB, attrsByte);

    // Handle wide characters (width > 1)
    for (int i = 1; i < info->width; i++) {
        self->setCell(pos.row, pos.col + i, GLYPH_WIDE_CONT,
                      fgR, fgG, fgB, bgR, bgG, bgB, attrsByte);
    }

    self->hasDamage_ = true;
    return 1;
}

int GPUScreen::onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user) {
    auto* self = static_cast<GPUScreen*>(user);
    ydebug("GPUScreen::onMoveCursor: ({},{}) -> ({},{})", oldpos.row, oldpos.col, pos.row, pos.col);
    self->cursorRow_ = pos.row;
    self->cursorCol_ = pos.col;
    self->cursorVisible_ = visible != 0;
    self->hasDamage_ = true;
    return 1;
}

int GPUScreen::onScrollRect(VTermRect rect, int downward, int rightward, void* user) {
    auto* self = static_cast<GPUScreen*>(user);
    (void)rightward;

    yinfo("GPUScreen::onScrollRect: rect=[{},{})x[{},{}) downward={} rightward={}",
           rect.start_row, rect.end_row, rect.start_col, rect.end_col, downward, rightward);

    bool isFullWidth = (rect.start_col == 0 && rect.end_col == self->cols_);

    // Save lines to scrollback BEFORE vterm moves content (via moverect/erase)
    // downward > 0: content moves down, top rows pushed to scrollback
    // downward < 0: content moves up, bottom rows cleared (less common)
    if (downward > 0 && rect.start_row == 0 && isFullWidth) {
        yinfo("GPUScreen::onScrollRect: pushing {} lines to scrollback (downward > 0)", downward);
        for (int i = 0; i < downward && i < rect.end_row; i++) {
            self->pushLineToScrollback(i);
        }
    } else if (downward < 0) {
        // Scroll up: top lines will be overwritten
        int upAmount = -downward;
        yinfo("GPUScreen::onScrollRect: upAmount={} isFullWidth={} start_row={}", upAmount, isFullWidth, rect.start_row);

        if (rect.start_row == 0 && isFullWidth) {
            // Full-screen scroll from top - push to scrollback
            yinfo("GPUScreen::onScrollRect: pushing {} lines to scrollback (downward < 0)", upAmount);
            for (int i = 0; i < upAmount && i < rect.end_row; i++) {
                self->pushLineToScrollback(i);
            }
        } else if (isFullWidth) {
            // Scroll within region (not from row 0) - still notify for widget position updates
            ydebug("GPUScreen::onScrollRect: scroll region [{},{}), notifying {} lines",
                   rect.start_row, rect.end_row, upAmount);
            if (self->scrollCallback_) {
                self->scrollCallback_(upAmount);
            }
        }
    }

    // Return 0 to let vterm handle the actual scroll via moverect/erase callbacks
    return 0;
}

int GPUScreen::onMoveRect(VTermRect dest, VTermRect src, void* user) {
    auto* self = static_cast<GPUScreen*>(user);

    int height = src.end_row - src.start_row;
    int width = src.end_col - src.start_col;
    int cols = self->cols_;
    int rows = self->rows_;

    // Bounds checking to prevent memory corruption
    if (height <= 0 || width <= 0) return 1;
    if (src.start_row < 0 || src.end_row > rows) return 1;
    if (dest.start_row < 0 || dest.start_row + height > rows) return 1;
    if (src.start_col < 0 || src.end_col > cols) return 1;
    if (dest.start_col < 0 || dest.start_col + width > cols) return 1;

    // Ultra-fast path: full-width move starting at column 0
    // Memory is fully contiguous - single memmove for entire region
    if (src.start_col == 0 && dest.start_col == 0 && width == cols) {
        size_t srcIdx = self->cellIndex(src.start_row, 0);
        size_t dstIdx = self->cellIndex(dest.start_row, 0);
        size_t totalCells = static_cast<size_t>(height) * cols;

        std::memmove(&self->visibleGlyphs_[dstIdx],
                    &self->visibleGlyphs_[srcIdx],
                    totalCells * sizeof(uint16_t));
        std::memmove(&self->visibleFgColors_[dstIdx * 4],
                    &self->visibleFgColors_[srcIdx * 4],
                    totalCells * 4);
        std::memmove(&self->visibleBgColors_[dstIdx * 4],
                    &self->visibleBgColors_[srcIdx * 4],
                    totalCells * 4);
        std::memmove(&self->visibleAttrs_[dstIdx],
                    &self->visibleAttrs_[srcIdx],
                    totalCells);
    }
    // Fast path: full-width but not starting at column 0 (scroll regions)
    // Still contiguous per row, do row-by-row
    else if (src.start_col == dest.start_col && width == cols) {
        if (dest.start_row < src.start_row) {
            for (int row = 0; row < height; row++) {
                size_t srcIdx = self->cellIndex(src.start_row + row, 0);
                size_t dstIdx = self->cellIndex(dest.start_row + row, 0);

                std::memmove(&self->visibleGlyphs_[dstIdx],
                            &self->visibleGlyphs_[srcIdx],
                            width * sizeof(uint16_t));
                std::memmove(&self->visibleFgColors_[dstIdx * 4],
                            &self->visibleFgColors_[srcIdx * 4],
                            width * 4);
                std::memmove(&self->visibleBgColors_[dstIdx * 4],
                            &self->visibleBgColors_[srcIdx * 4],
                            width * 4);
                std::memmove(&self->visibleAttrs_[dstIdx],
                            &self->visibleAttrs_[srcIdx],
                            width);
            }
        } else {
            for (int row = height - 1; row >= 0; row--) {
                size_t srcIdx = self->cellIndex(src.start_row + row, 0);
                size_t dstIdx = self->cellIndex(dest.start_row + row, 0);

                std::memmove(&self->visibleGlyphs_[dstIdx],
                            &self->visibleGlyphs_[srcIdx],
                            width * sizeof(uint16_t));
                std::memmove(&self->visibleFgColors_[dstIdx * 4],
                            &self->visibleFgColors_[srcIdx * 4],
                            width * 4);
                std::memmove(&self->visibleBgColors_[dstIdx * 4],
                            &self->visibleBgColors_[srcIdx * 4],
                            width * 4);
                std::memmove(&self->visibleAttrs_[dstIdx],
                            &self->visibleAttrs_[srcIdx],
                            width);
            }
        }
    } else {
        // General case: use pre-allocated scratch buffer, copy row-by-row
        // Ensure scratch buffers are large enough
        if (static_cast<int>(self->scratchGlyphs_.size()) < width) {
            self->scratchGlyphs_.resize(width);
            self->scratchFgColors_.resize(width * 4);
            self->scratchBgColors_.resize(width * 4);
            self->scratchAttrs_.resize(width);
        }

        // Determine copy direction
        bool copyForward = (dest.start_row < src.start_row) ||
                          (dest.start_row == src.start_row && dest.start_col <= src.start_col);

        if (copyForward) {
            for (int row = 0; row < height; row++) {
                size_t srcRowStart = self->cellIndex(src.start_row + row, src.start_col);
                size_t dstRowStart = self->cellIndex(dest.start_row + row, dest.start_col);

                // Copy to scratch
                std::memcpy(self->scratchGlyphs_.data(),
                           &self->visibleGlyphs_[srcRowStart],
                           width * sizeof(uint16_t));
                std::memcpy(self->scratchFgColors_.data(),
                           &self->visibleFgColors_[srcRowStart * 4],
                           width * 4);
                std::memcpy(self->scratchBgColors_.data(),
                           &self->visibleBgColors_[srcRowStart * 4],
                           width * 4);
                std::memcpy(self->scratchAttrs_.data(),
                           &self->visibleAttrs_[srcRowStart],
                           width);

                // Copy from scratch to dest
                std::memcpy(&self->visibleGlyphs_[dstRowStart],
                           self->scratchGlyphs_.data(),
                           width * sizeof(uint16_t));
                std::memcpy(&self->visibleFgColors_[dstRowStart * 4],
                           self->scratchFgColors_.data(),
                           width * 4);
                std::memcpy(&self->visibleBgColors_[dstRowStart * 4],
                           self->scratchBgColors_.data(),
                           width * 4);
                std::memcpy(&self->visibleAttrs_[dstRowStart],
                           self->scratchAttrs_.data(),
                           width);
            }
        } else {
            for (int row = height - 1; row >= 0; row--) {
                size_t srcRowStart = self->cellIndex(src.start_row + row, src.start_col);
                size_t dstRowStart = self->cellIndex(dest.start_row + row, dest.start_col);

                // Copy to scratch
                std::memcpy(self->scratchGlyphs_.data(),
                           &self->visibleGlyphs_[srcRowStart],
                           width * sizeof(uint16_t));
                std::memcpy(self->scratchFgColors_.data(),
                           &self->visibleFgColors_[srcRowStart * 4],
                           width * 4);
                std::memcpy(self->scratchBgColors_.data(),
                           &self->visibleBgColors_[srcRowStart * 4],
                           width * 4);
                std::memcpy(self->scratchAttrs_.data(),
                           &self->visibleAttrs_[srcRowStart],
                           width);

                // Copy from scratch to dest
                std::memcpy(&self->visibleGlyphs_[dstRowStart],
                           self->scratchGlyphs_.data(),
                           width * sizeof(uint16_t));
                std::memcpy(&self->visibleFgColors_[dstRowStart * 4],
                           self->scratchFgColors_.data(),
                           width * 4);
                std::memcpy(&self->visibleBgColors_[dstRowStart * 4],
                           self->scratchBgColors_.data(),
                           width * 4);
                std::memcpy(&self->visibleAttrs_[dstRowStart],
                           self->scratchAttrs_.data(),
                           width);
            }
        }
    }

    self->hasDamage_ = true;
    self->viewBufferDirty_ = true;
    return 1;
}

int GPUScreen::onErase(VTermRect rect, int, void* user) {
    auto* self = static_cast<GPUScreen*>(user);

    // Bounds checking to prevent memory corruption
    int startRow = std::max(0, rect.start_row);
    int endRow = std::min(self->rows_, rect.end_row);
    int startCol = std::max(0, rect.start_col);
    int endCol = std::min(self->cols_, rect.end_col);

    for (int row = startRow; row < endRow; row++) {
        for (int col = startCol; col < endCol; col++) {
            self->clearCell(row, col);
        }
    }

    self->hasDamage_ = true;
    if (self->scrollOffset_ > 0) {
        self->viewBufferDirty_ = true;
    }
    return 1;
}

int GPUScreen::onInitPen(void* user) {
    auto* self = static_cast<GPUScreen*>(user);
    
    // Reset pen to defaults
    self->pen_.fg = self->defaultFg_;
    self->pen_.bg = self->defaultBg_;
    self->pen_.bold = false;
    self->pen_.italic = false;
    self->pen_.underline = 0;
    self->pen_.strike = false;
    self->pen_.reverse = false;
    self->pen_.blink = false;
    
    return 1;
}

int GPUScreen::onSetPenAttr(VTermAttr attr, VTermValue* val, void* user) {
    auto* self = static_cast<GPUScreen*>(user);
    
    switch (attr) {
        case VTERM_ATTR_BOLD:
            self->pen_.bold = val->boolean != 0;
            break;
        case VTERM_ATTR_ITALIC:
            self->pen_.italic = val->boolean != 0;
            break;
        case VTERM_ATTR_UNDERLINE:
            self->pen_.underline = static_cast<uint8_t>(val->number & 0x03);
            break;
        case VTERM_ATTR_STRIKE:
            self->pen_.strike = val->boolean != 0;
            break;
        case VTERM_ATTR_REVERSE:
            self->pen_.reverse = val->boolean != 0;
            break;
        case VTERM_ATTR_BLINK:
            self->pen_.blink = val->boolean != 0;
            break;
        case VTERM_ATTR_FOREGROUND:
            self->pen_.fg = val->color;
            break;
        case VTERM_ATTR_BACKGROUND:
            self->pen_.bg = val->color;
            break;
        default:
            break;
    }
    
    return 1;
}

int GPUScreen::onSetTermProp(VTermProp prop, VTermValue* val, void* user) {
    auto* self = static_cast<GPUScreen*>(user);
    
    if (prop == VTERM_PROP_CURSORVISIBLE) {
        self->cursorVisible_ = val->boolean != 0;
    }
    
    if (self->termPropCallback_) {
        self->termPropCallback_(prop, val);
    }
    
    return 1;
}

int GPUScreen::onBell(void* user) {
    auto* self = static_cast<GPUScreen*>(user);
    
    if (self->bellCallback_) {
        self->bellCallback_();
    }
    
    return 1;
}

int GPUScreen::onResize(int rows, int cols, VTermStateFields*, void* user) {
    auto* self = static_cast<GPUScreen*>(user);
    self->resize(rows, cols);
    return 1;
}

int GPUScreen::onSetLineInfo(int, const VTermLineInfo*, const VTermLineInfo*, void*) {
    // Line info (double-width, double-height) - not commonly used
    return 1;
}

//=============================================================================
// Widget marker methods
//=============================================================================

void GPUScreen::setWidgetMarker(int row, int col, uint16_t widgetId) {
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_) {
        ydebug("GPUScreen::setWidgetMarker: OUT OF BOUNDS row={} col={} (screen {}x{})",
               row, col, rows_, cols_);
        return;
    }

    size_t idx = cellIndex(row, col);
    if (idx >= visibleGlyphs_.size()) return;

    ydebug("GPUScreen::setWidgetMarker: row={} col={} widgetId={} idx={}",
           row, col, widgetId, idx);

    // Set glyph to GLYPH_PLUGIN (0xFFFF) to mark as widget cell
    visibleGlyphs_[idx] = GLYPH_PLUGIN;

    // Encode widget ID using fg RGB + bg R + attrs = 40 bits available
    // Use: fgR=bits0-7, fgG=bits8-15, bgR=validation marker (0xAA)
    size_t colorIdx = idx * 4;
    visibleFgColors_[colorIdx + 0] = static_cast<uint8_t>(widgetId & 0xFF);        // bits 0-7
    visibleFgColors_[colorIdx + 1] = static_cast<uint8_t>((widgetId >> 8) & 0xFF); // bits 8-15
    visibleFgColors_[colorIdx + 2] = 0xFF;  // Marker validation byte
    visibleFgColors_[colorIdx + 3] = 0xFF;  // Marker validation byte

    // Also set bg to distinguish from erased cells
    visibleBgColors_[colorIdx + 0] = 0xAA;  // Validation pattern
    visibleBgColors_[colorIdx + 1] = 0xAA;
    visibleBgColors_[colorIdx + 2] = 0xAA;
    visibleBgColors_[colorIdx + 3] = 0xAA;

    hasDamage_ = true;
}

void GPUScreen::clearWidgetMarker(int row, int col) {
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_) return;

    size_t idx = cellIndex(row, col);
    if (idx >= visibleGlyphs_.size()) return;

    // Only clear if it's actually a widget marker
    if (visibleGlyphs_[idx] == GLYPH_PLUGIN) {
        clearCell(row, col);
    }
}

void GPUScreen::trackScrolledOutWidget(uint16_t widgetId, int col) {
    // Widget marker just scrolled from row 0 into scrollback
    // Start tracking with y = -1 (just above visible area)
    ydebug("GPUScreen::trackScrolledOutWidget: widget {} col={} y=-1", widgetId, col);
    scrolledOutWidgets_.push_back({widgetId, -1, col});
}

std::vector<WidgetPosition> GPUScreen::scanWidgetPositions() const {
    std::vector<WidgetPosition> positions;

    // When scrolled back, scan the composed view buffer (includes scrollback content)
    // Otherwise scan the visible buffer directly
    const uint16_t* glyphs;
    const uint8_t* fgColors;
    const uint8_t* bgColors;

    if (scrollOffset_ > 0) {
        // Ensure view buffer is composed
        const_cast<GPUScreen*>(this)->composeViewBuffer();
        glyphs = viewGlyphs_.data();
        fgColors = viewFgColors_.data();
        bgColors = viewBgColors_.data();
    } else {
        glyphs = visibleGlyphs_.data();
        fgColors = visibleFgColors_.data();
        bgColors = visibleBgColors_.data();
    }

    int numCells = rows_ * cols_;

    // Helper lambda to validate and extract widget marker
    auto extractWidget = [&](int cellIdx) -> bool {
        size_t colorIdx = cellIdx * 4;
        // Validate marker pattern: fg[2..3] = 0xFF, bg[0..3] = 0xAA
        if (fgColors[colorIdx + 2] != 0xFF || fgColors[colorIdx + 3] != 0xFF) return false;
        if (bgColors[colorIdx + 0] != 0xAA || bgColors[colorIdx + 1] != 0xAA) return false;

        int row = cellIdx / cols_;
        int col = cellIdx % cols_;
        uint16_t widgetId = static_cast<uint16_t>(fgColors[colorIdx + 0]) |
                           (static_cast<uint16_t>(fgColors[colorIdx + 1]) << 8);
        ydebug("GPUScreen::scanWidgetPositions: found widget {} at row={} col={}",
               widgetId, row, col);
        positions.push_back({widgetId, row, col});
        return true;
    };

    int i = 0;

#if defined(__SSE2__)
    // SSE2 optimized path: compare 8 uint16_t values at once
    __m128i marker = _mm_set1_epi16(static_cast<int16_t>(GLYPH_PLUGIN));

    for (; i + 8 <= numCells; i += 8) {
        __m128i data = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&glyphs[i]));
        __m128i cmp = _mm_cmpeq_epi16(data, marker);
        int mask = _mm_movemask_epi8(cmp);

        if (mask != 0) {
            // Found at least one match - check individual cells with validation
            for (int j = 0; j < 8; j++) {
                if (glyphs[i + j] == GLYPH_PLUGIN) {
                    extractWidget(i + j);
                }
            }
        }
    }
#endif

    // Scalar fallback for remaining cells
    for (; i < numCells; i++) {
        if (glyphs[i] == GLYPH_PLUGIN) {
            extractWidget(i);
        }
    }

    // Include widgets from helper struct (markers scrolled into scrollback)
    if (!scrolledOutWidgets_.empty()) {
        yinfo("GPUScreen::scanWidgetPositions: adding {} scrolled-out widgets", scrolledOutWidgets_.size());
    }
    for (const auto& sow : scrolledOutWidgets_) {
        yinfo("GPUScreen::scanWidgetPositions: scrolled-out widget {} at y={} col={}",
               sow.widgetId, sow.y, sow.col);
        positions.push_back({sow.widgetId, sow.y, sow.col});
    }

    yinfo("GPUScreen::scanWidgetPositions: returning {} total positions", positions.size());
    return positions;
}

} // namespace yetty
