/*
 * vtermgrid-store contract test (#699.5 clusters, retention stage) — headless,
 * no GPU, no libvterm. Pins every cutover-critical semantic of the
 * cluster-preserving store BEFORE the vtermgrid state-callback swap: mark
 * retention, wide head/continuation shape, pen application, BCE erase,
 * SU/SD scroll, ICH/DCH shifts, resize, and reverse resolution at read time
 * (cell attr XOR screen-wide DECSCNM).
 */

#include "../../../src/yetty/yscene/vtermgrid-store.h"

#include "ytest.h"

#include <stdint.h>
#include <string.h>

enum { TEST_ATTR_REVERSE = 1u << 4 };

static void test_marks_and_wide(struct ytest *test)
{
    struct yetty_yscene_vtermgrid_store store;
    YTEST_REQUIRE_OK(test,
                     yetty_yscene_vtermgrid_store_init(&store, 4, 10, 0xFFCCCCCCu, 0xFF101010u));

    /* A base with two combining marks is RETAINED whole. */
    uint32_t cluster[3] = {0x0065, 0x0301, 0x0308}; /* e + acute + diaeresis */
    YTEST_REQUIRE_OK(test, yetty_yscene_vtermgrid_store_put(&store, 0, 0, cluster, 3, 0));
    struct yetty_yscene_vtermgrid_store_cell cell;
    yetty_yscene_vtermgrid_store_read(&store, 0, 0, &cell);
    YTEST_CHECK_EQ_INT(test, (int)cell.codepoint, 0x0065);
    YTEST_CHECK_EQ_INT(test, cell.mark_count, 2);
    YTEST_CHECK_EQ_INT(test, (int)cell.marks[0], 0x0301);
    YTEST_CHECK_EQ_INT(test, (int)cell.marks[1], 0x0308);

    /* A wide glyph: head width 2, continuation width 0 with pen colors. */
    uint32_t wide_cp[1] = {0x4E2D};
    YTEST_REQUIRE_OK(test, yetty_yscene_vtermgrid_store_put(&store, 1, 2, wide_cp, 1, 1));
    yetty_yscene_vtermgrid_store_read(&store, 1, 2, &cell);
    YTEST_CHECK_EQ_INT(test, cell.width, 2);
    YTEST_CHECK_EQ_INT(test, (int)cell.codepoint, 0x4E2D);
    yetty_yscene_vtermgrid_store_read(&store, 1, 3, &cell);
    YTEST_CHECK_EQ_INT(test, cell.width, 0);
    YTEST_CHECK_EQ_INT(test, (int)cell.codepoint, 0);

    yetty_yscene_vtermgrid_store_free(&store);
}

static void test_pen_bce_and_reverse(struct ytest *test)
{
    struct yetty_yscene_vtermgrid_store store;
    YTEST_REQUIRE_OK(test,
                     yetty_yscene_vtermgrid_store_init(&store, 2, 8, 0xFFAAAAAAu, 0xFF000000u));
    store.pen_fg = 0xFF00FF00u;
    store.pen_bg = 0xFF000080u;
    store.pen_attrs = TEST_ATTR_REVERSE;

    uint32_t glyph[1] = {'R'};
    YTEST_REQUIRE_OK(test, yetty_yscene_vtermgrid_store_put(&store, 0, 0, glyph, 1, 0));
    struct yetty_yscene_vtermgrid_store_cell cell;
    yetty_yscene_vtermgrid_store_read(&store, 0, 0, &cell);
    /* Reverse resolved at read: fg/bg swapped (the fork's screen contract). */
    YTEST_CHECK(test, cell.fg == 0xFF000080u && cell.bg == 0xFF00FF00u);

    /* DECSCNM: global reverse XORs the cell attr — a reverse cell under a
     * reversed screen reads back UN-swapped. */
    store.global_reverse = 1;
    yetty_yscene_vtermgrid_store_read(&store, 0, 0, &cell);
    YTEST_CHECK(test, cell.fg == 0xFF00FF00u && cell.bg == 0xFF000080u);
    store.global_reverse = 0;

    /* BCE: erase blanks with the CURRENT pen colors. */
    yetty_yscene_vtermgrid_store_erase(&store, 0, 1, 0, 8);
    yetty_yscene_vtermgrid_store_read(&store, 0, 0, &cell);
    YTEST_CHECK_EQ_INT(test, (int)cell.codepoint, 0);
    YTEST_CHECK(test, cell.bg == 0xFF000080u); /* pen bg, not default */

    yetty_yscene_vtermgrid_store_free(&store);
}

static void test_scroll_and_shift(struct ytest *test)
{
    struct yetty_yscene_vtermgrid_store store;
    YTEST_REQUIRE_OK(test,
                     yetty_yscene_vtermgrid_store_init(&store, 3, 6, 0xFFFFFFFFu, 0xFF000000u));
    uint32_t glyphs[3] = {'a', 'b', 'c'};
    for (uint32_t col = 0; col < 3; ++col) {
        uint32_t one[1] = {glyphs[col]};
        YTEST_REQUIRE_OK(test, yetty_yscene_vtermgrid_store_put(&store, 0, col, one, 1, 0));
    }

    /* SU by 1: row 0 content leaves; the old row 1 (blank) lands at row 0...
     * seed row 1 first so the movement is observable. */
    uint32_t marker[1] = {'M'};
    YTEST_REQUIRE_OK(test, yetty_yscene_vtermgrid_store_put(&store, 1, 0, marker, 1, 0));
    yetty_yscene_vtermgrid_store_scroll(&store, 1);
    struct yetty_yscene_vtermgrid_store_cell cell;
    yetty_yscene_vtermgrid_store_read(&store, 0, 0, &cell);
    YTEST_CHECK_EQ_INT(test, (int)cell.codepoint, 'M');
    yetty_yscene_vtermgrid_store_read(&store, 2, 0, &cell);
    YTEST_CHECK_EQ_INT(test, (int)cell.codepoint, 0); /* vacated bottom row */

    /* ICH at col 0 by 1 on row 0: M moves to col 1; col 0 blanks. */
    yetty_yscene_vtermgrid_store_hshift(&store, 0, 0, 1);
    yetty_yscene_vtermgrid_store_read(&store, 0, 1, &cell);
    YTEST_CHECK_EQ_INT(test, (int)cell.codepoint, 'M');
    yetty_yscene_vtermgrid_store_read(&store, 0, 0, &cell);
    YTEST_CHECK_EQ_INT(test, (int)cell.codepoint, 0);

    /* DCH by 1: M returns to col 0. */
    yetty_yscene_vtermgrid_store_hshift(&store, 0, 0, -1);
    yetty_yscene_vtermgrid_store_read(&store, 0, 0, &cell);
    YTEST_CHECK_EQ_INT(test, (int)cell.codepoint, 'M');

    /* Resize preserves the overlapping rectangle. */
    YTEST_REQUIRE_OK(test, yetty_yscene_vtermgrid_store_resize(&store, 5, 4));
    yetty_yscene_vtermgrid_store_read(&store, 0, 0, &cell);
    YTEST_CHECK_EQ_INT(test, (int)cell.codepoint, 'M');
    yetty_yscene_vtermgrid_store_read(&store, 4, 3, &cell);
    YTEST_CHECK_EQ_INT(test, (int)cell.codepoint, 0);

    yetty_yscene_vtermgrid_store_free(&store);
}

/* DECSCA protection (review #11): pen-protected cells survive a SELECTIVE
 * erase and fall to a plain erase. */
static void test_selective_erase(struct ytest *test)
{
    struct yetty_yscene_vtermgrid_store store;
    YTEST_REQUIRE_OK(test,
                     yetty_yscene_vtermgrid_store_init(&store, 2, 8, 0xFFFFFFFFu, 0xFF000000u));
    uint32_t glyph_a = 'A';
    uint32_t glyph_b = 'B';
    store.pen_protected = 1;
    YTEST_REQUIRE_OK(test, yetty_yscene_vtermgrid_store_put(&store, 0, 0, &glyph_a, 1, 0));
    store.pen_protected = 0;
    YTEST_REQUIRE_OK(test, yetty_yscene_vtermgrid_store_put(&store, 0, 1, &glyph_b, 1, 0));

    struct yetty_yscene_vtermgrid_store_cell cell;
    yetty_yscene_vtermgrid_store_erase_selective(&store, 0, 1, 0, 8, 1);
    yetty_yscene_vtermgrid_store_read(&store, 0, 0, &cell);
    YTEST_CHECK_EQ_SIZE(test, cell.codepoint, (size_t)'A'); /* protected survives */
    yetty_yscene_vtermgrid_store_read(&store, 0, 1, &cell);
    YTEST_CHECK_EQ_SIZE(test, cell.codepoint, 0); /* unprotected erased */

    yetty_yscene_vtermgrid_store_erase_selective(&store, 0, 1, 0, 8, 0);
    yetty_yscene_vtermgrid_store_read(&store, 0, 0, &cell);
    YTEST_CHECK_EQ_SIZE(test, cell.codepoint, 0); /* plain erase takes it */

    yetty_yscene_vtermgrid_store_free(&store);
}

/* Review #12: an overlapping RIGHTWARD horizontal move must not overwrite
 * source cells before reading them — exercised through the grid store shape
 * the moverect callback drives (here: direct cell copies simulating the
 * callback's loop is covered by the vtermgrid cb; this pins the store's
 * cluster survival through hshift as the equivalent primitive). */
static void test_hshift_overlap_rightward(struct ytest *test)
{
    struct yetty_yscene_vtermgrid_store store;
    YTEST_REQUIRE_OK(test,
                     yetty_yscene_vtermgrid_store_init(&store, 1, 8, 0xFFFFFFFFu, 0xFF000000u));
    for (uint32_t col = 0; col < 4; ++col) {
        uint32_t glyph = 'A' + col;
        YTEST_REQUIRE_OK(test, yetty_yscene_vtermgrid_store_put(&store, 0, col, &glyph, 1, 0));
    }
    /* Shift right by 2 from col 0: ABCD.... -> ..ABCD.. (overlap: dest
     * range covers source). */
    yetty_yscene_vtermgrid_store_hshift(&store, 0, 0, 2);
    struct yetty_yscene_vtermgrid_store_cell cell;
    yetty_yscene_vtermgrid_store_read(&store, 0, 2, &cell);
    YTEST_CHECK_EQ_SIZE(test, cell.codepoint, (size_t)'A');
    yetty_yscene_vtermgrid_store_read(&store, 0, 5, &cell);
    YTEST_CHECK_EQ_SIZE(test, cell.codepoint, (size_t)'D');
    yetty_yscene_vtermgrid_store_read(&store, 0, 0, &cell);
    YTEST_CHECK_EQ_SIZE(test, cell.codepoint, 0);
    yetty_yscene_vtermgrid_store_free(&store);
}

int main(void)
{
    struct ytest test = ytest_begin("yscene_vtermgrid_store");
    YTEST_RUN(&test, test_marks_and_wide);
    YTEST_RUN(&test, test_pen_bce_and_reverse);
    YTEST_RUN(&test, test_scroll_and_shift);
    YTEST_RUN(&test, test_selective_erase);
    YTEST_RUN(&test, test_hshift_overlap_rightward);
    return ytest_end(&test);
}
