/*
 * yvterm terminal-state matrix — headless, no fork, no GPU (#418).
 *
 * Complements the terminal-core smoke test (terminal-test.c) with a wider
 * behavioural matrix, driven through the libvterm-backed grid model and
 * asserted on the observable cell / cursor / scrollback state. The focus is the
 * places where yvterm's own callbacks (cb_putglyph / cb_erase / cb_moverect /
 * cb_movecursor) translate libvterm's state into the rolling-row cell ring:
 * background-colour erase (BCE), wide-glyph spill, autowrap, tab stops, cursor
 * save/restore, in-line insert/delete, and scroll-region isolation vs
 * scrollback. It also covers the dual-buffer alternate-screen contract: entry
 * presents a cleared alternate ring, alt scrolls never touch the primary ring,
 * and exit switches back to the untouched primary — contents, scrollback
 * origin, and cursor (see test_alt_screen_restore).
 *
 * Every failure names the input escape stream and the expected terminal state.
 * Anything already covered by terminal-test.c (basic ingestion, CUP/CR/LF/CUU/
 * CUF, bold+underline+fg, scrollback origin, resize dims, DCS/OSC routing) is
 * intentionally not repeated here.
 */

#include <yetty/yvterm/grid.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static struct yetty_yclass_object *make_grid(struct ytest *test, uint32_t cols, uint32_t rows,
                                             uint32_t scrollback)
{
    struct yetty_yclass_object_ptr_result r = yetty_yvterm_grid_make(cols, rows, scrollback);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

static void feed(struct ytest *test, struct yetty_yclass_object *grid, const char *bytes,
                 size_t len)
{
    struct yetty_ycore_void_result r = yetty_yvterm_grid_feed(grid, bytes, len);
    YTEST_REQUIRE_OK(test, r);
}

/* Convenience for NUL-terminated escape/text literals. */
static void feeds(struct ytest *test, struct yetty_yclass_object *grid, const char *bytes)
{
    feed(test, grid, bytes, strlen(bytes));
}

static const struct yetty_yvterm_text_cell *cell_at(struct ytest *test,
                                                    struct yetty_yclass_object *grid, uint32_t row,
                                                    uint32_t col)
{
    struct yetty_yvterm_text_cell_const_ptr_result cells = yetty_yvterm_grid_line_cells(grid, row);
    YTEST_REQUIRE_OK(test, cells);
    YTEST_REQUIRE_NOT_NULL(test, cells.value);
    return &cells.value[col];
}

static uint32_t cp_at(struct ytest *test, struct yetty_yclass_object *grid, uint32_t row,
                      uint32_t col)
{
    return cell_at(test, grid, row, col)->codepoint;
}

static void cursor_of(struct ytest *test, struct yetty_yclass_object *grid, uint32_t *row,
                      uint32_t *col)
{
    uint32_t vis = 0;
    struct yetty_ycore_void_result cur = yetty_yvterm_grid_cursor(grid, row, col, &vis);
    YTEST_REQUIRE_OK(test, cur);
}

static uint32_t scroll_origin(struct ytest *test, struct yetty_yclass_object *grid)
{
    struct yetty_ycore_uint32_result r = yetty_yvterm_grid_scroll_origin(grid);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

/*---------------------------------------------------------------------------
 * Tab stops: HT advances to the next multiple-of-8 column by default.
 *-------------------------------------------------------------------------*/
static void test_tab_stops(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 0);
    uint32_t row, col;

    feeds(test, grid, "a\tb"); /* 'a' at col 0, HT → col 8, 'b' at col 8 */
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 0), 'a');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 8), 'b');
    cursor_of(test, grid, &row, &col);
    YTEST_CHECK_EQ_SIZE(test, col, 9);

    feeds(test, grid, "\t"); /* from col 9 → next stop col 16 */
    cursor_of(test, grid, &row, &col);
    YTEST_CHECK_EQ_SIZE(test, col, 16);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Autowrap: the 81st printable on an 80-col line wraps to the next row.
 *-------------------------------------------------------------------------*/
static void test_autowrap(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 10, 5, 0);
    uint32_t row, col;

    /* 10 'x' fill the row exactly; the 11th char ('Y') wraps to row 1. */
    feeds(test, grid, "xxxxxxxxxxY");
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 9), 'x');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 1, 0), 'Y');
    cursor_of(test, grid, &row, &col);
    YTEST_CHECK_EQ_SIZE(test, row, 1);
    YTEST_CHECK_EQ_SIZE(test, col, 1);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Wide (double-width) glyph: the head cell reports width 2 and the trailing
 * spill cell reports width 0, so the cursor advances by two columns.
 *-------------------------------------------------------------------------*/
static void test_wide_glyph(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 0);
    uint32_t row, col;

    /* U+4E00 (一), East-Asian wide, UTF-8 E4 B8 80. */
    feeds(test, grid,
          "\xe4\xb8\x80"
          "Z");
    const struct yetty_yvterm_text_cell *head = cell_at(test, grid, 0, 0);
    const struct yetty_yvterm_text_cell *spill = cell_at(test, grid, 0, 1);
    YTEST_CHECK_EQ_INT(test, head->codepoint, 0x4E00);
    YTEST_CHECK_EQ_INT(test, head->width, 2);
    YTEST_CHECK_EQ_INT(test, spill->width, 0);
    /* The next glyph lands past the spill, at col 2. */
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 2), 'Z');
    cursor_of(test, grid, &row, &col);
    YTEST_CHECK_EQ_SIZE(test, col, 3);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Cursor save/restore (DECSC / DECRC).
 *-------------------------------------------------------------------------*/
static void test_cursor_save_restore(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 0);
    uint32_t row, col;

    feeds(test, grid, "\033[5;10H"); /* CUP → (4,9) 0-based */
    feeds(test, grid, "\0337");      /* DECSC save */
    feeds(test, grid, "\033[1;1H");  /* CUP → home */
    cursor_of(test, grid, &row, &col);
    YTEST_CHECK_EQ_SIZE(test, row, 0);
    YTEST_CHECK_EQ_SIZE(test, col, 0);

    feeds(test, grid, "\0338"); /* DECRC restore */
    cursor_of(test, grid, &row, &col);
    YTEST_CHECK_EQ_SIZE(test, row, 4);
    YTEST_CHECK_EQ_SIZE(test, col, 9);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Erase-in-line: EL0 (cursor→EOL), EL1 (BOL→cursor), EL2 (whole line).
 *-------------------------------------------------------------------------*/
static void test_erase_in_line(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 0);

    /* EL0: erase from cursor to end of line. */
    feeds(test, grid, "ABCDEF\033[1;4H\033[K"); /* cursor at col 3 (D), erase D..EOL */
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 2), 'C');
    YTEST_CHECK(test, cp_at(test, grid, 0, 3) == 0 || cp_at(test, grid, 0, 3) == ' ');
    YTEST_CHECK(test, cp_at(test, grid, 0, 5) == 0 || cp_at(test, grid, 0, 5) == ' ');

    /* EL1: erase from BOL to cursor (inclusive). */
    feeds(test, grid, "\033[2;1HABCDEF\033[2;4H\033[1K");
    YTEST_CHECK(test, cp_at(test, grid, 1, 0) == 0 || cp_at(test, grid, 1, 0) == ' ');
    YTEST_CHECK(test, cp_at(test, grid, 1, 3) == 0 || cp_at(test, grid, 1, 3) == ' ');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 1, 4), 'E');

    /* EL2: erase whole line regardless of cursor. */
    feeds(test, grid, "\033[3;1HABCDEF\033[3;4H\033[2K");
    YTEST_CHECK(test, cp_at(test, grid, 2, 0) == 0 || cp_at(test, grid, 2, 0) == ' ');
    YTEST_CHECK(test, cp_at(test, grid, 2, 5) == 0 || cp_at(test, grid, 2, 5) == ' ');

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Background-colour erase (BCE): cells cleared while a non-default background
 * is set take that background colour, not the default.
 *-------------------------------------------------------------------------*/
static void test_bce(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 0);

    uint32_t default_bg = cell_at(test, grid, 0, 0)->bg;

    /* Capture the concrete packed colour a red background produces by writing a
     * glyph with it — this is exactly what BCE must fill erased cells with. */
    feeds(test, grid, "\033[41mX\033[0m");
    uint32_t red_bg = cell_at(test, grid, 0, 0)->bg;
    YTEST_CHECK(test, red_bg != default_bg);

    /* Set red background, clear the whole (second) line, then reset. The erased
     * cells must carry the SAME red background as the written glyph, not the
     * default. */
    feeds(test, grid, "\033[2;1H\033[41m\033[2K\033[0m");
    YTEST_CHECK_EQ_SIZE(test, cell_at(test, grid, 1, 5)->bg, red_bg);

    /* A fresh line cleared with the default background is unaffected. */
    feeds(test, grid, "\033[3;1H\033[2K");
    YTEST_CHECK_EQ_SIZE(test, cell_at(test, grid, 2, 5)->bg, default_bg);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Insert / delete character: ICH shifts the tail right; DCH pulls it left.
 *-------------------------------------------------------------------------*/
static void test_insert_delete_char(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 0);

    /* ICH: "ABCDEF", home, insert 2 blanks → "  ABCDEF". */
    feeds(test, grid, "ABCDEF\033[1;1H\033[2@");
    YTEST_CHECK(test, cp_at(test, grid, 0, 0) == 0 || cp_at(test, grid, 0, 0) == ' ');
    YTEST_CHECK(test, cp_at(test, grid, 0, 1) == 0 || cp_at(test, grid, 0, 1) == ' ');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 2), 'A');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 3), 'B');

    /* DCH: on a fresh line "ABCDEF", home, delete 2 → "CDEF". */
    feeds(test, grid, "\033[2;1HABCDEF\033[2;1H\033[2P");
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 1, 0), 'C');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 1, 1), 'D');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 1, 2), 'E');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 1, 3), 'F');

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Scroll region (DECSTBM): a scroll inside a top/bottom margin moves only the
 * rows within the region and leaves rows outside it untouched — and, crucially,
 * does NOT push anything into scrollback (only a full-screen scroll does).
 *-------------------------------------------------------------------------*/
static void test_scroll_region(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 6, 100);

    /* Fill six rows R0..R5 without a trailing newline (no scroll yet). */
    feeds(test, grid, "R0\r\nR1\r\nR2\r\nR3\r\nR4\r\nR5");
    YTEST_REQUIRE_EQ_SIZE(test, scroll_origin(test, grid), 0);

    /* Region = rows 2..4 (1-based) → 0-based rows 1..3. Put the cursor on the
     * bottom margin (row 4, 1-based) and emit LF: the region scrolls up by one,
     * so 0-based row1←"R2", row2←"R3", row3←blank; rows 0/4/5 stay. */
    feeds(test, grid, "\033[2;4r\033[4;1H\n");

    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 1), '0'); /* R0 outside region */
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 1, 1), '2'); /* R2 shifted up */
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 2, 1), '3'); /* R3 shifted up */
    YTEST_CHECK(test, cp_at(test, grid, 3, 0) == 0 || cp_at(test, grid, 3, 0) == ' ');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 4, 1), '4'); /* R4 outside region */
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 5, 1), '5'); /* R5 outside region */

    /* A region scroll must not have grown the scrollback. */
    YTEST_CHECK_EQ_SIZE(test, scroll_origin(test, grid), 0);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Extended SGR beyond bold/underline/fg (which terminal-test.c already covers):
 * reverse, italic, strike, indexed 256-colour fg, and 24-bit truecolour fg.
 *-------------------------------------------------------------------------*/
static void test_sgr_extended(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 0);

    /* Reverse video: yvterm represents it by swapping fg/bg on the cell (there
     * is no separate reverse attr bit set on cells), so a reversed cell's fg/bg
     * are a plain cell's bg/fg. */
    feeds(test, grid, "N\033[7mR\033[0m");
    uint32_t normal_fg = cell_at(test, grid, 0, 0)->fg;
    uint32_t normal_bg = cell_at(test, grid, 0, 0)->bg;
    uint32_t rev_fg = cell_at(test, grid, 0, 1)->fg;
    uint32_t rev_bg = cell_at(test, grid, 0, 1)->bg;
    YTEST_CHECK_EQ_SIZE(test, rev_fg, normal_bg);
    YTEST_CHECK_EQ_SIZE(test, rev_bg, normal_fg);

    /* Italic and strike are real attr bits. */
    feeds(test, grid, "\033[2;1H\033[3mI\033[0m\033[9mS\033[0m");
    YTEST_CHECK(test, cell_at(test, grid, 1, 0)->attrs & YETTY_YVTERM_ATTR_ITALIC);
    YTEST_CHECK(test, cell_at(test, grid, 1, 1)->attrs & YETTY_YVTERM_ATTR_STRIKE);

    /* Indexed 256-colour fg and 24-bit truecolour fg differ from default and
     * from each other. */
    feeds(test, grid, "\033[3;1H\033[38;5;208mP\033[0m\033[38;2;10;20;30mQ\033[0m");
    uint32_t idx_fg = cell_at(test, grid, 2, 0)->fg;
    uint32_t true_fg = cell_at(test, grid, 2, 1)->fg;
    YTEST_CHECK(test, idx_fg != normal_fg);
    YTEST_CHECK(test, true_fg != normal_fg);
    YTEST_CHECK(test, idx_fg != true_fg);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Alt-screen dual buffers (?1049h / ?1049l): the primary and alternate screens
 * are separate rings. Entering the alternate screen presents a cleared surface
 * and writes land on it; the alternate ring has no scrollback and its OWN
 * rolling origin (fresh per entry); exiting switches back to the untouched
 * primary — contents, scrollback origin, and (for mode 1049) the saved cursor.
 *-------------------------------------------------------------------------*/
static void test_alt_screen_restore(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 100);
    uint32_t row, col;

    /* Scroll real primary history first: 30 lines on a 24-row screen push
     * several rows into scrollback. */
    for (int line = 0; line < 30; ++line) {
        feeds(test, grid, "history\r\n");
    }
    uint32_t origin_before = scroll_origin(test, grid);
    YTEST_REQUIRE(test, origin_before > 0);

    feeds(test, grid, "\033[1;1HPRIM"); /* marker on the visible top row */
    YTEST_REQUIRE_EQ_INT(test, cp_at(test, grid, 0, 0), 'P');

    feeds(test, grid, "\033[?1049h"); /* enter alt: a cleared surface */
    YTEST_CHECK(test, cp_at(test, grid, 0, 0) == 0 || cp_at(test, grid, 0, 0) == ' ');
    /* The alternate screen's rolling origin is its own and starts fresh. */
    YTEST_CHECK_EQ_SIZE(test, scroll_origin(test, grid), 0);

    feeds(test, grid, "\033[1;1HALT"); /* writes land on the alt surface */
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 0), 'A');

    /* A whole-screen scroll inside the alt screen advances only the ALT
     * origin (in-place ring rotation, nothing retained). */
    feeds(test, grid, "\033[24;1H\n");
    YTEST_CHECK_EQ_SIZE(test, scroll_origin(test, grid), 1);

    feeds(test, grid, "\033[?1049l"); /* exit alt: back to the primary ring */
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 0), 'P');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 3), 'M');
    /* Primary scrollback survived the alt session untouched. */
    YTEST_CHECK_EQ_SIZE(test, scroll_origin(test, grid), origin_before);
    /* Mode 1049 also saves/restores the cursor: back to just after "PRIM". */
    cursor_of(test, grid, &row, &col);
    YTEST_CHECK_EQ_SIZE(test, row, 0);
    YTEST_CHECK_EQ_SIZE(test, col, 4);

    yetty_yvterm_grid_dispose(grid);
}

int main(void)
{
    struct ytest test = ytest_begin("yvterm_state_matrix");
    YTEST_RUN(&test, test_tab_stops);
    YTEST_RUN(&test, test_autowrap);
    YTEST_RUN(&test, test_wide_glyph);
    YTEST_RUN(&test, test_cursor_save_restore);
    YTEST_RUN(&test, test_erase_in_line);
    YTEST_RUN(&test, test_bce);
    YTEST_RUN(&test, test_insert_delete_char);
    YTEST_RUN(&test, test_scroll_region);
    YTEST_RUN(&test, test_sgr_extended);
    YTEST_RUN(&test, test_alt_screen_restore);
    return ytest_end(&test);
}
