/*
 * ligature-cells.h - Shared programming-ligature span detection over grid cells
 *
 * The terminal renders a programming ligature by suppressing the covered cells
 * on the grid (which cannot shape) and re-drawing the shaped glyph through the
 * SDF free-position path against a ligature face (Fira Code). Two passes must
 * agree, without communicating, on exactly which cells a ligature covers:
 *
 *   - vterm_pack_line (vterm.c)     — sets the grid glyph to 0 for those cells.
 *   - shape_row_ligatures (sdf-layer.c) — shapes the span and draws the glyph.
 *
 * Both call yetty_yvterm_ligature_run_length() so the decision is made in one
 * place: it combines the HarfBuzz-free codepoint table
 * (yetty_yfont_ligature_length_at) with a shapeability check on the cells.
 * Only compiled meaningfully when the shaper is built in; without it the
 * length is never consulted (the grid keeps drawing the characters).
 */

#ifndef YETTY_YVTERM_LIGATURE_CELLS_H
#define YETTY_YVTERM_LIGATURE_CELLS_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/yfont/font.h>
#include <yetty/yvterm/grid.h>

/* Length in cells of the programming ligature beginning at cells[col], or 0 if
 * none. A span qualifies only when every covered cell is a plain, single-width,
 * unconcealed cell with no combining marks — the shaper is fed exactly those
 * codepoints and its one ligature glyph is drawn across exactly those cells. */
static inline size_t yetty_yvterm_ligature_run_length(const struct yetty_yvterm_text_cell *cells,
                                                      uint32_t cols, uint32_t col)
{
    uint32_t window[YETTY_YFONT_LIGATURE_MAX_LEN];
    size_t window_count = 0;
    for (uint32_t ahead = 0; ahead < YETTY_YFONT_LIGATURE_MAX_LEN && (col + ahead) < cols;
         ++ahead) {
        window[window_count++] = cells[col + ahead].codepoint;
    }

    size_t len = yetty_yfont_ligature_length_at(window, window_count, 0u);
    if (len < 2u) {
        return 0;
    }

    for (size_t offset = 0; offset < len; ++offset) {
        const struct yetty_yvterm_text_cell *cell = &cells[col + offset];
        if (cell->width != 1u || cell->mark_count != 0u ||
            (cell->attrs & YETTY_YVTERM_ATTR_CONCEAL)) {
            return 0;
        }
    }
    return len;
}

#endif /* YETTY_YVTERM_LIGATURE_CELLS_H */
