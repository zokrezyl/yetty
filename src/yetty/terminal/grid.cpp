#include "grid.h"
#include "font.h"
#include <algorithm>
#include <cstring>

namespace yetty {

Grid::Grid(uint32_t cols, uint32_t rows)
    : cols_(cols), rows_(rows) {
    size_t cellCount = cols * rows;
    glyphIndices_.resize(cellCount, 0);
    fgColors_.resize(cellCount * 4, 255);  // RGBA, default white
    bgColors_.resize(cellCount * 4, 0);    // RGBA, default black (with alpha 0)
    pluginIds_.resize(cellCount, 0);       // No plugins initially

    // Set default alpha for fg colors
    for (size_t i = 0; i < cellCount; ++i) {
        fgColors_[i * 4 + 3] = 255;  // Alpha = 255
    }
}

void Grid::resize(uint32_t cols, uint32_t rows) {
    if (cols == cols_ && rows == rows_) return;

    size_t newCellCount = cols * rows;
    std::vector<uint16_t> newGlyphs(newCellCount, 0);
    std::vector<uint8_t> newFgColors(newCellCount * 4, 255);
    std::vector<uint8_t> newBgColors(newCellCount * 4, 0);
    std::vector<uint16_t> newPluginIds(newCellCount, 0);

    // Set alpha for new fg colors
    for (size_t i = 0; i < newCellCount; ++i) {
        newFgColors[i * 4 + 3] = 255;
    }

    // Copy existing content
    uint32_t copyRows = std::min(rows, rows_);
    uint32_t copyCols = std::min(cols, cols_);

    for (uint32_t r = 0; r < copyRows; ++r) {
        for (uint32_t c = 0; c < copyCols; ++c) {
            size_t oldIdx = r * cols_ + c;
            size_t newIdx = r * cols + c;

            newGlyphs[newIdx] = glyphIndices_[oldIdx];
            newPluginIds[newIdx] = pluginIds_[oldIdx];

            newFgColors[newIdx * 4 + 0] = fgColors_[oldIdx * 4 + 0];
            newFgColors[newIdx * 4 + 1] = fgColors_[oldIdx * 4 + 1];
            newFgColors[newIdx * 4 + 2] = fgColors_[oldIdx * 4 + 2];
            newFgColors[newIdx * 4 + 3] = fgColors_[oldIdx * 4 + 3];

            newBgColors[newIdx * 4 + 0] = bgColors_[oldIdx * 4 + 0];
            newBgColors[newIdx * 4 + 1] = bgColors_[oldIdx * 4 + 1];
            newBgColors[newIdx * 4 + 2] = bgColors_[oldIdx * 4 + 2];
            newBgColors[newIdx * 4 + 3] = bgColors_[oldIdx * 4 + 3];
        }
    }

    cols_ = cols;
    rows_ = rows;
    glyphIndices_ = std::move(newGlyphs);
    fgColors_ = std::move(newFgColors);
    bgColors_ = std::move(newBgColors);
    pluginIds_ = std::move(newPluginIds);
    dirty_ = true;
}

void Grid::clear() {
    size_t cellCount = cols_ * rows_;
    std::fill(glyphIndices_.begin(), glyphIndices_.end(), 0);
    std::fill(pluginIds_.begin(), pluginIds_.end(), 0);

    // Reset fg to white, bg to black
    for (size_t i = 0; i < cellCount; ++i) {
        fgColors_[i * 4 + 0] = 255;
        fgColors_[i * 4 + 1] = 255;
        fgColors_[i * 4 + 2] = 255;
        fgColors_[i * 4 + 3] = 255;

        bgColors_[i * 4 + 0] = 0;
        bgColors_[i * 4 + 1] = 0;
        bgColors_[i * 4 + 2] = 0;
        bgColors_[i * 4 + 3] = 0;
    }
    dirty_ = true;
}

void Grid::setCell(uint32_t col, uint32_t row, uint16_t glyphIndex,
                   uint8_t fgR, uint8_t fgG, uint8_t fgB,
                   uint8_t bgR, uint8_t bgG, uint8_t bgB) {
    if (col >= cols_ || row >= rows_) return;

    size_t idx = cellIndex(col, row);
    glyphIndices_[idx] = glyphIndex;

    fgColors_[idx * 4 + 0] = fgR;
    fgColors_[idx * 4 + 1] = fgG;
    fgColors_[idx * 4 + 2] = fgB;
    fgColors_[idx * 4 + 3] = 255;

    bgColors_[idx * 4 + 0] = bgR;
    bgColors_[idx * 4 + 1] = bgG;
    bgColors_[idx * 4 + 2] = bgB;
    bgColors_[idx * 4 + 3] = 255;

    dirty_ = true;
}

void Grid::setGlyph(uint32_t col, uint32_t row, uint16_t glyphIndex) {
    if (col >= cols_ || row >= rows_) return;
    glyphIndices_[cellIndex(col, row)] = glyphIndex;
    dirty_ = true;
}

void Grid::setFgColor(uint32_t col, uint32_t row, uint8_t r, uint8_t g, uint8_t b) {
    if (col >= cols_ || row >= rows_) return;
    size_t idx = cellIndex(col, row) * 4;
    fgColors_[idx + 0] = r;
    fgColors_[idx + 1] = g;
    fgColors_[idx + 2] = b;
    fgColors_[idx + 3] = 255;
    dirty_ = true;
}

void Grid::setBgColor(uint32_t col, uint32_t row, uint8_t r, uint8_t g, uint8_t b) {
    if (col >= cols_ || row >= rows_) return;
    size_t idx = cellIndex(col, row) * 4;
    bgColors_[idx + 0] = r;
    bgColors_[idx + 1] = g;
    bgColors_[idx + 2] = b;
    bgColors_[idx + 3] = 255;
    dirty_ = true;
}

uint16_t Grid::getGlyph(uint32_t col, uint32_t row) const {
    if (col >= cols_ || row >= rows_) return 0;
    return glyphIndices_[cellIndex(col, row)];
}

void Grid::getFgColor(uint32_t col, uint32_t row, uint8_t& r, uint8_t& g, uint8_t& b) const {
    if (col >= cols_ || row >= rows_) {
        r = g = b = 255;
        return;
    }
    size_t idx = cellIndex(col, row) * 4;
    r = fgColors_[idx + 0];
    g = fgColors_[idx + 1];
    b = fgColors_[idx + 2];
}

void Grid::setPluginId(uint32_t col, uint32_t row, uint16_t pluginId) {
    if (col >= cols_ || row >= rows_) return;
    pluginIds_[cellIndex(col, row)] = pluginId;
}

uint16_t Grid::getPluginId(uint32_t col, uint32_t row) const {
    if (col >= cols_ || row >= rows_) return 0;
    return pluginIds_[cellIndex(col, row)];
}

void Grid::clearPluginId(uint32_t col, uint32_t row) {
    setPluginId(col, row, 0);
}

void Grid::writeString(uint32_t col, uint32_t row, const char* str,
                       uint8_t fgR, uint8_t fgG, uint8_t fgB,
                       Font* font) {
    if (!str || row >= rows_) return;

    uint32_t c = col;
    while (*str && c < cols_) {
        uint32_t codepoint = static_cast<uint8_t>(*str);
        uint16_t glyphIndex = font ? font->getGlyphIndex(codepoint) : static_cast<uint16_t>(codepoint);
        setGlyph(c, row, glyphIndex);
        setFgColor(c, row, fgR, fgG, fgB);
        ++str;
        ++c;
    }
}

void Grid::scrollUp() {
    if (rows_ <= 1) return;

    // Move all rows up by one
    for (uint32_t row = 0; row < rows_ - 1; ++row) {
        for (uint32_t col = 0; col < cols_; ++col) {
            size_t srcIdx = (row + 1) * cols_ + col;
            size_t dstIdx = row * cols_ + col;

            glyphIndices_[dstIdx] = glyphIndices_[srcIdx];

            fgColors_[dstIdx * 4 + 0] = fgColors_[srcIdx * 4 + 0];
            fgColors_[dstIdx * 4 + 1] = fgColors_[srcIdx * 4 + 1];
            fgColors_[dstIdx * 4 + 2] = fgColors_[srcIdx * 4 + 2];
            fgColors_[dstIdx * 4 + 3] = fgColors_[srcIdx * 4 + 3];

            bgColors_[dstIdx * 4 + 0] = bgColors_[srcIdx * 4 + 0];
            bgColors_[dstIdx * 4 + 1] = bgColors_[srcIdx * 4 + 1];
            bgColors_[dstIdx * 4 + 2] = bgColors_[srcIdx * 4 + 2];
            bgColors_[dstIdx * 4 + 3] = bgColors_[srcIdx * 4 + 3];
        }
    }

    // Clear bottom row
    uint32_t lastRow = rows_ - 1;
    for (uint32_t col = 0; col < cols_; ++col) {
        size_t idx = lastRow * cols_ + col;
        glyphIndices_[idx] = 0;

        fgColors_[idx * 4 + 0] = 255;
        fgColors_[idx * 4 + 1] = 255;
        fgColors_[idx * 4 + 2] = 255;
        fgColors_[idx * 4 + 3] = 255;

        bgColors_[idx * 4 + 0] = 0;
        bgColors_[idx * 4 + 1] = 0;
        bgColors_[idx * 4 + 2] = 0;
        bgColors_[idx * 4 + 3] = 0;
    }

    dirty_ = true;
}

} // namespace yetty
