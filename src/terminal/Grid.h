#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

namespace yetty {

struct Cell {
    uint32_t codepoint = ' ';      // Unicode codepoint
    glm::vec4 fgColor = {1.0f, 1.0f, 1.0f, 1.0f};  // Foreground (text) color
    glm::vec4 bgColor = {0.5f, 0.0f, 0.0f, 1.0f};  // DEBUG: Bright red to see if backgrounds render
    bool bold = false;
    bool italic = false;
    bool underline = false;
};

class Grid {
public:
    Grid(uint32_t cols = 80, uint32_t rows = 24);

    void resize(uint32_t cols, uint32_t rows);
    void clear();

    void setCell(uint32_t col, uint32_t row, const Cell& cell);
    void setChar(uint32_t col, uint32_t row, uint32_t codepoint);
    void setFgColor(uint32_t col, uint32_t row, const glm::vec4& color);
    void setBgColor(uint32_t col, uint32_t row, const glm::vec4& color);

    const Cell& getCell(uint32_t col, uint32_t row) const;
    Cell& getCell(uint32_t col, uint32_t row);

    void writeString(uint32_t col, uint32_t row, const char* str,
                     const glm::vec4& fgColor = {1.0f, 1.0f, 1.0f, 1.0f});

    uint32_t getCols() const { return cols_; }
    uint32_t getRows() const { return rows_; }
    const std::vector<Cell>& getCells() const { return cells_; }

private:
    uint32_t cols_;
    uint32_t rows_;
    std::vector<Cell> cells_;
};

} // namespace yetty
