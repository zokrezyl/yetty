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

#include <yetty/api/yvterm/grid.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static struct yetty_yclass_object *make_grid(struct ytest *test, uint32_t cols, uint32_t rows,
                                             uint32_t scrollback)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_yvterm_grid_make(cols, rows, scrollback, /*hot_rows=*/0);
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
    struct yetty_ycore_uint64_result r = yetty_yvterm_grid_scroll_origin(grid);
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
 * Post-Unicode-5.0 width coverage (#569). The width tables (fullwidth.inc /
 * combining.inc) are regenerated from one pinned modern Unicode version, so
 * codepoints introduced after the frozen 2007 (Unicode 5.0) table must be
 * classified correctly now: recent wide glyphs occupy two columns, recent
 * combining marks occupy none. Under the stale table these post-5.0
 * codepoints were all mis-measured as plain width-1 cells.
 *-------------------------------------------------------------------------*/
static void test_modern_width_tables(struct ytest *test)
{
    /* Wide glyphs first seen in Unicode 12 / 14 / 15 / 16 — head cell width
     * 2, zero-width spill, cursor advances two columns. */
    static const struct {
        const char *utf8;
        uint32_t codepoint;
    } wides[] = {
        {"\xf0\x9f\x9b\x95", 0x1F6D5}, /* HINDU TEMPLE   — Unicode 12.0 */
        {"\xf0\x9f\xab\xa0", 0x1FAE0}, /* MELTING FACE   — Unicode 14.0 */
        {"\xf0\x9f\x9b\x9c", 0x1F6DC}, /* WIRELESS       — Unicode 15.0 */
        {"\xf0\x9f\xab\x9f", 0x1FADF}, /* SPLATTER       — Unicode 16.0 */
    };
    for (size_t i = 0; i < sizeof(wides) / sizeof(wides[0]); i++) {
        struct yetty_yclass_object *grid = make_grid(test, 80, 24, 0);
        uint32_t row, col;
        feeds(test, grid, wides[i].utf8);
        const struct yetty_yvterm_text_cell *head = cell_at(test, grid, 0, 0);
        const struct yetty_yvterm_text_cell *spill = cell_at(test, grid, 0, 1);
        YTEST_CHECK_EQ_INT(test, head->codepoint, wides[i].codepoint);
        YTEST_CHECK_EQ_INT(test, head->width, 2);
        YTEST_CHECK_EQ_INT(test, spill->width, 0);
        cursor_of(test, grid, &row, &col);
        YTEST_CHECK_EQ_SIZE(test, col, 2);
        yetty_yvterm_grid_dispose(grid);
    }

    /* Combining marks first seen after Unicode 5.0 must be zero-width: fed
     * after a base glyph they attach to it and leave the cursor on the base's
     * single column. A stale table would type these as spacing and push the
     * cursor to column 2. */
    static const char *const combining_after_base[] = {
        "x\xf0\x9e\xa5\x8a", /* U+1E94A ADLAM NUKTA          — Unicode 9.0  */
        "x\xf0\x91\xbc\x80", /* U+11F00 KAWI SIGN CANDRABINDU — Unicode 15.0 */
    };
    for (size_t i = 0; i < sizeof(combining_after_base) / sizeof(combining_after_base[0]); i++) {
        struct yetty_yclass_object *grid = make_grid(test, 80, 24, 0);
        uint32_t row, col;
        feeds(test, grid, combining_after_base[i]);
        YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 0), 'x');
        cursor_of(test, grid, &row, &col);
        YTEST_CHECK_EQ_SIZE(test, col, 1);
        yetty_yvterm_grid_dispose(grid);
    }
}

/*---------------------------------------------------------------------------
 * Grapheme-cluster cell model (#570): libvterm hands the whole cluster (base
 * + combining marks / variation selectors) to cb_putglyph as chars[]; the grid
 * must keep every codepoint, not just the base. The base stays in `codepoint`,
 * the continuation in marks[0..mark_count).
 *-------------------------------------------------------------------------*/
static void test_grapheme_cluster(struct ytest *test)
{
    /* Base 'e' + U+0301 COMBINING ACUTE ACCENT: one cell, one mark, cursor
     * advances by the base's single column. */
    {
        struct yetty_yclass_object *grid = make_grid(test, 80, 24, 0);
        uint32_t row, col;
        feeds(test, grid, "e\xcc\x81");
        const struct yetty_yvterm_text_cell *cell = cell_at(test, grid, 0, 0);
        YTEST_CHECK_EQ_INT(test, cell->codepoint, 'e');
        YTEST_CHECK_EQ_INT(test, cell->mark_count, 1);
        YTEST_CHECK_EQ_INT(test, cell->marks[0], 0x0301);
        cursor_of(test, grid, &row, &col);
        YTEST_CHECK_EQ_SIZE(test, col, 1);
        yetty_yvterm_grid_dispose(grid);
    }

    /* Base U+2764 HEAVY BLACK HEART + U+FE0F VARIATION SELECTOR-16: the VS is
     * a zero-width continuation, so both codepoints live in one cell. */
    {
        struct yetty_yclass_object *grid = make_grid(test, 80, 24, 0);
        feeds(test, grid, "\xe2\x9d\xa4\xef\xb8\x8f");
        const struct yetty_yvterm_text_cell *cell = cell_at(test, grid, 0, 0);
        YTEST_CHECK_EQ_INT(test, cell->codepoint, 0x2764);
        YTEST_CHECK_EQ_INT(test, cell->mark_count, 1);
        YTEST_CHECK_EQ_INT(test, cell->marks[0], 0xFE0F);
        yetty_yvterm_grid_dispose(grid);
    }

    /* Two stacked combining marks: base 'a' + U+0301 + U+0323 (dot below).
     * Both continuation codepoints are retained in order. */
    {
        struct yetty_yclass_object *grid = make_grid(test, 80, 24, 0);
        feeds(test, grid, "a\xcc\x81\xcc\xa3");
        const struct yetty_yvterm_text_cell *cell = cell_at(test, grid, 0, 0);
        YTEST_CHECK_EQ_INT(test, cell->codepoint, 'a');
        YTEST_CHECK_EQ_INT(test, cell->mark_count, 2);
        YTEST_CHECK_EQ_INT(test, cell->marks[0], 0x0301);
        YTEST_CHECK_EQ_INT(test, cell->marks[1], 0x0323);
        yetty_yvterm_grid_dispose(grid);
    }

    /* Overwriting a cluster cell with a plain glyph drops the stale marks. */
    {
        struct yetty_yclass_object *grid = make_grid(test, 80, 24, 0);
        feeds(test, grid, "a\xcc\x81");
        feeds(test, grid, "\rz"); /* CR home, overwrite col 0 with 'z' */
        const struct yetty_yvterm_text_cell *cell = cell_at(test, grid, 0, 0);
        YTEST_CHECK_EQ_INT(test, cell->codepoint, 'z');
        YTEST_CHECK_EQ_INT(test, cell->mark_count, 0);
        yetty_yvterm_grid_dispose(grid);
    }
}

/* Feed one UTF-8 cluster onto a fresh grid and return the column the cursor
 * advanced to (i.e. the cluster's display width from col 0). */
static uint32_t advance_of(struct ytest *test, const char *utf8)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 4, 0);
    feeds(test, grid, utf8);
    uint32_t row, col;
    cursor_of(test, grid, &row, &col);
    yetty_yvterm_grid_dispose(grid);
    return col;
}

/*---------------------------------------------------------------------------
 * Complex-script (Indic) cluster width: a base consonant plus a spacing
 * vowel sign (category Mc), or a virama-linked consonant conjunct, is one
 * grapheme cluster whose width caps at 2 cells — matching what the wcwidth
 * reference (the ucs-detect measure) reports. Without the cluster rules the
 * grid would count each consonant and each spacing mark as its own column
 * and overshoot (3, 4, …). A dangling virama with no following consonant,
 * and a virama+ZWJ explicit half-form request, both stay width 1.
 *-------------------------------------------------------------------------*/
static void test_indic_cluster_width(struct ytest *test)
{
    /* base + spacing mark (Mc) → 2 */
    YTEST_CHECK_EQ_SIZE(test, advance_of(test, "\xe0\xa4\x95\xe0\xa4\xbf"), 2); /* कि */
    YTEST_CHECK_EQ_SIZE(test, advance_of(test, "\xe0\xae\x95\xe0\xae\xbf"), 2); /* Tamil கி */
    /* base + spacing mark + anusvara (Bengali কিং) → 2 */
    YTEST_CHECK_EQ_SIZE(test, advance_of(test, "\xe0\xa6\x95\xe0\xa6\xbf\xe0\xa6\x82"), 2);
    /* virama conjunct KA+vir+SSA (क्ष) → 2 */
    YTEST_CHECK_EQ_SIZE(test, advance_of(test, "\xe0\xa4\x95\xe0\xa5\x8d\xe0\xa4\xb7"), 2);
    /* conjunct + trailing matra KA+vir+TA+vowelI (ক্তি) → 2 */
    YTEST_CHECK_EQ_SIZE(test, advance_of(test, "\xe0\xa6\x95\xe0\xa7\x8d\xe0\xa6\xa4\xe0\xa6\xbf"),
                        2);
    /* triple conjunct KA+vir+SSA+vir+MA (क्ष्म) — still capped at 2 */
    YTEST_CHECK_EQ_SIZE(
        test, advance_of(test, "\xe0\xa4\x95\xe0\xa5\x8d\xe0\xa4\xb7\xe0\xa5\x8d\xe0\xa4\xae"), 2);
    /* dangling virama KA+vir (क्) — no consonant follows → width 1 */
    YTEST_CHECK_EQ_SIZE(test, advance_of(test, "\xe0\xa4\x95\xe0\xa5\x8d"), 1);
    /* explicit half-form KA+vir+ZWJ (क्‍) — ZWJ transparent to width → 1 */
    YTEST_CHECK_EQ_SIZE(test, advance_of(test, "\xe0\xa4\x95\xe0\xa5\x8d\xe2\x80\x8d"), 1);
}

/*---------------------------------------------------------------------------
 * Emoji sequence width semantics (#571): VS16/VS15, ZWJ, skin tones, flags.
 * Each sequence must advance the cursor by exactly what wcwidth reports for
 * the pinned Unicode version — the quantity ucs-detect measures via CPR.
 * Codepoints are spelled as raw UTF-8 \x escapes so the invisible joiners /
 * selectors are unambiguous in the source.
 *-------------------------------------------------------------------------*/
static void test_emoji_sequence_widths(struct ytest *test)
{
    /* Regional-indicator flag pair (U+1F1FA U+1F1F8 = 🇺🇸) → one width-2 cluster. */
    YTEST_CHECK_EQ_SIZE(test, advance_of(test, "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8"), 2);

    /* Two flags in a row (US then JP) → two clusters, four columns — the second
     * regional-indicator pair must not merge with the first. */
    YTEST_CHECK_EQ_SIZE(
        test, advance_of(test, "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5"),
        4);

    /* ZWJ family (man ZWJ woman ZWJ girl = 👨‍👩‍👧, 5 codepoints) → width 2. */
    YTEST_CHECK_EQ_SIZE(
        test,
        advance_of(test,
                   "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7"),
        2);

    /* man ZWJ rocket (👨‍🚀) → width 2. */
    YTEST_CHECK_EQ_SIZE(test, advance_of(test, "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x9A\x80"), 2);

    /* Umbrella + VS16 (☂️) → narrow base promoted to width 2. */
    YTEST_CHECK_EQ_SIZE(test, advance_of(test, "\xE2\x98\x82\xEF\xB8\x8F"), 2);

    /* Umbrella + VS15 (☂︎) → text presentation, width 1. */
    YTEST_CHECK_EQ_SIZE(test, advance_of(test, "\xE2\x98\x82\xEF\xB8\x8E"), 1);

    /* Woman + skin-tone modifier (👩🏽) → absorbed into base, width 2 (not 4). */
    YTEST_CHECK_EQ_SIZE(test, advance_of(test, "\xF0\x9F\x91\xA9\xF0\x9F\x8F\xBD"), 2);

    /* Degenerate: lone regional indicator keeps width 2; plain wide emoji is 2. */
    YTEST_CHECK_EQ_SIZE(test, advance_of(test, "\xF0\x9F\x87\xA6"), 2);
    YTEST_CHECK_EQ_SIZE(test, advance_of(test, "\xF0\x9F\x91\xA8"), 2);

    /* The head cell of a ZWJ family reports width 2 with a width-0 spill. */
    {
        struct yetty_yclass_object *grid = make_grid(test, 80, 4, 0);
        feeds(test, grid,
              "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7");
        YTEST_CHECK_EQ_INT(test, cell_at(test, grid, 0, 0)->width, 2);
        YTEST_CHECK_EQ_INT(test, cell_at(test, grid, 0, 1)->width, 0);
        yetty_yvterm_grid_dispose(grid);
    }
}

/*---------------------------------------------------------------------------
 * Mode 2027 grapheme clustering (#572): with the mode set (the default) a ZWJ
 * family advances as one width-2 cluster; with it reset the cursor falls back
 * to legacy per-codepoint advance. The observable difference is the cursor
 * arithmetic — the DECRQM report itself is exercised by the ucs-detect harness.
 *-------------------------------------------------------------------------*/
static void test_mode_2027_clustering(struct ytest *test)
{
    /* man ZWJ woman ZWJ girl (👨‍👩‍👧, 5 codepoints). */
    const char *family = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
    uint32_t row, col;

    /* Default: mode 2027 on → one width-2 cluster. */
    {
        struct yetty_yclass_object *grid = make_grid(test, 80, 4, 0);
        feeds(test, grid, family);
        cursor_of(test, grid, &row, &col);
        YTEST_CHECK_EQ_SIZE(test, col, 2);
        yetty_yvterm_grid_dispose(grid);
    }

    /* CSI ? 2027 l → clustering off: each wide element and the width-0 joiners
     * advance independently (2 + 0 + 2 + 0 + 2 = 6). */
    {
        struct yetty_yclass_object *grid = make_grid(test, 80, 4, 0);
        feeds(test, grid, "\x1b[?2027l");
        feeds(test, grid, family);
        cursor_of(test, grid, &row, &col);
        YTEST_CHECK_EQ_SIZE(test, col, 6);
        yetty_yvterm_grid_dispose(grid);
    }

    /* CSI ? 2027 h re-enables clustering. */
    {
        struct yetty_yclass_object *grid = make_grid(test, 80, 4, 0);
        feeds(test, grid, "\x1b[?2027l\x1b[?2027h");
        feeds(test, grid, family);
        cursor_of(test, grid, &row, &col);
        YTEST_CHECK_EQ_SIZE(test, col, 2);
        yetty_yvterm_grid_dispose(grid);
    }
}

/*---------------------------------------------------------------------------
 * Kitty keyboard protocol (CSI ? u query / > push / < pop / = set). The query
 * reply is written back through the grid's pty-write hook, so a small sink
 * captures the emitted bytes for comparison.
 *-------------------------------------------------------------------------*/
struct kitty_reply_capture {
    char buf[64];
    size_t len;
};

static struct yetty_ycore_void_result kitty_reply_sink(const char *bytes, size_t len,
                                                       void *userdata)
{
    struct kitty_reply_capture *capture = userdata;
    for (size_t index = 0; index < len && capture->len < sizeof(capture->buf) - 1; index++) {
        capture->buf[capture->len++] = bytes[index];
    }
    capture->buf[capture->len] = 0;
    return YETTY_OK_VOID();
}

static void test_kitty_keyboard(struct ytest *test)
{
    struct kitty_reply_capture capture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 80, 4, 0);
    struct yetty_ycore_void_result set =
        yetty_yvterm_grid_set_pty_write(grid, kitty_reply_sink, &capture);
    YTEST_REQUIRE_OK(test, set);

    /* Empty stack → query (CSI ? u) reports flags 0. */
    feeds(test, grid, "\x1b[?u");
    YTEST_CHECK_STR_EQ(test, capture.buf, "\x1b[?0u");

    /* Push disambiguate (flag 1) via CSI > 1 u; query now reports 1. */
    capture.len = 0;
    capture.buf[0] = 0;
    feeds(test, grid, "\x1b[>1u\x1b[?u");
    YTEST_CHECK_STR_EQ(test, capture.buf, "\x1b[?1u");

    /* CSI = 2 ; 2 u — mode 2 sets (ORs in) report-events bit: 1|2 = 3. */
    capture.len = 0;
    capture.buf[0] = 0;
    feeds(test, grid, "\x1b[=2;2u\x1b[?u");
    YTEST_CHECK_STR_EQ(test, capture.buf, "\x1b[?3u");

    /* CSI = 1 ; 3 u — mode 3 clears the disambiguate bit: 3 & ~1 = 2. */
    capture.len = 0;
    capture.buf[0] = 0;
    feeds(test, grid, "\x1b[=1;3u\x1b[?u");
    YTEST_CHECK_STR_EQ(test, capture.buf, "\x1b[?2u");

    /* CSI < u pops the pushed entry → empty stack → flags 0. */
    capture.len = 0;
    capture.buf[0] = 0;
    feeds(test, grid, "\x1b[<u\x1b[?u");
    YTEST_CHECK_STR_EQ(test, capture.buf, "\x1b[?0u");

    /* CSI = 5 u — mode 1 (default) replaces the whole set; on an empty stack it
     * seeds a base entry. 5 = disambiguate | report-alternates. */
    capture.len = 0;
    capture.buf[0] = 0;
    feeds(test, grid, "\x1b[=5u\x1b[?u");
    YTEST_CHECK_STR_EQ(test, capture.buf, "\x1b[?5u");

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * OSC 52 clipboard write. libvterm base64-decodes the payload and hands the
 * plain text to the grid's clipboard-write hook; a mock sink captures it.
 *-------------------------------------------------------------------------*/
struct osc52_capture {
    char buf[64];
    size_t len;
    int clipboard;
    int calls;
};

static struct yetty_ycore_void_result osc52_sink(const char *text, size_t len, int clipboard,
                                                 void *userdata)
{
    struct osc52_capture *capture = userdata;
    capture->calls++;
    capture->clipboard = clipboard;
    capture->len = len < sizeof(capture->buf) - 1 ? len : sizeof(capture->buf) - 1;
    memcpy(capture->buf, text, capture->len);
    capture->buf[capture->len] = 0;
    return YETTY_OK_VOID();
}

static void test_osc52_clipboard(struct ytest *test)
{
    struct osc52_capture capture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 80, 4, 0);
    struct yetty_ycore_void_result set =
        yetty_yvterm_grid_set_clipboard_write(grid, osc52_sink, &capture);
    YTEST_REQUIRE_OK(test, set);

    /* OSC 52 ; c ; base64("hello") ST → "hello" to the system clipboard. */
    feeds(test, grid, "\x1b]52;c;aGVsbG8=\x07");
    YTEST_CHECK_EQ_INT(test, capture.calls, 1);
    YTEST_CHECK_STR_EQ(test, capture.buf, "hello");
    YTEST_CHECK_EQ_INT(test, capture.clipboard, 1);

    /* Primary-selection target 'p' → same text, clipboard flag 0. */
    capture = (struct osc52_capture){0};
    feeds(test, grid, "\x1b]52;p;aGVsbG8=\x07");
    YTEST_CHECK_EQ_INT(test, capture.calls, 1);
    YTEST_CHECK_STR_EQ(test, capture.buf, "hello");
    YTEST_CHECK_EQ_INT(test, capture.clipboard, 0);

    /* Invalid base64 → no clipboard write (rejected safely, nothing lands). */
    capture = (struct osc52_capture){0};
    feeds(test, grid, "\x1b]52;c;@@@bad@@@\x07");
    YTEST_CHECK_EQ_INT(test, capture.calls, 0);

    /* Read request (OSC 52 ; c ; ?) is not wired → no callback, no leak. */
    capture = (struct osc52_capture){0};
    feeds(test, grid, "\x1b]52;c;?\x07");
    YTEST_CHECK_EQ_INT(test, capture.calls, 0);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * OSC 10/11 dynamic default colors: query reply, #/rgb: set forms, and the
 * OSC 110/111 reset. Query replies come back through the grid's pty-write hook.
 *-------------------------------------------------------------------------*/
static void test_osc_dynamic_colors(struct ytest *test)
{
    struct kitty_reply_capture capture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 80, 4, 0);
    struct yetty_ycore_void_result set =
        yetty_yvterm_grid_set_pty_write(grid, kitty_reply_sink, &capture);
    YTEST_REQUIRE_OK(test, set);

    /* Query the configured default background; remember the reply for the
     * reset check below. Reply shape: OSC 11 ; rgb:RRRR/GGGG/BBBB ST. */
    feeds(test, grid, "\x1b]11;?\x07");
    YTEST_CHECK(test, strncmp(capture.buf, "\x1b]11;rgb:", 9) == 0);
    char configured_bg[48];
    strncpy(configured_bg, capture.buf, sizeof(configured_bg) - 1);
    configured_bg[sizeof(configured_bg) - 1] = 0;

    /* Set background via #rrggbb; query echoes it, 8-bit scaled to 16-bit. */
    capture.len = 0;
    capture.buf[0] = 0;
    feeds(test, grid, "\x1b]11;#1e1e2e\x07");
    feeds(test, grid, "\x1b]11;?\x07");
    YTEST_CHECK_STR_EQ(test, capture.buf, "\x1b]11;rgb:1e1e/1e1e/2e2e\x1b\\");

    /* Set foreground via rgb:rr/gg/bb; query OSC 10 echoes it. */
    capture.len = 0;
    capture.buf[0] = 0;
    feeds(test, grid, "\x1b]10;rgb:ff/80/00\x07");
    feeds(test, grid, "\x1b]10;?\x07");
    YTEST_CHECK_STR_EQ(test, capture.buf, "\x1b]10;rgb:ffff/8080/0000\x1b\\");

    /* OSC 111 resets background to the configured value. */
    capture.len = 0;
    capture.buf[0] = 0;
    feeds(test, grid, "\x1b]111\x07");
    feeds(test, grid, "\x1b]11;?\x07");
    YTEST_CHECK_STR_EQ(test, capture.buf, configured_bg);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * DEC mode 2048 in-band window resize. Enabling reports the current size
 * immediately; a pixel-size change while enabled emits a fresh notification;
 * DECRQM reports the mode. Notifications arrive through the pty-write hook.
 *-------------------------------------------------------------------------*/
static void test_mode_2048_inband_resize(struct ytest *test)
{
    struct kitty_reply_capture capture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 0);
    struct yetty_ycore_void_result set =
        yetty_yvterm_grid_set_pty_write(grid, kitty_reply_sink, &capture);
    YTEST_REQUIRE_OK(test, set);

    /* Feed the pixel size before enabling; the mode is off so nothing emits. */
    struct yetty_ycore_void_result px = yetty_yvterm_grid_set_pixel_size(grid, 800, 480);
    YTEST_REQUIRE_OK(test, px);
    YTEST_CHECK_EQ_INT(test, (int)capture.len, 0);

    /* Enable → immediate CSI 48 ; rows ; cols ; height_px ; width_px t. */
    feeds(test, grid, "\x1b[?2048h");
    YTEST_CHECK_STR_EQ(test, capture.buf, "\x1b[48;24;80;480;800t");

    /* DECRQM → set. */
    capture.len = 0;
    capture.buf[0] = 0;
    feeds(test, grid, "\x1b[?2048$p");
    YTEST_CHECK_STR_EQ(test, capture.buf, "\x1b[?2048;1$y");

    /* A pixel-size change emits a fresh notification (cols/rows unchanged). */
    capture.len = 0;
    capture.buf[0] = 0;
    px = yetty_yvterm_grid_set_pixel_size(grid, 1000, 600);
    YTEST_REQUIRE_OK(test, px);
    YTEST_CHECK_STR_EQ(test, capture.buf, "\x1b[48;24;80;600;1000t");

    /* The same size again is a no-op — no duplicate notification. */
    capture.len = 0;
    capture.buf[0] = 0;
    px = yetty_yvterm_grid_set_pixel_size(grid, 1000, 600);
    YTEST_REQUIRE_OK(test, px);
    YTEST_CHECK_EQ_INT(test, (int)capture.len, 0);

    /* Disable → later size changes are silent. */
    feeds(test, grid, "\x1b[?2048l");
    capture.len = 0;
    capture.buf[0] = 0;
    px = yetty_yvterm_grid_set_pixel_size(grid, 1200, 700);
    YTEST_REQUIRE_OK(test, px);
    YTEST_CHECK_EQ_INT(test, (int)capture.len, 0);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * DEC mode 2031 color-scheme notifications. DSR 996 reports the current
 * light/dark scheme (CSI ? 997 ; N n); while enabled, a default-background
 * change flips the scheme and emits the notification; DECRQM reports the mode.
 *-------------------------------------------------------------------------*/
static void test_mode_2031_color_scheme(struct ytest *test)
{
    struct kitty_reply_capture capture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 0);
    struct yetty_ycore_void_result set =
        yetty_yvterm_grid_set_pty_write(grid, kitty_reply_sink, &capture);
    YTEST_REQUIRE_OK(test, set);

    /* Enable, then pin a dark background so the scheme is deterministic. */
    feeds(test, grid, "\x1b[?2031h");
    feeds(test, grid, "\x1b]11;#000000\x07");

    /* DSR 996 → color-scheme report. Black background → dark (1). */
    capture.len = 0;
    capture.buf[0] = 0;
    feeds(test, grid, "\x1b[?996n");
    YTEST_CHECK_STR_EQ(test, capture.buf, "\x1b[?997;1n");

    /* DECRQM → set. */
    capture.len = 0;
    capture.buf[0] = 0;
    feeds(test, grid, "\x1b[?2031$p");
    YTEST_CHECK_STR_EQ(test, capture.buf, "\x1b[?2031;1$y");

    /* A background change to white flips the scheme and notifies: light (2). */
    capture.len = 0;
    capture.buf[0] = 0;
    feeds(test, grid, "\x1b]11;#ffffff\x07");
    YTEST_CHECK_STR_EQ(test, capture.buf, "\x1b[?997;2n");

    /* Setting the same background again does not re-notify. */
    capture.len = 0;
    capture.buf[0] = 0;
    feeds(test, grid, "\x1b]11;#ffffff\x07");
    YTEST_CHECK_EQ_INT(test, (int)capture.len, 0);

    /* Back to black flips it to dark again. */
    capture.len = 0;
    capture.buf[0] = 0;
    feeds(test, grid, "\x1b]11;#000000\x07");
    YTEST_CHECK_STR_EQ(test, capture.buf, "\x1b[?997;1n");

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
    YTEST_RUN(&test, test_modern_width_tables);
    YTEST_RUN(&test, test_grapheme_cluster);
    YTEST_RUN(&test, test_indic_cluster_width);
    YTEST_RUN(&test, test_emoji_sequence_widths);
    YTEST_RUN(&test, test_mode_2027_clustering);
    YTEST_RUN(&test, test_kitty_keyboard);
    YTEST_RUN(&test, test_osc52_clipboard);
    YTEST_RUN(&test, test_osc_dynamic_colors);
    YTEST_RUN(&test, test_mode_2048_inband_resize);
    YTEST_RUN(&test, test_mode_2031_color_scheme);
    YTEST_RUN(&test, test_cursor_save_restore);
    YTEST_RUN(&test, test_erase_in_line);
    YTEST_RUN(&test, test_bce);
    YTEST_RUN(&test, test_insert_delete_char);
    YTEST_RUN(&test, test_scroll_region);
    YTEST_RUN(&test, test_sgr_extended);
    YTEST_RUN(&test, test_alt_screen_restore);
    return ytest_end(&test);
}
