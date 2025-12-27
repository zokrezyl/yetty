#pragma once

#include <vector>
#include <cstdint>

namespace yetty {

// Special glyph index values
constexpr uint16_t GLYPH_PLUGIN = 0xFFFF;     // Cell is occupied by a decorator plugin
constexpr uint16_t GLYPH_WIDE_CONT = 0xFFFE;  // Wide char continuation (look at previous cell)

// Reserved range for custom glyph rendering (shader-based glyphs like animated emoji)
constexpr uint16_t GLYPH_CUSTOM_START = 0xF000;
constexpr uint16_t GLYPH_CUSTOM_END = 0xFFFD;

// Helper to check if a glyph index is in the custom range
inline bool isCustomGlyph(uint16_t glyphIndex) {
    return glyphIndex >= GLYPH_CUSTOM_START && glyphIndex <= GLYPH_CUSTOM_END;
}

// Cell attributes (bold, italic, etc.) packed into a single byte
struct CellAttrs {
    uint8_t _bold : 1;
    uint8_t _italic : 1;
    uint8_t _underline : 1;
    uint8_t _reserved : 5;
};

class Grid {
public:
    Grid(uint32_t cols = 80, uint32_t rows = 24);

    void resize(uint32_t cols, uint32_t rows);
    void clear();

    // Set cell data
    void setCell(uint32_t col, uint32_t row, uint16_t glyphIndex,
                 uint8_t fgR, uint8_t fgG, uint8_t fgB,
                 uint8_t bgR, uint8_t bgG, uint8_t bgB);

    void setGlyph(uint32_t col, uint32_t row, uint16_t glyphIndex);
    void setFgColor(uint32_t col, uint32_t row, uint8_t r, uint8_t g, uint8_t b);
    void setBgColor(uint32_t col, uint32_t row, uint8_t r, uint8_t g, uint8_t b);

    // Get cell data
    uint16_t getGlyph(uint32_t col, uint32_t row) const;
    void getFgColor(uint32_t col, uint32_t row, uint8_t& r, uint8_t& g, uint8_t& b) const;

    // Plugin ID tracking (separate from glyph data)
    void setPluginId(uint32_t col, uint32_t row, uint16_t pluginId);
    uint16_t getPluginId(uint32_t col, uint32_t row) const;
    void clearPluginId(uint32_t col, uint32_t row);

    // Write ASCII string (helper) - needs Font to convert codepoints to glyph indices
    // If font is nullptr, uses codepoint directly as index (for testing only)
    void writeString(uint32_t col, uint32_t row, const char* str,
                     uint8_t fgR, uint8_t fgG, uint8_t fgB,
                     class Font* font);

    void scrollUp();

    uint32_t getCols() const { return cols_; }
    uint32_t getRows() const { return rows_; }

    // Direct access to texture data for GPU upload
    const uint16_t* getGlyphData() const { return glyphIndices_.data(); }
    const uint8_t* getFgColorData() const { return fgColors_.data(); }
    const uint8_t* getBgColorData() const { return bgColors_.data(); }

    // Data sizes in bytes
    size_t getGlyphDataSize() const { return glyphIndices_.size() * sizeof(uint16_t); }
    size_t getFgColorDataSize() const { return fgColors_.size(); }
    size_t getBgColorDataSize() const { return bgColors_.size(); }

    // Mark as dirty when content changes
    bool isDirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }

private:
    uint32_t cols_;
    uint32_t rows_;

    // Texture data - flat arrays for GPU upload
    std::vector<uint16_t> glyphIndices_;  // 1 x uint16 per cell
    std::vector<uint8_t> fgColors_;       // RGBA per cell (4 bytes)
    std::vector<uint8_t> bgColors_;       // RGBA per cell (4 bytes)

    // Plugin tracking (0 = no plugin, non-zero = plugin ID)
    std::vector<uint16_t> pluginIds_;

    bool dirty_ = true;

    size_t cellIndex(uint32_t col, uint32_t row) const {
        return row * cols_ + col;
    }
};

} // namespace yetty
