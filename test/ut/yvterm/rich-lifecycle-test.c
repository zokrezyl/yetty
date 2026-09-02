/*
 * yvterm rich-content lifecycle characterization test — headless, no GPU.
 *
 * Pins the CURRENT per-line rich-content behavior before the rich-block
 * restructure lands, so every later phase is judged against a recorded
 * baseline rather than assumptions:
 *   - relocate-to-bottom: a multi-row block is owned by its bottom line and
 *     slot_span recovers the insertion row;
 *   - covered-row invalidation: a write or erase intersecting ANY covered
 *     row removes the complete block (the block-granular rule that
 *     replaced the pre-block bottom-owner-only behavior);
 *   - sealing: a block whose insertion row crosses the primary live top
 *     is marked sealed while its lower rows stay visible;
 *   - whole-screen erase clears visible line-anchored rich content in the
 *     grid itself and fires the clear hook on top;
 *   - clear_rich_all covers both rings, the materialization cache and the
 *     archive watermark (no resurrection on scroll-back);
 *   - alternate-screen exit eagerly drops alternate rich content;
 *   - two envelopes on one line share the line's storage (the coarseness the
 *     block model later separates).
 *
 * Complexes are fabricated with a test-local ops table; no factory, no GPU.
 */

#include <yetty/ydraw-factory/complex-factory.h>
#include <yetty/api/yvterm/grid.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/yvterm/group-key.h>
#include "yetty/yvterm/grid-sdf-layer.h"

#include "ytest.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* A complex-tier type id (>= 0x80000000) that no real factory owns. */
#define FAKE_COMPLEX_TYPE 0x80abcd01u

/* Group binding keys are nested-id PATHS folded to 64 bits; a flat top-level id
 * is a one-segment path. The tests address flat groups by this helper. */
static uint64_t gkey(uint32_t id)
{
    return yetty_yvterm_group_key_fold(YETTY_YVTERM_GROUP_KEY_ROOT, id);
}

struct rich_fixture {
    int destroy_count;
    int materialize_calls;
    int clear_hook_calls;
    /* Journal words the materialize hook last received (update replay). */
    uint32_t seen_journal_words;
    uint32_t seen_journal_field;
};

static void fake_complex_destroy(struct yetty_ydraw_complex *self)
{
    int *destroy_count = self->instance_data;
    (*destroy_count)++;
    free(self);
}

static const struct yetty_ydraw_complex_ops *fake_complex_ops(void)
{
    static const struct yetty_ydraw_complex_ops ops = {
        .destroy = fake_complex_destroy,
        .update = NULL,
    };
    return &ops;
}

static struct yetty_ydraw_complex *fake_complex_create(int *destroy_count)
{
    struct yetty_ydraw_complex *instance = calloc(1, sizeof(*instance));
    if (!instance) {
        return NULL;
    }
    instance->ops = fake_complex_ops();
    instance->type = FAKE_COMPLEX_TYPE;
    instance->instance_data = destroy_count;
    return instance;
}

static struct yetty_ycore_void_result fake_materialize(const uint32_t *envelope_words,
                                                       uint32_t envelope_word_count,
                                                       const uint32_t *journal_words,
                                                       uint32_t journal_word_count, void *userdata,
                                                       struct yetty_ydraw_complex **out_instance)
{
    struct rich_fixture *fixture = userdata;
    (void)envelope_words;
    (void)envelope_word_count;
    fixture->materialize_calls++;
    fixture->seen_journal_words = journal_word_count;
    fixture->seen_journal_field = journal_word_count >= 2u ? journal_words[0] : 0u;
    *out_instance = fake_complex_create(&fixture->destroy_count);
    if (!*out_instance) {
        return YETTY_ERR(yetty_ycore_void, "fake complex alloc failed");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result count_clear_hook(void *userdata)
{
    struct rich_fixture *fixture = userdata;
    fixture->clear_hook_calls++;
    return YETTY_OK_VOID();
}

static struct yetty_yclass_object *make_grid(struct ytest *test, uint32_t cols, uint32_t rows,
                                             uint32_t hot_rows)
{
    struct yetty_yclass_object_ptr_result r = yetty_yvterm_grid_make(cols, rows, 0, hot_rows);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

static void feed(struct ytest *test, struct yetty_yclass_object *grid, const char *bytes)
{
    struct yetty_ycore_void_result r = yetty_yvterm_grid_feed(grid, bytes, strlen(bytes));
    YTEST_REQUIRE_OK(test, r);
}

static void feed_newlines(struct ytest *test, struct yetty_yclass_object *grid, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        feed(test, grid, "\r\n");
    }
}

static const uint32_t *view_window(struct ytest *test, struct yetty_yclass_object *grid, int active,
                                   uint64_t view_top, uint32_t row_count)
{
    struct yetty_ycore_void_result view_res = yetty_yvterm_grid_set_view(grid, active, view_top);
    YTEST_REQUIRE_OK(test, view_res);
    uint32_t resolved = 0;
    struct yetty_ycore_const_uint32_ptr_result window_res =
        yetty_yvterm_grid_view_window(grid, row_count, &resolved);
    YTEST_REQUIRE_OK(test, window_res);
    YTEST_REQUIRE_NOT_NULL(test, window_res.value);
    YTEST_CHECK_EQ_INT(test, resolved, row_count);
    return window_res.value;
}

/* Aggregate helpers over the per-block accessors, preserving the pinned
 * pre-block semantics: complex count = live figure runtimes on the slot,
 * primitive count = retained records with wire bytes, span = the tallest
 * block anchored on the slot. */
static void slot_rich_totals(struct ytest *test, struct yetty_yclass_object *grid, uint32_t slot,
                             uint32_t *out_runtimes, uint32_t *out_records, uint32_t *out_span)
{
    *out_runtimes = 0;
    *out_records = 0;
    *out_span = 0;
    struct yetty_ycore_uint32_result block_count_res =
        yetty_yvterm_grid_slot_rich_block_count(grid, slot);
    YTEST_REQUIRE_OK(test, block_count_res);
    for (uint32_t block_index = 0; block_index < block_count_res.value; ++block_index) {
        uint32_t span = 0;
        uint32_t record_count = 0;
        struct yetty_ycore_void_result block_res =
            yetty_yvterm_grid_slot_rich_block(grid, slot, block_index, &span, &record_count);
        YTEST_REQUIRE_OK(test, block_res);
        if (span > *out_span) {
            *out_span = span;
        }
        for (uint32_t record_index = 0; record_index < record_count; ++record_index) {
            uint32_t word_count = 0;
            struct yetty_ydraw_complex *complex = NULL;
            struct yetty_ycore_const_uint32_ptr_result record_res =
                yetty_yvterm_grid_slot_rich_block_record(grid, slot, block_index, record_index,
                                                         &word_count, &complex);
            YTEST_REQUIRE_OK(test, record_res);
            if (complex) {
                (*out_runtimes)++;
            }
            if (word_count) {
                (*out_records)++;
            }
        }
    }
}

static uint32_t slot_complex_count(struct ytest *test, struct yetty_yclass_object *grid,
                                   uint32_t slot)
{
    uint32_t runtimes = 0, records = 0, span = 0;
    slot_rich_totals(test, grid, slot, &runtimes, &records, &span);
    return runtimes;
}

static uint32_t slot_primitive_count(struct ytest *test, struct yetty_yclass_object *grid,
                                     uint32_t slot)
{
    uint32_t runtimes = 0, records = 0, span = 0;
    slot_rich_totals(test, grid, slot, &runtimes, &records, &span);
    return records;
}

static uint32_t slot_span(struct ytest *test, struct yetty_yclass_object *grid, uint32_t slot)
{
    uint32_t runtimes = 0, records = 0, span = 0;
    slot_rich_totals(test, grid, slot, &runtimes, &records, &span);
    return span;
}

/* Append one fake envelope + runtime on the current cursor row, mirroring
 * the terminal ingest order (envelope first, then the instance). */
static void anchor_figure_at_cursor(struct ytest *test, struct yetty_yclass_object *grid,
                                    struct rich_fixture *fixture)
{
    uint32_t row = 0, col = 0, visible = 0;
    struct yetty_ycore_void_result cur = yetty_yvterm_grid_cursor(grid, &row, &col, &visible);
    YTEST_REQUIRE_OK(test, cur);

    uint32_t envelope[4] = {FAKE_COMPLEX_TYPE, 8u, 0xaaaaaaaau, 0xbbbbbbbbu};
    struct yetty_ycore_uint32_result env_res =
        yetty_yvterm_grid_append_primitive(grid, row, envelope, 4u);
    YTEST_REQUIRE_OK(test, env_res);

    struct yetty_ydraw_complex *instance = fake_complex_create(&fixture->destroy_count);
    YTEST_REQUIRE_NOT_NULL(test, instance);
    struct yetty_ycore_uint32_result attach_res =
        yetty_yvterm_grid_attach_complex(grid, row, instance);
    YTEST_REQUIRE_OK(test, attach_res);
}

/* Build the canonical 3-row block: content anchored at row 0, three reserve
 * newlines, then relocate. Bottom owner lands on visible row 2, covering
 * rows 0..2; the cursor ends on row 3. */
static void make_three_row_block(struct ytest *test, struct yetty_yclass_object *grid,
                                 struct rich_fixture *fixture)
{
    anchor_figure_at_cursor(test, grid, fixture);
    feed_newlines(test, grid, 3);
    struct yetty_ycore_void_result relocate_res =
        yetty_yvterm_grid_relocate_rich_to_bottom(grid, 3);
    YTEST_REQUIRE_OK(test, relocate_res);
}

/*---------------------------------------------------------------------------
 * Relocate-to-bottom: the block is owned by its bottom line; the top line no
 * longer carries it; slot_span recovers the covered height.
 *-------------------------------------------------------------------------*/
static void test_relocate_rehomes_block_to_bottom(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    make_three_row_block(test, grid, &fixture);

    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[0]), 0);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[0]), 0);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[0]), 0);

    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[2]), 1);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[2]), 1);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[2]), 3);
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 0);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Covered-row invalidation (the block-granular rule): writing text over ANY
 * row a block covers — owner or not — removes the complete block. A write
 * on an uncovered row below the block leaves it alone.
 *-------------------------------------------------------------------------*/
static void test_covered_row_write_removes_block(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    make_three_row_block(test, grid, &fixture);

    /* Row 4 is below the block: untouched. */
    feed(test, grid, "\x1b[5;1Hu");
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 0);
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[2]), 1);

    /* Row 0 is covered but NOT the owner: the whole block dies anyway. */
    feed(test, grid, "\x1b[1;1Hx");
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);
    window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[2]), 0);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[2]), 0);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[2]), 0);

    /* Owner-row write on a fresh block still removes it, as before. */
    feed(test, grid, "\x1b[8;1H"); /* park the cursor clear of the new block */
    feed(test, grid, "\x1b[1;1H");
    make_three_row_block(test, grid, &fixture);
    feed(test, grid, "\x1b[3;1Hz");
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 2);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Same rule through erase: EL on any covered row removes the whole block.
 *-------------------------------------------------------------------------*/
static void test_partial_erase_any_covered_row(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    make_three_row_block(test, grid, &fixture);

    feed(test, grid, "\x1b[5;1H\x1b[K"); /* EL below the block: untouched */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 0);

    feed(test, grid, "\x1b[1;1H\x1b[K"); /* EL over covered row 0: block dies */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[2]), 0);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[2]), 0);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Sealing: a block whose INSERTION row crosses the primary live top is
 * marked sealed even while its lower rows are still visible; content stays
 * rendered.
 *-------------------------------------------------------------------------*/
static void test_block_seals_when_fully_off(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 16);
    /* Block spans rows 0..2 of a 4-row screen; cursor ends on row 3. */
    make_three_row_block(test, grid, &fixture);

    const uint32_t *window = view_window(test, grid, 0, 0, 4);
    struct yetty_ycore_uint32_result sealed_res =
        yetty_yvterm_grid_slot_rich_block_sealed(grid, window[2], 0);
    YTEST_REQUIRE_OK(test, sealed_res);
    YTEST_CHECK_EQ_INT(test, sealed_res.value, 0);

    /* One newline scrolls the top row into scrollback, but rows 1..2 stay
     * visible. A block that is only PARTIALLY off screen must remain LIVE
     * (addressable) — a complex whose body is still visible has to keep
     * receiving CMD_UPDATE. The bottom owner moved up to window row 1. */
    feed_newlines(test, grid, 1);
    window = view_window(test, grid, 0, 0, 4);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[1]), 3);
    sealed_res = yetty_yvterm_grid_slot_rich_block_sealed(grid, window[1], 0);
    YTEST_REQUIRE_OK(test, sealed_res);
    YTEST_CHECK_EQ_INT(test, sealed_res.value, 0); /* still LIVE while partly on screen */
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[1]), 1);
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 0);

    /* Two more newlines push the WHOLE block above the live top — now it is
     * history and seals. Scroll the view back (active=1) to timeline 0 to
     * inspect it. */
    feed_newlines(test, grid, 2);
    window = view_window(test, grid, 1, 0, 4);
    sealed_res = yetty_yvterm_grid_slot_rich_block_sealed(grid, window[2], 0);
    YTEST_REQUIRE_OK(test, sealed_res);
    YTEST_CHECK_EQ_INT(test, sealed_res.value, 1);                          /* fully off → sealed */
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[2]), 1); /* still rendered */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 0);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Whole-screen erase (CSI 2J): the grid clears visible line-anchored rich
 * content itself (fill_line_blank releases each line's rich) AND fires the
 * clear hook so the terminal can tear down container figures + archive
 * watermark on top.
 *-------------------------------------------------------------------------*/
static void test_whole_screen_erase_clears_rich_and_fires_hook(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    struct yetty_ycore_void_result hook_res =
        yetty_yvterm_grid_set_clear_hook(grid, count_clear_hook, &fixture);
    YTEST_REQUIRE_OK(test, hook_res);
    make_three_row_block(test, grid, &fixture);

    feed(test, grid, "\x1b[2J");
    YTEST_CHECK_EQ_INT(test, fixture.clear_hook_calls, 1);
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[2]), 0);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[2]), 0);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * clear_rich_all: hot ring, materialization cache and archive watermark. A
 * cleared figure must not resurrect when its archived line scrolls back.
 *-------------------------------------------------------------------------*/
static void test_clear_rich_all_covers_rings_cache_watermark(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 8);
    struct yetty_ycore_void_result hook_res =
        yetty_yvterm_grid_set_materialize(grid, fake_materialize, &fixture);
    YTEST_REQUIRE_OK(test, hook_res);

    feed(test, grid, "marker");
    anchor_figure_at_cursor(test, grid, &fixture);
    feed_newlines(test, grid, 40); /* deep into the archive */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);

    /* Materializes once from the archive... */
    const uint32_t *window = view_window(test, grid, 1, 0, 4);
    YTEST_CHECK_EQ_INT(test, fixture.materialize_calls, 1);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[0]), 1);

    struct yetty_ycore_void_result clear_res = yetty_yvterm_grid_clear_rich_all(grid);
    YTEST_REQUIRE_OK(test, clear_res);

    /* ...and never again after clear_rich_all: cache cleared + watermark
     * suppresses the archived records on the next inflate. */
    window = view_window(test, grid, 1, 0, 4);
    YTEST_CHECK_EQ_INT(test, fixture.materialize_calls, 1);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[0]), 0);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[0]), 0);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Alternate screen: rich content anchored while the alt screen is active is
 * dropped eagerly on exit; re-entering starts clean.
 *-------------------------------------------------------------------------*/
static void test_alt_screen_exit_drops_alt_rich(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    feed(test, grid, "\x1b[?1049h"); /* enter alt */
    anchor_figure_at_cursor(test, grid, &fixture);
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[0]), 1);

    feed(test, grid, "\x1b[?1049l"); /* exit alt: alt rich dies eagerly */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);

    feed(test, grid, "\x1b[?1049h"); /* re-enter: clean */
    window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[0]), 0);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[0]), 0);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Two envelopes on one line share the line's storage (the coarseness the
 * rich-block model later separates): counts accumulate, one arena.
 *-------------------------------------------------------------------------*/
static void test_two_envelopes_share_line_storage(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    anchor_figure_at_cursor(test, grid, &fixture);
    anchor_figure_at_cursor(test, grid, &fixture);

    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[0]), 2);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[0]), 2);

    /* One glyph on the shared line kills BOTH blocks — the line is the unit. */
    feed(test, grid, "x");
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 2);
    window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[0]), 0);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Groups: DELETE removes one group's subtree; siblings survive; a second
 * DELETE of the same id reports not-found.
 *-------------------------------------------------------------------------*/
static void test_group_delete_removes_subtree(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    /* Group 7 with one figure; sibling group 8 with one primitive. */
    struct yetty_ycore_uint32_result open_res = yetty_yvterm_grid_rich_group_open(grid, 0, gkey(7));
    YTEST_REQUIRE_OK(test, open_res);
    YTEST_CHECK_EQ_INT(test, open_res.value, 0); /* created, not reopened */
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    open_res = yetty_yvterm_grid_rich_group_open(grid, 0, gkey(8));
    YTEST_REQUIRE_OK(test, open_res);
    uint32_t sibling_words[3] = {0x10000001u, 4u, 0xdeadbeefu};
    struct yetty_ycore_uint32_result append_res =
        yetty_yvterm_grid_append_primitive(grid, 0, sibling_words, 3u);
    YTEST_REQUIRE_OK(test, append_res);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[0]), 1);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[0]), 2);

    /* DELETE(7): its figure dies, sibling group 8 survives. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_delete(grid, gkey(7)));
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);
    window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[0]), 0);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[0]), 1);

    /* Same id again: the binding is gone. */
    struct yetty_ycore_void_result again = yetty_yvterm_grid_rich_group_delete(grid, gkey(7));
    YTEST_REQUIRE_ERR(test, again);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Nested groups: deleting the parent removes the child's content too.
 *-------------------------------------------------------------------------*/
static void test_group_delete_parent_removes_child(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(11)));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(12)));
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_delete(grid, gkey(11)));
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[0]), 0);
    /* The child's binding died with the parent. */
    struct yetty_ycore_void_result child_delete =
        yetty_yvterm_grid_rich_group_delete(grid, gkey(12));
    YTEST_REQUIRE_ERR(test, child_delete);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Reopen: GROUP(id) with a live binding replaces the group's content in its
 * ORIGINAL block — old records die, the anchor/span stay, and the reopen
 * reports itself so the ingest reserves nothing.
 *-------------------------------------------------------------------------*/
static void test_group_reopen_replaces_in_place(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    struct yetty_ycore_uint32_result open_res =
        yetty_yvterm_grid_rich_group_open(grid, 0, gkey(21));
    YTEST_REQUIRE_OK(test, open_res);
    YTEST_CHECK_EQ_INT(test, open_res.value, 0);
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    feed_newlines(test, grid, 3);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_relocate_rich_to_bottom(grid, 3));

    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[2]), 3);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[2]), 1);

    /* Reopen from a DIFFERENT cursor row: content replaces in the original
     * block; nothing lands at the cursor. */
    open_res = yetty_yvterm_grid_rich_group_open(grid, 5, gkey(21));
    YTEST_REQUIRE_OK(test, open_res);
    YTEST_CHECK_EQ_INT(test, open_res.value, 1);        /* reopened */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1); /* old figure died */
    uint32_t replacement_words[3] = {0x10000001u, 4u, 0x0badf00du};
    /* Row argument is irrelevant inside a group scope — the bound block
     * receives the record. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 5, replacement_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[2]), 3); /* anchor kept */
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[2]), 0);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[2]), 1);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[5]), 0); /* cursor row clean */

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Sealing removes addressability: once the block's top crosses into
 * scrollback, DELETE(id) answers not-found while the content stays
 * rendered; the id is immediately reusable for a new block.
 *-------------------------------------------------------------------------*/
static void test_group_binding_dies_on_seal(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 16);

    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(31)));
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    feed_newlines(test, grid, 3);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_relocate_rich_to_bottom(grid, 3));

    feed_newlines(test, grid, 3); /* whole block crosses above the live top — seals */
    struct yetty_ycore_void_result delete_res = yetty_yvterm_grid_rich_group_delete(grid, gkey(31));
    YTEST_REQUIRE_ERR(test, delete_res);
    const uint32_t *window = view_window(test, grid, 1, 0, 4);              /* scrolled-back view */
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[2]), 1); /* still rendered */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 0);

    /* The id rebinds to fresh live content without touching history. */
    struct yetty_ycore_uint32_result open_res =
        yetty_yvterm_grid_rich_group_open(grid, 3, gkey(31));
    YTEST_REQUIRE_OK(test, open_res);
    YTEST_CHECK_EQ_INT(test, open_res.value, 0); /* created anew, not reopened */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_delete(grid, gkey(31)));
    window = view_window(test, grid, 1, 0, 4); /* re-fetch the scrolled-back view */
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[2]), 1); /* history intact */

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Region scroll (DECSTBM): a block wholly inside the scrolled region moves
 * with its text; scrolling it against the region boundary destroys it
 * rather than leaving a stale figure over moved text.
 *-------------------------------------------------------------------------*/
static void test_region_scroll_moves_contained_block(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    /* Block covering rows 1..3 (anchor at row 1 + 3-row reserve). */
    feed(test, grid, "\x1b[2;1H");
    make_three_row_block(test, grid, &fixture);
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[3]), 1);

    /* Scroll region rows 1..6 (CSI 1;7r? DECSTBM is 1-based inclusive:
     * rows 1..7 → top=1 bottom=7 covers visible rows 0..6). Scroll UP one
     * line inside it: the block (rows 1..3) is wholly contained and moves
     * to rows 0..2. */
    feed(test, grid, "\x1b[1;7r");
    feed(test, grid, "\x1b[7;1H\n"); /* newline at region bottom scrolls the region */
    feed(test, grid, "\x1b[r");

    window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 0);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[2]), 1);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[2]), 3);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[3]), 0);

    /* Scroll up twice more: the block's top would leave the region — it is
     * destroyed, never smeared. */
    feed(test, grid, "\x1b[1;7r");
    feed(test, grid, "\x1b[7;1H\n\n\n");
    feed(test, grid, "\x1b[r");
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);
    window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[0]), 0);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[1]), 0);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[2]), 0);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * UPDATE machinery: a complex-node binding resolves to the newest runtime,
 * the journal retains accepted updates, and re-materialization replays them
 * through the hook.
 *-------------------------------------------------------------------------*/
static void test_update_bind_journal_and_replay(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 8);
    struct yetty_ycore_void_result hook_res =
        yetty_yvterm_grid_set_materialize(grid, fake_materialize, &fixture);
    YTEST_REQUIRE_OK(test, hook_res);

    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_bind(grid, gkey(1)));

    /* The binding resolves to the attached runtime. */
    struct yetty_ydraw_complex *target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, gkey(1), &target));
    YTEST_REQUIRE_NOT_NULL(test, target);

    /* Journal one accepted update: field 0x77, three payload words. */
    uint32_t payload[3] = {1u, 2u, 3u};
    YTEST_REQUIRE_OK(test,
                     yetty_yvterm_grid_rich_update_journal(grid, gkey(1), 0x77u, payload, 3u));

    /* Age into the archive: runtime dies, then the scrolled-back view
     * re-materializes WITH the journal (entry = 2 header words + payload). */
    feed_newlines(test, grid, 40);
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);
    (void)view_window(test, grid, 1, 0, 4);
    YTEST_CHECK_EQ_INT(test, fixture.materialize_calls, 1);
    YTEST_CHECK_EQ_INT(test, fixture.seen_journal_words, 5);
    YTEST_CHECK_EQ_INT(test, fixture.seen_journal_field, 0x77);

    /* Sealed content is no longer addressable: the binding died with the
     * boundary crossing. */
    target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, gkey(1), &target));
    YTEST_CHECK(test, target == NULL);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Re-insert at a live complex path REPLACES: the previous node dies whole
 * (create-or-exact-replace, drawable-use-cases.md §4) and the path resolves
 * to the new complex.
 *-------------------------------------------------------------------------*/
static void test_update_rebind_replaces_previous(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_bind(grid, gkey(1)));
    struct yetty_ydraw_complex *first_target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, gkey(1), &first_target));
    YTEST_REQUIRE_NOT_NULL(test, first_target);

    /* Second envelope on another row: same path, new figure — replacement. */
    feed_newlines(test, grid, 2);
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_bind(grid, gkey(1)));
    struct yetty_ydraw_complex *second_target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, gkey(1), &second_target));
    YTEST_REQUIRE_NOT_NULL(test, second_target);
    YTEST_CHECK(test, second_target != first_target);
    /* The older figure DIED with the replacement — one path, one node. */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Resize: a live block survives the reflow with its span and stays
 * addressable/invalidatable at its remapped rows (anchors + coverage
 * counters are re-derived at the resize commit point).
 *-------------------------------------------------------------------------*/
static void test_resize_remaps_block_anchors(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    make_three_row_block(test, grid, &fixture);

    struct yetty_ycore_void_result resize_res = yetty_yvterm_grid_resize(grid, 30, 10);
    YTEST_REQUIRE_OK(test, resize_res);
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 0);

    /* The block still renders at its reflowed position with its span. */
    const uint32_t *window = view_window(test, grid, 0, 0, 10);
    uint32_t owner_row = UINT32_MAX;
    for (uint32_t row = 0; row < 10; ++row) {
        if (slot_complex_count(test, grid, window[row]) == 1) {
            owner_row = row;
            break;
        }
    }
    YTEST_REQUIRE(test, owner_row != UINT32_MAX);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[owner_row]), 3);

    /* Coverage counters were rebuilt: writing over a covered (non-owner)
     * remapped row still removes the whole block. */
    YTEST_REQUIRE(test, owner_row >= 2);
    char over_covered[16];
    snprintf(over_covered, sizeof(over_covered), "\x1b[%u;1Hx", owner_row - 1u);
    feed(test, grid, over_covered);
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);
    window = view_window(test, grid, 0, 0, 10);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[owner_row]), 0);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Reopen replaces the WHOLE subtree (exact-subtree insert, §4): an omitted
 * id-bearing child disappears with the parent's replacement and its path is
 * gone — the subtree becomes exactly the supplied content.
 *-------------------------------------------------------------------------*/
static void test_reopen_replaces_whole_subtree(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(41)));
    uint32_t parent_words[3] = {0x10000001u, 4u, 0x11111111u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, parent_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(42)));
    anchor_figure_at_cursor(test, grid, &fixture); /* child content */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    /* Reopen parent 41 WITHOUT re-emitting child 42. */
    struct yetty_ycore_uint32_result open_res =
        yetty_yvterm_grid_rich_group_open(grid, 0, gkey(41));
    YTEST_REQUIRE_OK(test, open_res);
    YTEST_CHECK_EQ_INT(test, open_res.value, 1);
    uint32_t replacement_words[3] = {0x10000001u, 4u, 0x22222222u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, replacement_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    /* The omitted child died with the replacement: figure destroyed, no
     * complex rendered, path 42 gone. */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[0]), 0);
    struct yetty_ycore_void_result child_delete =
        yetty_yvterm_grid_rich_group_delete(grid, gkey(42));
    YTEST_REQUIRE_ERR(test, child_delete);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Reopen preserves the group's paint position: the replacement occupies the
 * replaced run's slot, ahead of later siblings, not appended behind them.
 *-------------------------------------------------------------------------*/
static void test_reopen_preserves_paint_position(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    (void)fixture;

    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(51)));
    uint32_t first_words[3] = {0x10000001u, 4u, 0xAAAAAAAAu};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, first_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(52)));
    uint32_t second_words[3] = {0x10000001u, 4u, 0xBBBBBBBBu};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, second_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    /* Reopen the FIRST group with new content. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(51)));
    uint32_t replacement_words[3] = {0x10000001u, 4u, 0xCCCCCCCCu};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, replacement_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    /* Published record indices never move: the replacement APPENDS (the
     * replaced record turns dead in place) and takes the replaced run's
     * paint position through its KEY — the group's replacement-anchor
     * sequence — not through array position. */
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    uint32_t alive_values[4] = {0};
    uint64_t alive_sequences[4] = {0};
    int32_t alive_z[4] = {0};
    uint32_t alive_ordinals[4] = {0};
    uint32_t alive_count = 0;
    uint32_t block_count = 0;
    struct yetty_ycore_uint32_result count_res =
        yetty_yvterm_grid_slot_rich_block_count(grid, window[0]);
    YTEST_REQUIRE_OK(test, count_res);
    block_count = count_res.value;
    for (uint32_t block_index = 0; block_index < block_count; ++block_index) {
        uint32_t record_count = 0;
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_slot_rich_block(grid, window[0], block_index, NULL,
                                                                 &record_count));
        for (uint32_t record_index = 0; record_index < record_count; ++record_index) {
            uint32_t word_count = 0;
            struct yetty_ycore_const_uint32_ptr_result words_res =
                yetty_yvterm_grid_slot_rich_block_record(grid, window[0], block_index, record_index,
                                                         &word_count, NULL);
            YTEST_REQUIRE_OK(test, words_res);
            if (words_res.value && word_count == 3u && alive_count < 4u) {
                YTEST_REQUIRE_OK(test, yetty_yvterm_grid_slot_rich_block_record_paint_key(
                                           grid, window[0], block_index, record_index,
                                           &alive_z[alive_count], &alive_sequences[alive_count],
                                           &alive_ordinals[alive_count]));
                alive_values[alive_count++] = words_res.value[2];
            }
        }
    }
    YTEST_CHECK_EQ_INT(test, alive_count, 2);
    /* Array order is B then C (append-only storage)... */
    YTEST_CHECK_EQ_INT(test, alive_values[0], 0xBBBBBBBBu);
    YTEST_CHECK_EQ_INT(test, alive_values[1], 0xCCCCCCCCu);
    /* ...but the PAINT order is C then B: the replacement inherited group
     * 51's anchor sequence, which precedes group 52's record sequence.
     * Both sit on the z-0 plane with ordinal-0 keys. */
    YTEST_CHECK_EQ_INT(test, alive_z[0], 0);
    YTEST_CHECK_EQ_INT(test, alive_z[1], 0);
    YTEST_CHECK(test, alive_sequences[1] < alive_sequences[0]);
    YTEST_CHECK_EQ_INT(test, alive_ordinals[1], 0);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Structural reopen keeps the WHOLE subtree in its emission slot: a nested
 * group created inside a replacement body inherits the replaced subtree's
 * paint anchor, so its content can never stack above records emitted after
 * the original (the overlay guarantee). A later direct reopen of that
 * nested group mints ordinals from the shared anchor counter — keys stay
 * unique and still sort below the overlay.
 *-------------------------------------------------------------------------*/
static void test_reopen_nested_subtree_keeps_slot(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    uint64_t k61 = gkey(61);
    uint64_t k61_5 = yetty_yvterm_group_key_fold(k61, 5);
    uint64_t k61_6 = yetty_yvterm_group_key_fold(k61, 6);

    /* App root with a nested widget group, then an overlay group AFTER it. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, k61));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, k61_5));
    uint32_t widget_words[3] = {0x10000001u, 4u, 0xAAAAAAAAu};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, widget_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(62)));
    uint32_t overlay_words[3] = {0x10000001u, 4u, 0xBBBBBBBBu};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, overlay_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    /* Structural reopen of the app root: a DIFFERENT nested group appears. */
    struct yetty_ycore_uint32_result reopen_res = yetty_yvterm_grid_rich_group_open(grid, 0, k61);
    YTEST_REQUIRE_OK(test, reopen_res);
    YTEST_CHECK_EQ_INT(test, reopen_res.value, 1);
    struct yetty_ycore_uint32_result nested_res = yetty_yvterm_grid_rich_group_open(grid, 0, k61_6);
    YTEST_REQUIRE_OK(test, nested_res);
    YTEST_CHECK_EQ_INT(test, nested_res.value, 0); /* fresh group inside the body */
    uint32_t fresh_words[3] = {0x10000001u, 4u, 0xCCCCCCCCu};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, fresh_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    /* Then a direct reopen of THAT nested group. */
    reopen_res = yetty_yvterm_grid_rich_group_open(grid, 0, k61_6);
    YTEST_REQUIRE_OK(test, reopen_res);
    YTEST_CHECK_EQ_INT(test, reopen_res.value, 1);
    uint32_t updated_words[3] = {0x10000001u, 4u, 0xDDDDDDDDu};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, updated_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    /* Alive: the overlay prim and the twice-replaced nested prim. */
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    uint64_t overlay_sequence = 0;
    uint64_t nested_sequence = 0;
    uint32_t nested_ordinal = 0;
    int seen_overlay = 0;
    int seen_nested = 0;
    uint32_t record_count = 0;
    YTEST_REQUIRE_OK(test,
                     yetty_yvterm_grid_slot_rich_block(grid, window[0], 0u, NULL, &record_count));
    for (uint32_t record_index = 0; record_index < record_count; ++record_index) {
        uint32_t word_count = 0;
        struct yetty_ycore_const_uint32_ptr_result words_res =
            yetty_yvterm_grid_slot_rich_block_record(grid, window[0], 0u, record_index, &word_count,
                                                     NULL);
        YTEST_REQUIRE_OK(test, words_res);
        if (!words_res.value || word_count != 3u) {
            continue;
        }
        int32_t paint_z = 0;
        uint64_t sequence = 0;
        uint32_t ordinal = 0;
        YTEST_REQUIRE_OK(test,
                         yetty_yvterm_grid_slot_rich_block_record_paint_key(
                             grid, window[0], 0u, record_index, &paint_z, &sequence, &ordinal));
        if (words_res.value[2] == 0xBBBBBBBBu) {
            overlay_sequence = sequence;
            seen_overlay = 1;
        } else if (words_res.value[2] == 0xDDDDDDDDu) {
            nested_sequence = sequence;
            nested_ordinal = ordinal;
            seen_nested = 1;
        }
    }
    YTEST_CHECK(test, seen_overlay);
    YTEST_CHECK(test, seen_nested);
    /* The nested content inherited the app root's anchor: it sorts BELOW
     * the overlay, and its ordinal came from the shared monotone counter
     * (0 went to the first replacement body's prim). */
    YTEST_CHECK(test, nested_sequence < overlay_sequence);
    YTEST_CHECK_EQ_INT(test, nested_ordinal, 1);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Binding churn: creating and deleting the same id many times must keep
 * working (tombstones are reclaimed, the table does not degrade).
 *-------------------------------------------------------------------------*/
static void test_binding_churn(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    (void)fixture;
    uint32_t churn_words[3] = {0x10000001u, 4u, 0x12341234u};
    for (uint32_t cycle = 0; cycle < 200; ++cycle) {
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(61)));
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, churn_words, 3u));
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_delete(grid, gkey(61)));
    }
    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Failed resize must not destroy rich content: with a ring allocation
 * failure injected, resize errors out and the figure survives at its
 * pre-resize anchor.
 *-------------------------------------------------------------------------*/
static void test_failed_resize_keeps_rich(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    make_three_row_block(test, grid, &fixture);

    /* Fail the ALTERNATE ring build: the reflowed primary carries the
     * handles by then — the abort path must hand them back. 10 rows: the
     * new primary consumes allocations 1..10, so 11 is the alternate's
     * first line. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_inject_ring_alloc_failure(grid, 11));
    struct yetty_ycore_void_result resize_res = yetty_yvterm_grid_resize(grid, 30, 10);
    YTEST_REQUIRE_ERR(test, resize_res);
    (void)yetty_yvterm_grid_inject_ring_alloc_failure(grid, 0);

    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 0);
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[2]), 1);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[2]), 3);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Key spaces: a group id, an update id with the same number, and the same
 * group id under another namespace are three independent bindings.
 *-------------------------------------------------------------------------*/
static void test_binding_key_spaces_disjoint(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    /* Group [5] with a figure bound as the complex node [5,77] — the complex
     * carries its OWN id nested under the group. */
    uint64_t k5_77 = yetty_yvterm_group_key_fold(gkey(5), 77);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(5)));
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_bind(grid, k5_77));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    /* Same LOCAL id 5 under a DIFFERENT parent — the nested PATH [9,5] — is the
     * replacement for the old producer namespace. It must be an independent
     * binding from the top-level [5]. */
    feed_newlines(test, grid, 2);
    uint64_t k9 = gkey(9);
    uint64_t k9_5 = yetty_yvterm_group_key_fold(k9, 5);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 2, k9));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 2, k9_5));
    uint32_t other_words[3] = {0x10000001u, 4u, 0x55555555u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 2, other_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    /* Deleting the top-level GROUP [5] cascades to its subtree — the complex
     * node [5,77] dies with it — but never touches the nested [9,5] group. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_delete(grid, gkey(5)));
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);
    struct yetty_ydraw_complex *dead_target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, k5_77, &dead_target));
    YTEST_CHECK(test, dead_target == NULL);
    struct yetty_ycore_uint32_result nested_res =
        yetty_yvterm_grid_rich_group_query(grid, k9_5, NULL);
    YTEST_REQUIRE_OK(test, nested_res);
    YTEST_CHECK_EQ_INT(test, nested_res.value, 1);
    /* The nested path deletes independently. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_delete(grid, k9_5));
    struct yetty_ycore_void_result again = yetty_yvterm_grid_rich_group_delete(grid, k9_5);
    YTEST_REQUIRE_ERR(test, again);

    /* Sentinel-shaped local ids (0, UINT32_MAX) fold like any other id. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 4, gkey(0)));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 4, other_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 4, gkey(UINT32_MAX)));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 4, other_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    struct yetty_ycore_uint32_result zero_res =
        yetty_yvterm_grid_rich_group_query(grid, gkey(0), NULL);
    YTEST_REQUIRE_OK(test, zero_res);
    YTEST_CHECK_EQ_INT(test, zero_res.value, 1);
    struct yetty_ycore_uint32_result max_res =
        yetty_yvterm_grid_rich_group_query(grid, gkey(UINT32_MAX), NULL);
    YTEST_REQUIRE_OK(test, max_res);
    YTEST_CHECK_EQ_INT(test, max_res.value, 1);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Kind change at a live path (§4): a complex inserted at a live GROUP path
 * replaces the whole group; a GROUP inserted at a live complex path replaces
 * the complex. One path, one node, whichever kind was supplied last.
 *-------------------------------------------------------------------------*/
static void test_insert_kind_change(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    /* A GROUP at path [7] holding a primitive. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(7)));
    uint32_t prim_words[3] = {0x10000001u, 4u, 0x77777777u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, prim_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    /* A complex bound at the SAME path: group -> complex kind change. */
    feed_newlines(test, grid, 2);
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_bind(grid, gkey(7)));
    struct yetty_ydraw_complex *target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, gkey(7), &target));
    YTEST_REQUIRE_NOT_NULL(test, target);

    /* A GROUP re-created at the SAME path: complex -> group kind change —
     * the complex dies, the open reports CREATE (not reopen). */
    feed_newlines(test, grid, 2);
    uint32_t cursor_row = 0, cursor_col = 0, cursor_visible = 0;
    YTEST_REQUIRE_OK(test,
                     yetty_yvterm_grid_cursor(grid, &cursor_row, &cursor_col, &cursor_visible));
    struct yetty_ycore_uint32_result open_res =
        yetty_yvterm_grid_rich_group_open(grid, cursor_row, gkey(7));
    YTEST_REQUIRE_OK(test, open_res);
    YTEST_CHECK_EQ_INT(test, open_res.value, 0); /* fresh group, not a reopen */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);
    target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, gkey(7), &target));
    YTEST_CHECK(test, target == NULL); /* the path is a group now — update is a no-op */

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Reopen + update rebinding: an update id bound to the REPLACEMENT complex
 * during the reopen body must resolve to that complex after the close
 * rotation (the tail moved left; the binding must move with it).
 *-------------------------------------------------------------------------*/
static void test_reopen_rebinds_update_to_replacement(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    /* Group 71 with a figure, then an anonymous sibling record AFTER it so
     * the rotation has a middle to shift. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(71)));
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    uint32_t sibling_words[3] = {0x10000001u, 4u, 0x71717171u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, sibling_words, 3u));

    /* Reopen 71: new figure + new update binding minted INSIDE the body. */
    struct yetty_ycore_uint32_result open_res =
        yetty_yvterm_grid_rich_group_open(grid, 0, gkey(71));
    YTEST_REQUIRE_OK(test, open_res);
    YTEST_CHECK_EQ_INT(test, open_res.value, 1);
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_bind(grid, gkey(3)));
    struct yetty_ydraw_complex *bound_before = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, gkey(3), &bound_before));
    YTEST_REQUIRE_NOT_NULL(test, bound_before);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    /* After the rotation the binding must still resolve to the SAME
     * replacement complex — not a sibling, not a dead record. */
    struct yetty_ydraw_complex *bound_after = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, gkey(3), &bound_after));
    YTEST_REQUIRE_NOT_NULL(test, bound_after);
    YTEST_CHECK(test, bound_after == bound_before);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Paint plan: one total order over primitives and complexes by
 * (paint_z, paint_sequence, record_ordinal) — never emission/array order
 * alone, and never all-prims-then-all-complexes.
 *-------------------------------------------------------------------------*/
static void append_box(struct ytest *test, struct yetty_yclass_object *grid, uint32_t row,
                       uint32_t z_bits, uint32_t fill)
{
    /* A REAL generated SDF box record — the store's z extraction resolves
     * the type through the SDF size table, so a made-up type id would pin
     * nothing. Layout: [type][z][fill][stroke][stroke_w][cx cy hw hh cr]. */
    uint32_t words[10] = {0};
    words[0] = (uint32_t)YETTY_YSDF_BOX;
    words[1] = z_bits;
    words[2] = fill;
    float geometry[5] = {10.0f, 10.0f, 4.0f, 4.0f, 0.0f};
    memcpy(&words[5], geometry, sizeof(geometry));
    struct yetty_ycore_uint32_result append_res =
        yetty_yvterm_grid_append_primitive(grid, row, words, 10u);
    YTEST_REQUIRE_OK(test, append_res);
}

static void test_paint_plan_total_order(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    append_box(test, grid, 0, 5u, 0xAAAAAAAAu);           /* A: z 5 */
    append_box(test, grid, 0, (uint32_t)-1, 0xBBBBBBBBu); /* B: z -1 */
    struct yetty_ydraw_complex *instance = fake_complex_create(&fixture.destroy_count);
    YTEST_REQUIRE_NOT_NULL(test, instance);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_attach_complex(grid, 0, instance)); /* C: z 0 */
    append_box(test, grid, 0, 0u, 0xDDDDDDDDu);                                  /* D: z 0 */

    struct yetty_ycore_uint32_result count_res = yetty_yvterm_grid_paint_plan_leaf_count(grid);
    YTEST_REQUIRE_OK(test, count_res);
    YTEST_REQUIRE(test, count_res.value == 4u);

    /* Expected total order: B(z-1) < C(z0, earlier sequence) < D(z0) < A(z5).
     * The complex sits BETWEEN equal-z primitives — its sequence, not its
     * kind, decides the cut point. */
    uint32_t expected_records[4] = {1u, 2u, 3u, 0u};
    uint32_t expected_kinds[4] = {0u, 1u, 0u, 0u}; /* prim, complex, prim, prim */
    int32_t expected_z[4] = {-1, 0, 0, 5};
    uint64_t previous_sequence = 0;
    for (uint32_t leaf = 0; leaf < 4u; ++leaf) {
        uint32_t record_index = 0, kind = 0, ordinal = 0;
        int32_t paint_z = 0;
        uint64_t sequence = 0;
        YTEST_REQUIRE_OK(test,
                         yetty_yvterm_grid_paint_plan_leaf(grid, leaf, NULL, &record_index, &kind,
                                                           &paint_z, &sequence, &ordinal));
        YTEST_CHECK_EQ_INT(test, record_index, expected_records[leaf]);
        YTEST_CHECK_EQ_INT(test, kind, expected_kinds[leaf]);
        YTEST_CHECK_EQ_INT(test, paint_z, expected_z[leaf]);
        YTEST_CHECK_EQ_INT(test, ordinal, 0);
        if (leaf > 0 && paint_z == expected_z[leaf - 1u]) {
            YTEST_CHECK(test, sequence > previous_sequence); /* equal z → sequence order */
        }
        previous_sequence = sequence;
    }

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Paint plan cache: reads reuse the built plan; membership changes rebuild
 * it; ZERO/refill mints fresh (higher) sequences — the allocator never
 * rewinds.
 *-------------------------------------------------------------------------*/
static void test_paint_plan_cache_and_zero_sequences(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    append_box(test, grid, 0, 0u, 0x11111111u);
    struct yetty_ycore_uint32_result count_res = yetty_yvterm_grid_paint_plan_leaf_count(grid);
    YTEST_REQUIRE_OK(test, count_res);
    YTEST_CHECK_EQ_INT(test, count_res.value, 1);
    struct yetty_ycore_uint64_result build_res = yetty_yvterm_grid_paint_plan_build_count(grid);
    YTEST_REQUIRE_OK(test, build_res);
    uint64_t builds_after_first = build_res.value;

    /* Repeated reads and a view change reuse the cached order — the plan
     * holds handles, so the scrollback view origin is per-frame state. */
    (void)view_window(test, grid, 0, 0, 8);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_paint_plan_leaf_count(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_paint_plan_leaf_count(grid));
    build_res = yetty_yvterm_grid_paint_plan_build_count(grid);
    YTEST_REQUIRE_OK(test, build_res);
    YTEST_CHECK_EQ_SIZE(test, build_res.value, builds_after_first);

    uint64_t sequence_before_zero = 0;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_slot_rich_block_record_paint_key(
                               grid, view_window(test, grid, 0, 0, 8)[0], 0u, 0u, NULL,
                               &sequence_before_zero, NULL));

    /* Appending is a membership change: the next read rebuilds. */
    append_box(test, grid, 0, 0u, 0x22222222u);
    count_res = yetty_yvterm_grid_paint_plan_leaf_count(grid);
    YTEST_REQUIRE_OK(test, count_res);
    YTEST_CHECK_EQ_INT(test, count_res.value, 2);
    build_res = yetty_yvterm_grid_paint_plan_build_count(grid);
    YTEST_REQUIRE_OK(test, build_res);
    YTEST_CHECK(test, build_res.value > builds_after_first);

    /* ZERO, then refill: the new record's sequence is strictly beyond the
     * pre-ZERO one. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_clear_rich_all(grid));
    count_res = yetty_yvterm_grid_paint_plan_leaf_count(grid);
    YTEST_REQUIRE_OK(test, count_res);
    YTEST_CHECK_EQ_INT(test, count_res.value, 0);
    append_box(test, grid, 0, 0u, 0x33333333u);
    uint64_t sequence_after_zero = 0;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_slot_rich_block_record_paint_key(
                               grid, view_window(test, grid, 0, 0, 8)[0], 0u, 0u, NULL,
                               &sequence_after_zero, NULL));
    YTEST_CHECK(test, sequence_after_zero > sequence_before_zero);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Deep-ring smoke: the plan build + full accessor walk over thousands of
 * leaves completes within the suite timeout (the walk is linear with an
 * O(1) per-leaf test — a complexity regression blows the gate).
 *-------------------------------------------------------------------------*/
static void test_paint_plan_deep_ring_smoke(struct ytest *test)
{
    /* A deep hot ring so the blocks stay resident (the plan covers ALL
     * resident leaves, not the viewport). */
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 2000);
    enum { DEEP_BLOCKS = 500, DEEP_RECORDS = 20 };
    for (uint32_t block = 0; block < DEEP_BLOCKS; ++block) {
        uint32_t row = 0, col = 0, visible = 0;
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_cursor(grid, &row, &col, &visible));
        for (uint32_t record = 0; record < DEEP_RECORDS; ++record) {
            append_box(test, grid, row, record % 7u, 0x40404040u);
        }
        /* One reserve line closes the burst so each iteration mints a new
         * block at the cursor. */
        feed_newlines(test, grid, 1);
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_relocate_rich_to_bottom(grid, 1u));
    }
    struct yetty_ycore_uint32_result count_res = yetty_yvterm_grid_paint_plan_leaf_count(grid);
    YTEST_REQUIRE_OK(test, count_res);
    /* Ring depth bounds residency; every resident leaf is in the plan. */
    YTEST_CHECK(test, count_res.value >= DEEP_BLOCKS * DEEP_RECORDS / 2u);
    uint64_t previous_key[3] = {0, 0, 0};
    int have_previous = 0;
    for (uint32_t leaf = 0; leaf < count_res.value; ++leaf) {
        uint32_t ordinal = 0;
        int32_t paint_z = 0;
        uint64_t sequence = 0;
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_paint_plan_leaf(grid, leaf, NULL, NULL, NULL,
                                                                 &paint_z, &sequence, &ordinal));
        uint64_t key[3] = {(uint64_t)(int64_t)paint_z + 0x80000000ull, sequence, ordinal};
        if (have_previous) {
            int ascending = key[0] > previous_key[0] ||
                            (key[0] == previous_key[0] &&
                             (key[1] > previous_key[1] ||
                              (key[1] == previous_key[1] && key[2] > previous_key[2])));
            YTEST_CHECK(test, ascending);
        }
        memcpy(previous_key, key, sizeof(previous_key));
        have_previous = 1;
    }
    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Ambient paint-z scope: while a scope is open every appended record —
 * complexes included, which carry no wire z — paints at the scope's z,
 * overriding the record's own wire z; nested scopes use the innermost;
 * outside any scope records keep their wire z.
 *-------------------------------------------------------------------------*/
static void test_ambient_paint_z_overrides(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    (void)fixture;

    /* A box wire record carrying its OWN z = 7. */
    uint32_t box7[10] = {0};
    box7[0] = (uint32_t)YETTY_YSDF_BOX;
    box7[1] = 7u;
    float geometry[5] = {10.0f, 10.0f, 4.0f, 4.0f, 0.0f};
    memcpy(&box7[5], geometry, sizeof(geometry));

    /* Outside any scope: the box keeps its wire z = 7. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, box7, 10u));

    /* Scope z = -5: the SAME box record now paints at -5, not 7. And a
     * complex (no wire z) also paints at -5. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_push_paint_z(grid, -5));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, box7, 10u));
    uint32_t complex_words[6] = {0x80000003u, 16u, 0u, 0u, 0u, 0u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, complex_words, 6u));
    /* Nested scope z = 3: innermost wins. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_push_paint_z(grid, 3));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, box7, 10u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_pop_paint_z(grid));
    /* Back to the outer scope z = -5. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, box7, 10u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_pop_paint_z(grid));
    /* Scope closed: the box keeps its wire z = 7 again. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, box7, 10u));

    /* Records 0..5 in emission order; check each record's paint_z. */
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    int32_t expected_z[6] = {7, -5, -5, 3, -5, 7};
    for (uint32_t record_index = 0; record_index < 6u; ++record_index) {
        int32_t paint_z = 0;
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_slot_rich_block_record_paint_key(
                                   grid, window[0], 0u, record_index, &paint_z, NULL, NULL));
        YTEST_CHECK_EQ_INT(test, paint_z, expected_z[record_index]);
    }

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Group offset state (the ONE mover): set on a live group; rejected on a
 * complex node, a missing path, and a sealed group; ancestor offsets
 * ACCUMULATE for nested leaves; root-level (group-less) leaves stay at 0;
 * the offset survives an exact-subtree reopen (state, not content).
 *-------------------------------------------------------------------------*/
static void test_group_offset_state(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    /* Nested groups with a figure inside, plus a root-level anonymous prim. */
    uint64_t k9 = gkey(9);
    uint64_t k9_5 = yetty_yvterm_group_key_fold(k9, 5);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, k9));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, k9_5));
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_bind(grid, gkey(77)));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    uint32_t loose_words[3] = {0x10000001u, 4u, 0x11111111u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, loose_words, 3u));

    /* Offsets on both levels of the chain. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, k9, 10.0f, -20.0f));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, k9_5, 1.0f, 2.0f));

    /* Rejections: a complex node, a missing path. */
    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_group_offset_set(grid, gkey(77), 5.0f, 5.0f));
    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_group_offset_set(grid, gkey(12345), 1.0f, 1.0f));

    /* Accumulation: the figure's leaf sums both ancestors; the loose prim's
     * leaf projects at zero. */
    struct yetty_ycore_uint32_result count_res = yetty_yvterm_grid_paint_plan_leaf_count(grid);
    YTEST_REQUIRE_OK(test, count_res);
    int checked_nested = 0;
    int checked_loose = 0;
    for (uint32_t leaf = 0; leaf < count_res.value; ++leaf) {
        uint32_t kind = 0;
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_paint_plan_leaf(grid, leaf, NULL, NULL, &kind,
                                                                 NULL, NULL, NULL));
        float offset_x = 0.0f;
        float offset_y = 0.0f;
        YTEST_REQUIRE_OK(
            test, yetty_yvterm_grid_paint_plan_leaf_offset(grid, leaf, &offset_x, &offset_y));
        if (kind == 1u) { /* the complex leaf — nested under [9, 9.5] */
            YTEST_CHECK(test, offset_x == 11.0f && offset_y == -18.0f);
            checked_nested = 1;
        } else if (offset_x == 0.0f && offset_y == 0.0f) {
            checked_loose = 1;
        }
    }
    YTEST_CHECK(test, checked_nested);
    YTEST_CHECK(test, checked_loose);

    /* Exact-subtree reopen: the group's CONTENT is replaced, its STATE (the
     * offset) persists — a replaced panel stays scrolled. */
    struct yetty_ycore_uint32_result open_res = yetty_yvterm_grid_rich_group_open(grid, 0, k9);
    YTEST_REQUIRE_OK(test, open_res);
    YTEST_CHECK_EQ_INT(test, open_res.value, 1);
    uint32_t fresh_words[3] = {0x10000001u, 4u, 0x22222222u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, fresh_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    count_res = yetty_yvterm_grid_paint_plan_leaf_count(grid);
    YTEST_REQUIRE_OK(test, count_res);
    int found_offset_leaf = 0;
    for (uint32_t leaf = 0; leaf < count_res.value; ++leaf) {
        float offset_x = 0.0f;
        float offset_y = 0.0f;
        if (YETTY_IS_ERR(
                yetty_yvterm_grid_paint_plan_leaf_offset(grid, leaf, &offset_x, &offset_y))) {
            continue;
        }
        if (offset_x == 10.0f && offset_y == -20.0f) {
            found_offset_leaf = 1; /* the replacement content under group 9 */
        }
    }
    YTEST_CHECK(test, found_offset_leaf);

    /* Sealing kills the offset address like every other one: scroll the
     * block fully out, the path stops resolving. */
    feed_newlines(test, grid, 40);
    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_group_offset_set(grid, k9, 0.0f, 0.0f));

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * A full-height insertion must survive its own reserve: the span is declared
 * BEFORE the cursor advance, so the scroll the advance triggers sees the
 * block's true coverage instead of a one-row provisional — previously the
 * block was sealed (primary) during its own creation.
 *-------------------------------------------------------------------------*/
static void test_full_height_insert_survives_own_reserve(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(41)));
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    /* Ingest order: declare span, advance (scrolls once: 8 rows from row 0
     * on an 8-row screen), then relocate. */
    struct yetty_ycore_uint32_result declare_res = yetty_yvterm_grid_rich_span_declare(grid, 8u);
    YTEST_REQUIRE_OK(test, declare_res);
    YTEST_CHECK_EQ_INT(test, declare_res.value, 8); /* primary: full advance */
    feed_newlines(test, grid, 8);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_relocate_rich_to_bottom(grid, 8u));

    /* The block is LIVE and addressable — not sealed by its own reserve. */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 0);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, gkey(41), 3.0f, 4.0f));

    /* Bottom owner sits on the last covered visible row (timeline 7, one
     * scroll → visible row 6) with the full span. */
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[6]), 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[6]), 1);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * The alternate screen has no history: the reserve advance is clamped so an
 * insertion can never scroll away (= destroy) its own content, and a span
 * taller than the remaining rows is clipped to the screen.
 *-------------------------------------------------------------------------*/
static void test_alt_screen_insert_never_scrolls_itself(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    feed(test, grid, "\x1b[?1049h\x1b[H"); /* alternate screen, cursor home */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(42)));
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    struct yetty_ycore_uint32_result declare_res = yetty_yvterm_grid_rich_span_declare(grid, 8u);
    YTEST_REQUIRE_OK(test, declare_res);
    YTEST_CHECK_EQ_INT(test, declare_res.value, 7); /* clamped: stop ON the last row */
    feed_newlines(test, grid, declare_res.value);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_relocate_rich_to_bottom(grid, 8u));

    /* No scroll happened — the block survived its own insertion. */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 0);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, gkey(42), 1.0f, 1.0f));
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[7]), 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[7]), 1);

    /* Taller than the remaining screen (cursor now on the last row): the
     * ADVANCE clamps to zero (an insertion never scrolls itself), but the
     * stamped span keeps the DECLARED value — it is the addressable
     * extent, and the projection clips the part below the surface. A span
     * truncated to the birth-height pane would cap the insertion forever:
     * later reopens laid out for a taller pane (RESERVE-budget producers,
     * pane grow) would fail the replacement fit check. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 7, gkey(43)));
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    declare_res = yetty_yvterm_grid_rich_span_declare(grid, 20u);
    YTEST_REQUIRE_OK(test, declare_res);
    YTEST_CHECK_EQ_INT(test, declare_res.value, 0);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_relocate_rich_to_bottom(grid, 20u));
    uint32_t declared_span = 0;
    struct yetty_ycore_uint32_result query_res =
        yetty_yvterm_grid_rich_group_query(grid, gkey(43), &declared_span);
    YTEST_REQUIRE_OK(test, query_res);
    YTEST_CHECK_EQ_INT(test, query_res.value, 1); /* live */
    YTEST_CHECK_EQ_INT(test, declared_span, 20);  /* declared, not truncated */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 0);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Mixed batch: an interleaved reopen of an OLD block must not orphan the
 * batch's provisional insertion — declare/relocate target the provisional
 * block regardless of which block the mutation scope last routed into, in
 * either command order, and a reopen of a root created in the same batch
 * stays inside that batch's block.
 *-------------------------------------------------------------------------*/
static void test_mixed_batch_keeps_provisional_insertion(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    /* Batch 1: the OLD block — group 81, one figure, span 1. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(81)));
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    struct yetty_ycore_uint32_result declare_res = yetty_yvterm_grid_rich_span_declare(grid, 1u);
    YTEST_REQUIRE_OK(test, declare_res);
    feed_newlines(test, grid, declare_res.value);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_relocate_rich_to_bottom(grid, 1u));

    /* Batch 2, order new-root-then-reopen: fresh anonymous prim (opens the
     * provisional block at row 1), THEN a reopen of old group 81 (which
     * re-targets the scope at the old block and closes it), then finalize
     * with span 2. */
    uint32_t fresh_words[3] = {0x10000001u, 4u, 0xAAAA0001u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 1, fresh_words, 3u));
    struct yetty_ycore_uint32_result reopen_res =
        yetty_yvterm_grid_rich_group_open(grid, 1, gkey(81));
    YTEST_REQUIRE_OK(test, reopen_res);
    YTEST_CHECK_EQ_INT(test, reopen_res.value, 1);
    uint32_t replacement_words[3] = {0x10000001u, 4u, 0xAAAA0002u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 1, replacement_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    declare_res = yetty_yvterm_grid_rich_span_declare(grid, 2u);
    YTEST_REQUIRE_OK(test, declare_res);
    YTEST_CHECK_EQ_INT(test, declare_res.value, 2);
    feed_newlines(test, grid, declare_res.value);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_relocate_rich_to_bottom(grid, 2u));

    /* The provisional block was stamped + relocated: span 2, bottom owner on
     * visible row 2 (rows 1..2). The old block keeps span 1 on row 0. */
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[2]), 2);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[2]), 1);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[0]), 1);

    /* Batch 3, order reopen-then-new-root: reopen 81 first, close, THEN the
     * fresh root (which must open a NEW provisional block at the cursor,
     * row 3), finalize span 1. */
    reopen_res = yetty_yvterm_grid_rich_group_open(grid, 3, gkey(81));
    YTEST_REQUIRE_OK(test, reopen_res);
    YTEST_CHECK_EQ_INT(test, reopen_res.value, 1);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    uint32_t late_words[3] = {0x10000001u, 4u, 0xAAAA0003u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 3, late_words, 3u));
    declare_res = yetty_yvterm_grid_rich_span_declare(grid, 1u);
    YTEST_REQUIRE_OK(test, declare_res);
    feed_newlines(test, grid, declare_res.value);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_relocate_rich_to_bottom(grid, 1u));
    window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[3]), 1);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[3]), 1);

    /* Batch 4, new-root-then-reopen-same-root: create group 82, close, then
     * reopen it in the SAME batch — the reopen stays inside the batch's
     * provisional block and finalization places it once. */
    struct yetty_ycore_uint32_result open_res =
        yetty_yvterm_grid_rich_group_open(grid, 4, gkey(82));
    YTEST_REQUIRE_OK(test, open_res);
    YTEST_CHECK_EQ_INT(test, open_res.value, 0);
    uint32_t first_words[3] = {0x10000001u, 4u, 0xAAAA0004u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 4, first_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    reopen_res = yetty_yvterm_grid_rich_group_open(grid, 4, gkey(82));
    YTEST_REQUIRE_OK(test, reopen_res);
    YTEST_CHECK_EQ_INT(test, reopen_res.value, 1);
    uint32_t second_words[3] = {0x10000001u, 4u, 0xAAAA0005u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 4, second_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    declare_res = yetty_yvterm_grid_rich_span_declare(grid, 1u);
    YTEST_REQUIRE_OK(test, declare_res);
    feed_newlines(test, grid, declare_res.value);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_relocate_rich_to_bottom(grid, 1u));
    window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[4]), 1);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[4]), 1); /* replaced once */
    uint32_t live_span = 0;
    struct yetty_ycore_uint32_result query_res =
        yetty_yvterm_grid_rich_group_query(grid, gkey(82), &live_span);
    YTEST_REQUIRE_OK(test, query_res);
    YTEST_CHECK_EQ_INT(test, query_res.value, 1);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * DECSTBM during the reserve advance: reservation newlines fed at a scroll
 * region's bottom margin scroll the REGION, not the screen. A provisional
 * block wholly inside the region rides the scroll (anchors + tracked handle
 * shift together); one crossing the region boundary is destroyed, never
 * smeared — the standard region rules applied to the insertion's own
 * placement.
 *-------------------------------------------------------------------------*/
static void test_region_scroll_during_reserve(struct ytest *test)
{
    /* Carried: region rows 0..5, cursor on the margin (row 5), one-row
     * insertion; its single reserve newline region-scrolls it to row 4. */
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    feed(test, grid, "\x1b[1;6r\x1b[6;1H");
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 5, gkey(95)));
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    struct yetty_ycore_uint32_result declare_res = yetty_yvterm_grid_rich_span_declare(grid, 1u);
    YTEST_REQUIRE_OK(test, declare_res);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_reserve_advance(grid, declare_res.value));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_relocate_rich_to_bottom(grid, 1u));
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 0);
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[4]), 1);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[4]), 1);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[5]), 0);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, gkey(95), 1.0f, 1.0f));

    /* Coverage integrity after the carried move: exactly ONE count on the
     * destination row, and invalidating the block (a text write over it)
     * returns the gate to ZERO — a leaked counter would make every later
     * write on the row walk the rich path forever. */
    struct yetty_ycore_uint32_result coverage_res =
        yetty_yvterm_grid_slot_rich_coverage(grid, window[4]);
    YTEST_REQUIRE_OK(test, coverage_res);
    YTEST_CHECK_EQ_INT(test, coverage_res.value, 1);
    feed(test, grid, "\x1b[5;1Hx"); /* write over the carried block's row */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);
    window = view_window(test, grid, 0, 0, 8);
    coverage_res = yetty_yvterm_grid_slot_rich_coverage(grid, window[4]);
    YTEST_REQUIRE_OK(test, coverage_res);
    YTEST_CHECK_EQ_INT(test, coverage_res.value, 0);
    yetty_yvterm_grid_dispose(grid);

    /* Boundary-cut: region rows 0..3, cursor on its margin (row 3), TWO-row
     * insertion (rows 3..4 cross the region bottom). The region scroll cuts
     * it → destroyed whole, batch invalidated, finalize a clean no-op. */
    struct rich_fixture cut_fixture = {0};
    grid = make_grid(test, 20, 8, 16);
    feed(test, grid, "\x1b[1;4r\x1b[4;1H");
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 3, gkey(96)));
    anchor_figure_at_cursor(test, grid, &cut_fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    declare_res = yetty_yvterm_grid_rich_span_declare(grid, 2u);
    YTEST_REQUIRE_OK(test, declare_res);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_reserve_advance(grid, declare_res.value));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_relocate_rich_to_bottom(grid, 2u));
    YTEST_CHECK_EQ_INT(test, cut_fixture.destroy_count, 1);
    uint32_t cut_span = 0;
    struct yetty_ycore_uint32_result query_res =
        yetty_yvterm_grid_rich_group_query(grid, gkey(96), &cut_span);
    YTEST_REQUIRE_OK(test, query_res);
    YTEST_CHECK_EQ_INT(test, query_res.value, 0); /* binding gone with the block */
    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_group_offset_set(grid, gkey(96), 1.0f, 1.0f));
    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * A reservation deeper than the ring's history capacity must not recycle
 * the line carrying the provisional block's handle: the chunked
 * reserve-advance re-homes it, and the block survives with full span,
 * live binding and its figure runtime.
 *-------------------------------------------------------------------------*/
static void test_ring_deep_reserve_keeps_block(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 4); /* tiny hot window */

    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(91)));
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    struct yetty_ycore_uint32_result declare_res = yetty_yvterm_grid_rich_span_declare(grid, 40u);
    YTEST_REQUIRE_OK(test, declare_res);
    YTEST_CHECK_EQ_INT(test, declare_res.value, 40);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_reserve_advance(grid, declare_res.value));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_relocate_rich_to_bottom(grid, 40u));

    /* 40 rows from row 0 on an 8-row screen scrolled 33; the block's bottom
     * owner (timeline 39) sits on visible row 6 with the whole span, its
     * runtime intact, and the binding still live (top scrolled into
     * history, bottom still on screen — NOT sealed). */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 0);
    const uint32_t *window = view_window(test, grid, 0, 33, 8);
    YTEST_CHECK_EQ_INT(test, slot_span(test, grid, window[6]), 40);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[6]), 1);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, gkey(91), 2.0f, 2.0f));

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Offsets come off the wire and are summed + floored downstream: non-finite
 * or out-of-range values must be rejected at the choke point, leaving the
 * stored offset untouched.
 *-------------------------------------------------------------------------*/
static void test_offset_rejects_non_finite(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, gkey(44)));
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_group_offset_set(grid, gkey(44), NAN, 0.0f));
    YTEST_REQUIRE_ERR(test,
                      yetty_yvterm_grid_rich_group_offset_set(grid, gkey(44), 0.0f, INFINITY));
    YTEST_REQUIRE_ERR(test,
                      yetty_yvterm_grid_rich_group_offset_set(grid, gkey(44), -INFINITY, 0.0f));
    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_group_offset_set(grid, gkey(44), 2.0e7f, 0.0f));
    /* A sane value still lands. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, gkey(44), 5.0f, -5.0f));

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Per-node scroll retirement (UC-12): after a terminal scroll, a node whose
 * projected footprint fully left the live surface retires PERMANENTLY while
 * siblings with visible content stay addressable; detached (offset-moved)
 * and unknown-extent nodes are exempt; a live ancestor's reopen replaces
 * only the live remainder — the retired subtree stays rendered frozen.
 *-------------------------------------------------------------------------*/
static void test_node_retires_when_footprint_leaves(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    YTEST_REQUIRE_OK(test,
                     yetty_yvterm_grid_set_pixel_size(grid, 20u * 8u, 8u * 16u)); /* cell 16 */

    uint64_t k101 = gkey(101);
    uint64_t k102 = yetty_yvterm_group_key_fold(k101, 2);
    uint64_t k103 = yetty_yvterm_group_key_fold(k101, 3);
    uint64_t k104 = yetty_yvterm_group_key_fold(k101, 4);
    uint64_t k105 = yetty_yvterm_group_key_fold(k101, 5);
    uint32_t header_words[3] = {0x10000001u, 4u, 0xA1A1A1A1u};
    uint32_t body_words[3] = {0x10000001u, 4u, 0xB2B2B2B2u};
    uint32_t detached_words[3] = {0x10000001u, 4u, 0xD4D4D4D4u};
    uint32_t opaque_words[3] = {0x10000001u, 4u, 0xE5E5E5E5u};

    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, k101));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, k102));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive_extent(grid, 0, header_words, 3u,
                                                                     0.0f, 16.0f)); /* row 0 */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, k103));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive_extent(grid, 0, body_words, 3u, 32.0f,
                                                                     64.0f)); /* rows 2..3 */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, k104));
    YTEST_REQUIRE_OK(
        test, yetty_yvterm_grid_append_primitive_extent(grid, 0, detached_words, 3u, 0.0f, 16.0f));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, k105));
    YTEST_REQUIRE_OK(test,
                     yetty_yvterm_grid_append_primitive(grid, 0, opaque_words, 3u)); /* no extent */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    struct yetty_ycore_uint32_result declare_res = yetty_yvterm_grid_rich_span_declare(grid, 4u);
    YTEST_REQUIRE_OK(test, declare_res);
    feed_newlines(test, grid, declare_res.value);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_relocate_rich_to_bottom(grid, 4u));
    /* Push 104's content out of the span (span 4 = 64px; y+80 detaches). */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, k104, 0.0f, 80.0f));

    feed_newlines(test, grid, 4); /* cursor walks to the bottom; the last newline
                                   * scrolls one row — live top passes row 0 */

    /* 102 (rows fully above): retired — permanently unaddressable. */
    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_group_offset_set(grid, k102, 1.0f, 1.0f));
    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_group_delete(grid, k102));
    /* 103 (rows 2..3 visible), 101 (union intersects): still live. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, k103, 1.0f, 1.0f));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, k101, 0.0f, 0.0f));
    /* 104 (detached) and 105 (unknown extent): exempt. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, k104, 0.0f, 80.0f));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, k105, 0.0f, 0.0f));

    /* Reopen the live root: the retired 102 subtree is immutable history —
     * spared and still rendered; live descendants are replaced. */
    struct yetty_ycore_uint32_result reopen_res = yetty_yvterm_grid_rich_group_open(grid, 3, k101);
    YTEST_REQUIRE_OK(test, reopen_res);
    YTEST_CHECK_EQ_INT(test, reopen_res.value, 1);
    uint32_t fresh_words[3] = {0x10000001u, 4u, 0xC3C3C3C3u};
    YTEST_REQUIRE_OK(
        test, yetty_yvterm_grid_append_primitive_extent(grid, 3, fresh_words, 3u, 32.0f, 48.0f));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    int header_alive = 0;
    int body_alive = 0;
    int fresh_alive = 0;
    const uint32_t *window = view_window(test, grid, 0, 1, 8);
    uint32_t record_count = 0;
    YTEST_REQUIRE_OK(test,
                     yetty_yvterm_grid_slot_rich_block(grid, window[2], 0u, NULL, &record_count));
    for (uint32_t record_index = 0; record_index < record_count; ++record_index) {
        uint32_t word_count = 0;
        struct yetty_ycore_const_uint32_ptr_result words_res =
            yetty_yvterm_grid_slot_rich_block_record(grid, window[2], 0u, record_index, &word_count,
                                                     NULL);
        YTEST_REQUIRE_OK(test, words_res);
        if (!words_res.value || word_count != 3u) {
            continue;
        }
        if (words_res.value[2] == 0xA1A1A1A1u) {
            header_alive = 1;
        } else if (words_res.value[2] == 0xB2B2B2B2u) {
            body_alive = 1;
        } else if (words_res.value[2] == 0xC3C3C3C3u) {
            fresh_alive = 1;
        }
    }
    YTEST_CHECK(test, header_alive); /* frozen history survives the reopen */
    YTEST_CHECK(test, !body_alive);  /* live descendant content was replaced */
    YTEST_CHECK(test, fresh_alive);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Group CLIP state (contract §1a): set on a live group, validated, and the
 * plan leaf's resolved clip is the ANCESTOR INTERSECTION with offsets
 * applied; leaves without any clipped ancestor stay unclipped.
 *-------------------------------------------------------------------------*/
static void test_group_clip_state(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    uint64_t outer = gkey(111);
    uint64_t inner = yetty_yvterm_group_key_fold(outer, 5);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, outer));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, inner));
    anchor_figure_at_cursor(test, grid, &fixture);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    uint32_t loose_words[3] = {0x10000001u, 4u, 0x77777777u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, loose_words, 3u));

    YTEST_REQUIRE_ERR(test,
                      yetty_yvterm_grid_rich_group_clip_set(grid, outer, NAN, 0.0f, 1.0f, 1.0f));
    YTEST_REQUIRE_ERR(test,
                      yetty_yvterm_grid_rich_group_clip_set(grid, outer, 0.0f, 0.0f, -1.0f, 1.0f));
    YTEST_REQUIRE_ERR(
        test, yetty_yvterm_grid_rich_group_clip_set(grid, gkey(9999), 0.0f, 0.0f, 10.0f, 10.0f));

    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, outer, 10.0f, 20.0f));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, inner, 5.0f, 5.0f));
    YTEST_REQUIRE_OK(test,
                     yetty_yvterm_grid_rich_group_clip_set(grid, outer, 0.0f, 0.0f, 100.0f, 50.0f));
    YTEST_REQUIRE_OK(test,
                     yetty_yvterm_grid_rich_group_clip_set(grid, inner, 0.0f, 0.0f, 40.0f, 40.0f));

    struct yetty_ycore_uint32_result count_res = yetty_yvterm_grid_paint_plan_leaf_count(grid);
    YTEST_REQUIRE_OK(test, count_res);
    int checked_clipped = 0;
    int checked_unclipped = 0;
    for (uint32_t leaf = 0; leaf < count_res.value; ++leaf) {
        int valid = 0;
        float clip_x = 0.0f, clip_y = 0.0f, clip_w = 0.0f, clip_h = 0.0f;
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_paint_plan_leaf_clip(grid, leaf, &valid, &clip_x,
                                                                      &clip_y, &clip_w, &clip_h));
        uint32_t kind = 0;
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_paint_plan_leaf(grid, leaf, NULL, NULL, &kind,
                                                                 NULL, NULL, NULL));
        if (kind == 1u) {
            YTEST_CHECK_EQ_INT(test, valid, 1);
            YTEST_CHECK(test, clip_x == 15.0f && clip_y == 25.0f);
            YTEST_CHECK(test, clip_w == 40.0f && clip_h == 40.0f);
            checked_clipped = 1;
        } else if (!valid) {
            checked_unclipped = 1;
        }
    }
    YTEST_CHECK(test, checked_clipped);
    YTEST_CHECK(test, checked_unclipped);
    yetty_yvterm_grid_dispose(grid);
}

/* Anchor one FAKE complex inside group `group_key` with the given content
 * extent, bind it at fold(group_key, 9), then declare/advance/relocate a
 * `span_rows` block — the receiver-side shape of one hosted figure. */
static uint64_t anchor_extent_figure(struct ytest *test, struct yetty_yclass_object *grid,
                                     struct rich_fixture *fixture, uint64_t group_key,
                                     float extent_top_px, float extent_bottom_px,
                                     uint32_t span_rows)
{
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, group_key));
    uint32_t envelope[4] = {FAKE_COMPLEX_TYPE, 8u, 0xaaaaaaaau, 0xbbbbbbbbu};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive_extent(
                               grid, 0, envelope, 4u, extent_top_px, extent_bottom_px));
    struct yetty_ydraw_complex *instance = fake_complex_create(&fixture->destroy_count);
    YTEST_REQUIRE_NOT_NULL(test, instance);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_attach_complex(grid, 0, instance));
    uint64_t complex_key = yetty_yvterm_group_key_fold(group_key, 9);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_bind(grid, complex_key));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    struct yetty_ycore_uint32_result declare_res =
        yetty_yvterm_grid_rich_span_declare(grid, span_rows);
    YTEST_REQUIRE_OK(test, declare_res);
    feed_newlines(test, grid, declare_res.value);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_relocate_rich_to_bottom(grid, span_rows));
    return complex_key;
}

/*---------------------------------------------------------------------------
 * Extent refresh after a complex geometry mutation (UC-12 coherence): the
 * retained scroll-retirement footprint follows the runtime's CURRENT
 * effective AABB, so retirement — and the ancestor group's subtree union —
 * judges what is actually drawn, not the creation size. GROW keeps the
 * node addressable exactly one extra scrolled row; SHRINK retires it one
 * row earlier. The refresh rejects non-complex keys and bad extents.
 *-------------------------------------------------------------------------*/
static void test_update_extent_refresh_moves_retirement(struct ytest *test)
{
    /* GROW: created covering row 0 (0..16px) in a 2-row block, refreshed
     * to rows 0..1 (0..32px) — the runtime grew on a geometry op. */
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_set_pixel_size(grid, 20u * 8u, 8u * 16u));
    uint64_t k301 = gkey(301);
    uint64_t grow_key = anchor_extent_figure(test, grid, &fixture, k301, 0.0f, 16.0f, 2u);

    /* Guards: a group key is not a complex; a non-finite extent rejects. */
    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_update_extent_refresh(grid, k301, 0.0f, 32.0f));
    YTEST_REQUIRE_ERR(test,
                      yetty_yvterm_grid_rich_update_extent_refresh(grid, grow_key, 0.0f, INFINITY));
    YTEST_REQUIRE_OK(test,
                     yetty_yvterm_grid_rich_update_extent_refresh(grid, grow_key, 0.0f, 32.0f));

    /* Live top passes the block's row 0: the OLD footprint (row 0 only)
     * would retire here; the refreshed one (rows 0..1) stays live. */
    feed_newlines(test, grid, 6);
    struct yetty_ydraw_complex *target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, grow_key, &target));
    YTEST_CHECK(test, target != NULL);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, k301, 0.0f, 0.0f));
    /* One more scrolled row passes the refreshed bottom: NOW it retires,
     * and the ancestor group's union follows the same footprint. */
    feed_newlines(test, grid, 1);
    target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, grow_key, &target));
    YTEST_CHECK(test, target == NULL);
    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_group_offset_set(grid, k301, 0.0f, 0.0f));
    yetty_yvterm_grid_dispose(grid);

    /* SHRINK: created covering rows 0..1, refreshed down to row 0 — the
     * node retires as soon as row 0 leaves, though the CREATION footprint
     * would have kept it addressable. */
    struct rich_fixture shrink_fixture = {0};
    grid = make_grid(test, 20, 8, 16);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_set_pixel_size(grid, 20u * 8u, 8u * 16u));
    uint64_t k302 = gkey(302);
    uint64_t shrink_key = anchor_extent_figure(test, grid, &shrink_fixture, k302, 0.0f, 32.0f, 2u);
    YTEST_REQUIRE_OK(test,
                     yetty_yvterm_grid_rich_update_extent_refresh(grid, shrink_key, 0.0f, 16.0f));
    feed_newlines(test, grid, 6); /* live top passes row 0 */
    target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, shrink_key, &target));
    YTEST_CHECK(test, target == NULL);
    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_group_offset_set(grid, k302, 0.0f, 0.0f));
    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Receiver-local chrome replacement keeps the figure's ORIGINAL paint-z:
 * the bound complex's effective z is readable by key, and a replacement
 * performed under that z pushed as the ambient scope stamps every fresh
 * record at the original depth — regardless of the (scope-less) update
 * envelope that triggered it. Mirrors terminal_ydraw_local_rechrome.
 *-------------------------------------------------------------------------*/
static void test_local_replacement_keeps_paint_z(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    /* Creation under ambient z = 1000 (the overlay band): the figure
     * group holds the complex + a chrome subgroup with one label prim. */
    uint64_t k501 = gkey(501);
    uint64_t chrome_key = yetty_yvterm_group_key_fold(k501, 5);
    uint64_t complex_key = yetty_yvterm_group_key_fold(k501, 9);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_push_paint_z(grid, 1000));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, k501));
    uint32_t envelope[4] = {FAKE_COMPLEX_TYPE, 8u, 0xaaaaaaaau, 0xbbbbbbbbu};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, envelope, 4u));
    struct yetty_ydraw_complex *instance = fake_complex_create(&fixture.destroy_count);
    YTEST_REQUIRE_NOT_NULL(test, instance);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_attach_complex(grid, 0, instance));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_bind(grid, complex_key));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, chrome_key));
    uint32_t old_label[3] = {0x10000001u, 4u, 0xA1A1A1A1u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, old_label, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_pop_paint_z(grid));

    /* The figure's effective depth is readable by its update key; group
     * keys are rejected. */
    int32_t figure_z = 0;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_paint_z(grid, complex_key, &figure_z));
    YTEST_CHECK_EQ_INT(test, figure_z, 1000);
    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_update_paint_z(grid, chrome_key, &figure_z));

    /* Capacity staging: reserving on the live chrome group succeeds; an
     * unknown key is rejected before touching anything; the counter /
     * ordinal preflight rejects an overflowing request up front. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_reserve(grid, chrome_key, 4u, 64u));
    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_group_reserve(grid, gkey(999), 1u, 8u));
    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_group_reserve(grid, chrome_key, UINT32_MAX, 8u));

    /* The bounded ambient stack reports overflow instead of silently
     * absorbing a push — a caller whose z MUST take effect can abort,
     * and its BALANCING pop restores the incoming scopes exactly: after
     * the failed push + its pop, the innermost accepted z (7) still
     * stamps appended records, and the eight real pops then return to
     * wire z. This is the sequence local rechrome performs on overflow. */
    for (uint32_t depth = 0; depth < 8u; ++depth) {
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_push_paint_z(grid, (int32_t)depth));
    }
    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_push_paint_z(grid, 42));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_pop_paint_z(grid)); /* balances the reject */
    uint32_t depth_probe[3] = {0x10000001u, 4u, 0xD7D7D7D7u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, depth_probe, 3u));
    for (uint32_t depth = 0; depth < 8u; ++depth) {
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_pop_paint_z(grid));
    }
    {
        const uint32_t *probe_window = view_window(test, grid, 0, 0, 8);
        struct yetty_ycore_uint32_result block_count_res =
            yetty_yvterm_grid_slot_rich_block_count(grid, probe_window[0]);
        YTEST_REQUIRE_OK(test, block_count_res);
        int probe_seen = 0;
        for (uint32_t block_index = 0; block_index < block_count_res.value; ++block_index) {
            uint32_t probe_count = 0;
            YTEST_REQUIRE_OK(test, yetty_yvterm_grid_slot_rich_block(
                                       grid, probe_window[0], block_index, NULL, &probe_count));
            for (uint32_t record_index = 0; record_index < probe_count; ++record_index) {
                uint32_t word_count = 0;
                struct yetty_ycore_const_uint32_ptr_result words_res =
                    yetty_yvterm_grid_slot_rich_block_record(grid, probe_window[0], block_index,
                                                             record_index, &word_count, NULL);
                YTEST_REQUIRE_OK(test, words_res);
                if (words_res.value && word_count == 3u && words_res.value[2] == 0xD7D7D7D7u) {
                    int32_t probe_z = 0;
                    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_slot_rich_block_record_paint_key(
                                               grid, probe_window[0], block_index, record_index,
                                               &probe_z, NULL, NULL));
                    YTEST_CHECK_EQ_INT(test, probe_z, 7); /* innermost ACCEPTED z */
                    probe_seen = 1;
                }
            }
        }
        YTEST_CHECK(test, probe_seen);
    }

    /* LOCAL replacement, outside any envelope scope: push the figure's
     * z, reopen the chrome group, append the fresh label, pop. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_push_paint_z(grid, figure_z));
    struct yetty_ycore_uint32_result reopen_res =
        yetty_yvterm_grid_rich_group_open(grid, 0, chrome_key);
    YTEST_REQUIRE_OK(test, reopen_res);
    YTEST_CHECK_EQ_INT(test, reopen_res.value, 1);
    uint32_t fresh_label[3] = {0x10000001u, 4u, 0xC3C3C3C3u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, fresh_label, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_pop_paint_z(grid));

    /* The old label is dead; the replacement carries z = 1000. */
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    int found_fresh = 0;
    int found_old = 0;
    uint32_t record_count = 0;
    YTEST_REQUIRE_OK(test,
                     yetty_yvterm_grid_slot_rich_block(grid, window[0], 0u, NULL, &record_count));
    for (uint32_t record_index = 0; record_index < record_count; ++record_index) {
        uint32_t word_count = 0;
        struct yetty_ycore_const_uint32_ptr_result words_res =
            yetty_yvterm_grid_slot_rich_block_record(grid, window[0], 0u, record_index, &word_count,
                                                     NULL);
        YTEST_REQUIRE_OK(test, words_res);
        if (!words_res.value || word_count != 3u) {
            continue;
        }
        if (words_res.value[2] == 0xC3C3C3C3u) {
            int32_t paint_z = 0;
            YTEST_REQUIRE_OK(test, yetty_yvterm_grid_slot_rich_block_record_paint_key(
                                       grid, window[0], 0u, record_index, &paint_z, NULL, NULL));
            YTEST_CHECK_EQ_INT(test, paint_z, 1000);
            found_fresh = 1;
        } else if (words_res.value[2] == 0xA1A1A1A1u) {
            found_old = 1;
        }
    }
    YTEST_CHECK(test, found_fresh);
    YTEST_CHECK(test, !found_old);
    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Live replacement reclamation: repeated in-place group reopens (chrome
 * rebuilds, skin repaints) must NOT grow the block's retained record table
 * with the number of updates — dead generations are compacted while the
 * insertion stays live, and the COMPLEX binding that addresses its record
 * by index survives every remap.
 *-------------------------------------------------------------------------*/
static void test_replacement_storage_stays_bounded(struct ytest *test)
{
    struct rich_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);

    /* One batch: the replaceable "chrome" group + a bound complex in a
     * sibling group of the SAME block. */
    uint64_t chrome_key = gkey(701);
    uint64_t figure_group_key = gkey(702);
    uint64_t complex_key = gkey(703);
    uint32_t label_words[3] = {0x10000001u, 4u, 0xA1A1A1A1u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, chrome_key));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, label_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, figure_group_key));
    uint32_t envelope[4] = {FAKE_COMPLEX_TYPE, 8u, 0xaaaaaaaau, 0xbbbbbbbbu};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, envelope, 4u));
    struct yetty_ydraw_complex *instance = fake_complex_create(&fixture.destroy_count);
    YTEST_REQUIRE_NOT_NULL(test, instance);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_attach_complex(grid, 0, instance));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_bind(grid, complex_key));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    /* Hundreds of replacements, each appending a fresh 3-record chrome
     * generation over the dead one. */
    for (uint32_t round = 0; round < 400u; ++round) {
        struct yetty_ycore_uint32_result reopen_res =
            yetty_yvterm_grid_rich_group_open(grid, 0, chrome_key);
        YTEST_REQUIRE_OK(test, reopen_res);
        for (uint32_t prim = 0; prim < 3u; ++prim) {
            uint32_t fresh_words[3] = {0x10000001u, 4u, 0xC3C30000u + round * 4u + prim};
            YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, fresh_words, 3u));
        }
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    }

    /* Physical retention stays near the LIVE content size (5 live records
     * here), not the update count: 400 x 3-record generations would have
     * retained 1200+ records without live compaction. */
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    uint32_t record_count = 0;
    YTEST_REQUIRE_OK(test,
                     yetty_yvterm_grid_slot_rich_block(grid, window[0], 0u, NULL, &record_count));
    YTEST_CHECK(test, record_count < 80u);

    /* The complex binding survived every compaction remap. */
    struct yetty_ydraw_complex *target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, complex_key, &target));
    YTEST_CHECK(test, target == instance);
    /* And the LAST chrome generation is what the block holds. */
    int found_last = 0;
    for (uint32_t record_index = 0; record_index < record_count; ++record_index) {
        uint32_t word_count = 0;
        struct yetty_ycore_const_uint32_ptr_result words_res =
            yetty_yvterm_grid_slot_rich_block_record(grid, window[0], 0u, record_index, &word_count,
                                                     NULL);
        YTEST_REQUIRE_OK(test, words_res);
        if (words_res.value && word_count == 3u &&
            words_res.value[2] == 0xC3C30000u + 399u * 4u + 2u) {
            found_last = 1;
        }
    }
    YTEST_CHECK(test, found_last);
    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Live topology reclamation: a structural reopen kills descendant GROUPS
 * and recreates them each round (the pre-E5 ancestor-reopen fallback).
 * Dead group slots are recycled (with generations guarding stale
 * bindings), records/arena compact, and the binding sweep purges dead
 * entries — so a constant live tree stays constant-sized however many
 * frames replace it, and the recreated children stay addressable.
 *-------------------------------------------------------------------------*/
static void test_reopen_topology_stays_bounded(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    uint64_t parent_key = gkey(801);
    uint64_t child_a_key = yetty_yvterm_group_key_fold(parent_key, 2);
    uint64_t child_b_key = yetty_yvterm_group_key_fold(parent_key, 3);
    uint32_t label_words[3] = {0x10000001u, 4u, 0xA1A1A1A1u};

    for (uint32_t round = 0; round < 300u; ++round) {
        struct yetty_ycore_uint32_result open_res =
            yetty_yvterm_grid_rich_group_open(grid, 0, parent_key);
        YTEST_REQUIRE_OK(test, open_res);
        YTEST_CHECK_EQ_INT(test, (int)open_res.value, round == 0 ? 0 : 1); /* create then reopen */
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, child_a_key));
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, label_words, 3u));
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, child_b_key));
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, label_words, 3u));
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
        /* The recreated children resolve at their stable paths. */
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, child_a_key, 1.0f,
                                                                       (float)(round % 7u)));
        YTEST_REQUIRE_OK(test,
                         yetty_yvterm_grid_rich_group_offset_set(grid, child_b_key, 2.0f, 0.0f));
    }

    /* 300 rounds x (2 groups + 2 records) of churn: physical retention
     * must sit near the LIVE tree size, not the round count. */
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    uint32_t record_count = 0;
    uint32_t group_count = 0;
    uint32_t arena_words = 0;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_slot_rich_block_stats(
                               grid, window[0], 0u, &record_count, &group_count, &arena_words));
    YTEST_CHECK(test, record_count < 80u); /* 600+ without reclamation */
    YTEST_CHECK(test, group_count < 16u);  /* 600+ without slot reuse */
    YTEST_CHECK(test, arena_words < 400u); /* 1800+ without compaction */
    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Record-LESS replacement churn: reopening a parent whose children are
 * pure groups with FRESH external ids each round creates no dead
 * drawable records — so the record-compaction sweep never fires. The
 * binding table's density trigger must sweep the stale descendant
 * bindings instead (growth-time validation), and dead group slots must
 * still recycle: occupancy, capacity and group slots all stay near the
 * LIVE size after hundreds of rounds.
 *-------------------------------------------------------------------------*/
static void test_recordless_churn_stays_bounded(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    uint64_t parent_key = gkey(901);

    for (uint32_t round = 0; round < 400u; ++round) {
        struct yetty_ycore_uint32_result open_res =
            yetty_yvterm_grid_rich_group_open(grid, 0, parent_key);
        YTEST_REQUIRE_OK(test, open_res);
        /* A FRESH child id every round: the previous round's child binding
         * goes stale with no dead record anywhere. */
        uint64_t child_key = yetty_yvterm_group_key_fold(parent_key, 1000u + round);
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, child_key));
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
        YTEST_REQUIRE_OK(test,
                         yetty_yvterm_grid_rich_group_offset_set(grid, child_key, 1.0f, 1.0f));
    }

    uint32_t binding_live = 0;
    uint32_t binding_capacity = 0;
    YTEST_REQUIRE_OK(
        test, yetty_yvterm_grid_rich_binding_occupancy(grid, &binding_live, &binding_capacity));
    /* Live: the parent + the last round's child + slack; capacity must
     * sit near that, not near the 400 stale ids ever bound. */
    YTEST_CHECK(test, binding_live < 8u);
    YTEST_CHECK(test, binding_capacity < 64u);

    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    uint32_t group_count = 0;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_slot_rich_block_stats(grid, window[0], 0u, NULL,
                                                                   &group_count, NULL));
    YTEST_CHECK(test, group_count < 8u); /* 400+ without slot recycling */
    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Deleting the OPEN scope's own path and then opening a nested child in
 * the same batch: the recycler must never hand the dead-but-still-open
 * slot to its own child (a self-parent cycle would silently swallow the
 * subtree and corrupt the slot's identity). The child is legitimately
 * dead-chained (its parent was deleted — bogus wire, absorbed per
 * command); the grid stays intact and the path recovers with a fresh
 * create afterwards.
 *-------------------------------------------------------------------------*/
static void test_delete_open_scope_then_nested_open(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    uint64_t throwaway_key = gkey(950);
    uint64_t parent_key = gkey(951);
    uint64_t child_key = yetty_yvterm_group_key_fold(parent_key, 2);
    uint32_t label_words[3] = {0x10000001u, 4u, 0xA1A1A1A1u};

    /* Arm the recycler: leave one dead, unreferenced slot behind. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, throwaway_key));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_delete(grid, throwaway_key));

    /* The pathological batch: delete the open scope, then nest under it. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, parent_key));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_delete(grid, parent_key));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, child_key));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, label_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    /* The child is dead-chained (deleted parent) — unaddressable, but a
     * clean absorb, not corruption. */
    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_group_offset_set(grid, child_key, 1.0f, 1.0f));

    /* The path RECOVERS: a fresh create at the same key works and its
     * content is alive and addressable. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, parent_key));
    uint32_t fresh_words[3] = {0x10000001u, 4u, 0xC3C3C3C3u};
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, fresh_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_offset_set(grid, parent_key, 2.0f, 2.0f));
    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * The RECORDLESS variant of the pathological batch: an EMPTY child opened
 * under the deleted-while-open scope anchors the dead chain exactly like
 * a record would — the dead ancestor's slot must stay unreclaimable, or
 * an unrelated later open recycles it, the empty child's parent_slot
 * suddenly names live foreign structure, and the deleted path becomes
 * addressable again beneath it. (The record-appending variant above never
 * caught this: the append's own dead-chain guard hid the missing
 * group-open guard.)
 *-------------------------------------------------------------------------*/
static void test_delete_open_scope_recordless_child_stays_dead(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    uint64_t throwaway_key = gkey(955);
    uint64_t parent_key = gkey(956);
    uint64_t child_key = yetty_yvterm_group_key_fold(parent_key, 2);
    uint64_t unrelated_key = gkey(957);
    uint32_t label_words[3] = {0x10000001u, 4u, 0xA5A5A5A5u};

    /* Arm the recycler cache: one dead, unreferenced slot. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, throwaway_key));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_delete(grid, throwaway_key));

    /* Delete the open scope, then open an EMPTY nested child — no record
     * anywhere in the batch. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, parent_key));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_delete(grid, parent_key));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, child_key));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    /* NO probe of the child here: a failed lookup lazily drops the
     * binding, which would hide the resurrection by killing the address
     * before the recycle. The reviewer's minimal sequence goes straight
     * to the unrelated open. */

    /* Unrelated root group: must NOT recycle the deleted ancestor's slot
     * into a live parent for the orphaned child. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, unrelated_key));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, label_words, 3u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));

    /* The deleted path must STILL be dead — the resurrection bug made
     * this succeed once the ancestor slot was reused. */
    YTEST_REQUIRE_ERR(test, yetty_yvterm_grid_rich_group_offset_set(grid, child_key, 1.0f, 1.0f));
    /* And the unrelated group is undamaged and addressable. */
    YTEST_REQUIRE_OK(test,
                     yetty_yvterm_grid_rich_group_offset_set(grid, unrelated_key, 2.0f, 2.0f));
    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * The delete + reinsert loop (delete(a); insert(a){ b{...} }) must stay
 * bounded: a kill marks DESCENDANT groups dead too — an alive-flagged
 * husk under a dead ancestor would pin both slots against recycling
 * forever, growing the group table with the round count and re-running
 * a reclaim-nothing scan on every open.
 *-------------------------------------------------------------------------*/
static void test_delete_reinsert_topology_stays_bounded(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    uint64_t parent_key = gkey(961);
    uint64_t child_key = yetty_yvterm_group_key_fold(parent_key, 2);
    uint32_t label_words[3] = {0x10000001u, 4u, 0xB2B2B2B2u};

    for (uint32_t round = 0; round < 300u; ++round) {
        if (round) {
            YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_delete(grid, parent_key));
        }
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, parent_key));
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, child_key));
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, label_words, 3u));
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
        YTEST_REQUIRE_OK(test,
                         yetty_yvterm_grid_rich_group_offset_set(grid, child_key, 1.0f, 1.0f));
    }

    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    uint32_t record_count = 0;
    uint32_t group_count = 0;
    uint32_t arena_words = 0;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_slot_rich_block_stats(
                               grid, window[0], 0u, &record_count, &group_count, &arena_words));
    YTEST_CHECK(test, group_count < 8u); /* 600+ without the descendant sweep */
    YTEST_CHECK(test, record_count < 80u);
    YTEST_CHECK(test, arena_words < 400u);
    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Recycler COMPLEXITY pin: an exact-subtree replacement window pays ONE
 * reachability scan however many descendants it recreates — wide (many
 * siblings) and deep (a long chain) alike. The scan counter must stay
 * proportional to replacement WINDOWS (plus compaction rescans), never
 * to recreated NODES: without the cached candidate set every recreated
 * group ran its own full mark scan, making structural reopens quadratic.
 *-------------------------------------------------------------------------*/
static void test_replacement_scan_cost_stays_windowed(struct ytest *test)
{
    enum { CHURN_ROUNDS = 40, WIDE_CHILDREN = 24, DEEP_CHAIN = 6 };
    uint32_t label_words[3] = {0x10000001u, 4u, 0xB2B2B2B2u};

    /* WIDE: one parent reopened per round, recreating 24 stable-key
     * children with one record each. */
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    uint64_t wide_parent = gkey(951);
    for (uint32_t round = 0; round < CHURN_ROUNDS; ++round) {
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, wide_parent));
        for (uint32_t child = 0; child < WIDE_CHILDREN; ++child) {
            uint64_t child_key = yetty_yvterm_group_key_fold(wide_parent, 2u + child);
            YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(grid, 0, child_key));
            YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(grid, 0, label_words, 3u));
            YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
        }
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(grid));
    }
    const uint32_t *window = view_window(test, grid, 0, 0, 8);
    uint64_t wide_scans = 0;
    uint32_t wide_groups = 0;
    YTEST_REQUIRE_OK(test,
                     yetty_yvterm_grid_slot_rich_reusable_scans(grid, window[0], 0u, &wide_scans));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_slot_rich_block_stats(grid, window[0], 0u, NULL,
                                                                   &wide_groups, NULL));
    /* <= one rebuild per replacement window + one per compaction rescan;
     * the uncached recycler performed ~ROUNDS x WIDTH (960) scans. */
    YTEST_CHECK(test, wide_scans <= 2u * CHURN_ROUNDS);
    YTEST_CHECK(test, wide_groups < WIDE_CHILDREN + 8u); /* reuse still works */
    yetty_yvterm_grid_dispose(grid);

    /* DEEP: one parent reopened per round, recreating a 6-deep chain
     * (the grid scope stack holds 8) with a record at the bottom. */
    struct yetty_yclass_object *deep_grid = make_grid(test, 20, 8, 16);
    uint64_t deep_parent = gkey(952);
    for (uint32_t round = 0; round < CHURN_ROUNDS; ++round) {
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(deep_grid, 0, deep_parent));
        uint64_t chain_key = deep_parent;
        for (uint32_t level = 0; level < DEEP_CHAIN; ++level) {
            chain_key = yetty_yvterm_group_key_fold(chain_key, 3u);
            YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_open(deep_grid, 0, chain_key));
        }
        YTEST_REQUIRE_OK(test, yetty_yvterm_grid_append_primitive(deep_grid, 0, label_words, 3u));
        for (uint32_t level = 0; level <= DEEP_CHAIN; ++level) {
            YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_group_close(deep_grid));
        }
    }
    const uint32_t *deep_window = view_window(test, deep_grid, 0, 0, 8);
    uint64_t deep_scans = 0;
    uint32_t deep_groups = 0;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_slot_rich_reusable_scans(deep_grid, deep_window[0], 0u,
                                                                      &deep_scans));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_slot_rich_block_stats(deep_grid, deep_window[0], 0u,
                                                                   NULL, &deep_groups, NULL));
    YTEST_CHECK(test, deep_scans <= 2u * CHURN_ROUNDS);
    YTEST_CHECK(test, deep_groups < DEEP_CHAIN + 8u);
    yetty_yvterm_grid_dispose(deep_grid);
}

/*---------------------------------------------------------------------------
 * Rich transform CONTRACT under density x structural cell zoom: the row
 * anchor lives in CURRENT framebuffer cells; ONLY the primitive-local
 * producer space scales (by density x cell zoom). Bucketing (the shared
 * yetty_yvterm_sdf_prim_cell_span formula), the shader's inverse sample
 * transform, and complex placement all use that one factor — so an SDF
 * primitive and a complex in the same group at a nonzero rolling row
 * with a nonzero group offset translate identically, a multi-cell
 * drawable's bucket range is complete and zoom-invariant, and a shaped
 * TERMINAL glyph run (framebuffer-space text) follows the text grid
 * exactly once with no rich conversion.
 *-------------------------------------------------------------------------*/
static void test_rich_transform_contract(struct ytest *test)
{
    /* World: baseline cell 8x16; density 2 AND structural zoom 2 — the
     * cells re-derive to 32x64 and the rich local scale is 4. */
    const float density = 2.0f;
    const float cell_zoom = 2.0f;
    const float local_scale = density * cell_zoom;
    const float cell_w = 8.0f * density * cell_zoom;
    const float cell_h = 16.0f * density * cell_zoom;
    const int32_t rolling_row = 3;
    const float offset_x = 10.0f;
    const float offset_y = 6.0f;
    /* A drawable crossing multiple cells: local (producer) x 2..70, y 4..44. */
    const float aabb_min_x = 2.0f;
    const float aabb_max_x = 70.0f;
    const float aabb_min_y = 4.0f;
    const float aabb_max_y = 44.0f;

    int32_t col_min = 0;
    int32_t col_max = 0;
    int32_t row_span_min = 0;
    int32_t row_span_max = 0;
    yetty_yvterm_sdf_prim_cell_span(aabb_min_x, aabb_max_x, offset_x, local_scale, cell_w,
                                    &col_min, &col_max);
    yetty_yvterm_sdf_prim_cell_span(aabb_min_y, aabb_max_y, offset_y, local_scale, cell_h,
                                    &row_span_min, &row_span_max);
    /* COMPLETE range: (2+10)*4/32 → 1 .. (70+10)*4/32 → 10;
     * rows (4+6)*4/64 → 0 .. (44+6)*4/64 → 3 (anchored at row 3). */
    YTEST_CHECK_EQ_INT(test, (int)col_min, 1);
    YTEST_CHECK_EQ_INT(test, (int)col_max, 10);
    YTEST_CHECK_EQ_INT(test, (int)(rolling_row + row_span_min), 3);
    YTEST_CHECK_EQ_INT(test, (int)(rolling_row + row_span_max), 6);

    /* ZOOM/DENSITY INVARIANCE: the same drawable on the baseline metrics
     * covers the SAME cells — scaling moves pixels, never cell coverage
     * (the broken transform halved the bucket range at 2x). */
    int32_t base_col_min = 0;
    int32_t base_col_max = 0;
    int32_t base_row_min = 0;
    int32_t base_row_max = 0;
    yetty_yvterm_sdf_prim_cell_span(aabb_min_x, aabb_max_x, offset_x, 1.0f, 8.0f, &base_col_min,
                                    &base_col_max);
    yetty_yvterm_sdf_prim_cell_span(aabb_min_y, aabb_max_y, offset_y, 1.0f, 16.0f, &base_row_min,
                                    &base_row_max);
    YTEST_CHECK_EQ_INT(test, (int)base_col_min, (int)col_min);
    YTEST_CHECK_EQ_INT(test, (int)base_col_max, (int)col_max);
    YTEST_CHECK_EQ_INT(test, (int)base_row_min, (int)row_span_min);
    YTEST_CHECK_EQ_INT(test, (int)base_row_max, (int)row_span_max);

    /* SDF <-> COMPLEX parity at the same (row, group offset): complex
     * placement anchors at row*cell + offset*local_scale (vterm.c); the
     * SDF projection puts local y=0 at row*cell + (0+offset)*local_scale
     * — identical translation, one shared factor. */
    float complex_anchor_y = (float)rolling_row * cell_h + offset_y * local_scale;
    float sdf_zero_y = (float)rolling_row * cell_h + (0.0f + offset_y) * local_scale;
    YTEST_CHECK(test, complex_anchor_y == sdf_zero_y);

    /* SHADER COHERENCE: the shader's inverse transform
     * content = (pixel - row_anchor) / local_scale recovers the local
     * coordinate exactly, and the SAMPLED cell of every AABB corner
     * falls inside the BUCKETED range (the broken transform sampled far
     * from the bucketed cells at any nonzero row). */
    const float corners_y[2] = {aabb_min_y, aabb_max_y};
    const int32_t corner_rows[2] = {row_span_min, row_span_max};
    for (uint32_t corner = 0; corner < 2; ++corner) {
        float pixel_y =
            (float)rolling_row * cell_h + (corners_y[corner] + offset_y) * local_scale;
        float content_y = (pixel_y - (float)rolling_row * cell_h) / local_scale;
        YTEST_CHECK_NEAR(test, content_y - offset_y, corners_y[corner], 1e-4);
        int32_t sampled_row = (int32_t)floorf(pixel_y / cell_h);
        YTEST_CHECK_EQ_INT(test, (int)sampled_row, (int)(rolling_row + corner_rows[corner]));
    }

    /* SHAPED TERMINAL RUN at a nonzero row: staged at CURRENT framebuffer
     * cell metrics with local scale 1 — a glyph spanning column 2's cell
     * interior buckets to exactly column 2 (the text grid, exactly once),
     * independent of density and zoom. */
    int32_t term_col_min = 0;
    int32_t term_col_max = 0;
    yetty_yvterm_sdf_prim_cell_span(2.0f * cell_w + 1.0f, 3.0f * cell_w - 1.0f, 0.0f, 1.0f,
                                    cell_w, &term_col_min, &term_col_max);
    YTEST_CHECK_EQ_INT(test, (int)term_col_min, 2);
    YTEST_CHECK_EQ_INT(test, (int)term_col_max, 2);
}

/*---------------------------------------------------------------------------
 * LIVE density transition over an ALREADY-RETAINED block: the committed
 * rich density and the cell metrics move TOGETHER (the production
 * transition re-derives cells at the new density in the same pass), so a
 * retained logical footprint's ROW count is invariant — 24 logical px is
 * rows 0..1 at density 1 x 16px cells AND at density 2 x 32px cells. A
 * stale retained density (the bug) retired one row early on 1->2 and
 * lingered one row late on 2->1.
 *-------------------------------------------------------------------------*/
static void test_density_transition_over_retained_block(struct ytest *test)
{
    /* 1x -> 2x. Insert at density 1 (16px cells), THEN transition. */
    struct rich_fixture up_fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_set_pixel_size(grid, 20u * 8u, 8u * 16u));
    uint64_t up_key = anchor_extent_figure(test, grid, &up_fixture, gkey(321), 0.0f, 24.0f, 2u);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_set_rich_density(grid, 2.0f));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_set_pixel_size(grid, 20u * 16u, 8u * 32u));
    feed_newlines(test, grid, 6); /* row 0 passes */
    struct yetty_ydraw_complex *target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, up_key, &target));
    YTEST_CHECK(test, target != NULL); /* stale density-1 retired it here */
    feed_newlines(test, grid, 1); /* row 1 passes */
    target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, up_key, &target));
    YTEST_CHECK(test, target == NULL);
    yetty_yvterm_grid_dispose(grid);

    /* 2x -> 1x. Insert at density 2 (32px cells), THEN transition down. */
    struct rich_fixture down_fixture = {0};
    grid = make_grid(test, 20, 8, 16);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_set_rich_density(grid, 2.0f));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_set_pixel_size(grid, 20u * 16u, 8u * 32u));
    uint64_t down_key =
        anchor_extent_figure(test, grid, &down_fixture, gkey(322), 0.0f, 24.0f, 2u);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_set_rich_density(grid, 1.0f));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_set_pixel_size(grid, 20u * 8u, 8u * 16u));
    feed_newlines(test, grid, 6); /* row 0 passes — still live */
    target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, down_key, &target));
    YTEST_CHECK(test, target != NULL);
    feed_newlines(test, grid, 1); /* row 1 passes — PERMANENT retirement;
                                   * a stale density 2 kept it addressable
                                   * one extra row (footprint row 2). */
    target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, down_key, &target));
    YTEST_CHECK(test, target == NULL);
    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Retirement row boundary (the ceil rule): last footprint row =
 * ceil(bottom / cell_h) - 1. A bottom EXACTLY on a row boundary belongs
 * to the row above; a hair past it keeps the next row in the footprint —
 * the old fixed absolute epsilon floored that back and retired a
 * still-visible node. A very small positive extent is row 0 (no
 * negative-to-unsigned conversion, no clamp to the last span row).
 *-------------------------------------------------------------------------*/
static void test_retirement_row_boundary(struct ytest *test)
{
    /* bottom == 16.0 (cell 16): footprint is row 0 only — retires as
     * soon as row 0 leaves. */
    struct rich_fixture at_fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 8, 16);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_set_pixel_size(grid, 20u * 8u, 8u * 16u));
    uint64_t at_key = anchor_extent_figure(test, grid, &at_fixture, gkey(311), 0.0f, 16.0f, 2u);
    feed_newlines(test, grid, 6); /* live top passes the block's row 0 */
    struct yetty_ydraw_complex *target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, at_key, &target));
    YTEST_CHECK(test, target == NULL);
    yetty_yvterm_grid_dispose(grid);

    /* bottom just PAST the boundary: row 1 is in the footprint — the
     * node must survive row 0 leaving and retire only with row 1. */
    struct rich_fixture past_fixture = {0};
    grid = make_grid(test, 20, 8, 16);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_set_pixel_size(grid, 20u * 8u, 8u * 16u));
    uint64_t past_key =
        anchor_extent_figure(test, grid, &past_fixture, gkey(312), 0.0f, 16.0005f, 2u);
    feed_newlines(test, grid, 6);
    target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, past_key, &target));
    YTEST_CHECK(test, target != NULL); /* the epsilon bug retired it here */
    feed_newlines(test, grid, 1);
    target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, past_key, &target));
    YTEST_CHECK(test, target == NULL);
    yetty_yvterm_grid_dispose(grid);

    /* bottom just BEFORE the boundary: row 0 only. */
    struct rich_fixture before_fixture = {0};
    grid = make_grid(test, 20, 8, 16);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_set_pixel_size(grid, 20u * 8u, 8u * 16u));
    uint64_t before_key =
        anchor_extent_figure(test, grid, &before_fixture, gkey(313), 0.0f, 15.9995f, 2u);
    feed_newlines(test, grid, 6);
    target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, before_key, &target));
    YTEST_CHECK(test, target == NULL);
    yetty_yvterm_grid_dispose(grid);

    /* HiDPI density 2: footprints are PRODUCER-LOGICAL — a 16-logical-px
     * bottom is 32 framebuffer px (rows 0..1 of 16px cells), so the node
     * must survive row 0 leaving. Unscaled consumption retired it a full
     * row early. */
    struct rich_fixture density_fixture = {0};
    grid = make_grid(test, 20, 8, 16);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_set_pixel_size(grid, 20u * 8u, 8u * 16u));
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_set_rich_density(grid, 2.0f));
    uint64_t density_key =
        anchor_extent_figure(test, grid, &density_fixture, gkey(315), 0.0f, 16.0f, 2u);
    feed_newlines(test, grid, 6);
    target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, density_key, &target));
    YTEST_CHECK(test, target != NULL); /* 32 fb px — row 1 still holds it */
    feed_newlines(test, grid, 1);
    target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, density_key, &target));
    YTEST_CHECK(test, target == NULL);
    yetty_yvterm_grid_dispose(grid);

    /* a VERY SMALL positive extent: row 0 — the old code's
     * (bottom - epsilon) went negative here, and the float-to-unsigned
     * conversion of a negative value is undefined (commonly clamped to
     * the LAST span row, wrongly keeping the node addressable). */
    struct rich_fixture tiny_fixture = {0};
    grid = make_grid(test, 20, 8, 16);
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_set_pixel_size(grid, 20u * 8u, 8u * 16u));
    uint64_t tiny_key =
        anchor_extent_figure(test, grid, &tiny_fixture, gkey(314), 0.0f, 0.0005f, 2u);
    feed_newlines(test, grid, 6);
    target = NULL;
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_rich_update_target(grid, tiny_key, &target));
    YTEST_CHECK(test, target == NULL);
    yetty_yvterm_grid_dispose(grid);
}

int main(void)
{
    struct ytest test = ytest_begin("yvterm_rich_lifecycle");
    YTEST_RUN(&test, test_group_clip_state);
    YTEST_RUN(&test, test_relocate_rehomes_block_to_bottom);
    YTEST_RUN(&test, test_covered_row_write_removes_block);
    YTEST_RUN(&test, test_partial_erase_any_covered_row);
    YTEST_RUN(&test, test_block_seals_when_fully_off);
    YTEST_RUN(&test, test_whole_screen_erase_clears_rich_and_fires_hook);
    YTEST_RUN(&test, test_clear_rich_all_covers_rings_cache_watermark);
    YTEST_RUN(&test, test_alt_screen_exit_drops_alt_rich);
    YTEST_RUN(&test, test_two_envelopes_share_line_storage);
    YTEST_RUN(&test, test_group_delete_removes_subtree);
    YTEST_RUN(&test, test_group_delete_parent_removes_child);
    YTEST_RUN(&test, test_group_reopen_replaces_in_place);
    YTEST_RUN(&test, test_group_binding_dies_on_seal);
    YTEST_RUN(&test, test_region_scroll_moves_contained_block);
    YTEST_RUN(&test, test_update_bind_journal_and_replay);
    YTEST_RUN(&test, test_update_rebind_replaces_previous);
    YTEST_RUN(&test, test_insert_kind_change);
    YTEST_RUN(&test, test_group_offset_state);
    YTEST_RUN(&test, test_full_height_insert_survives_own_reserve);
    YTEST_RUN(&test, test_alt_screen_insert_never_scrolls_itself);
    YTEST_RUN(&test, test_mixed_batch_keeps_provisional_insertion);
    YTEST_RUN(&test, test_region_scroll_during_reserve);
    YTEST_RUN(&test, test_node_retires_when_footprint_leaves);
    YTEST_RUN(&test, test_ring_deep_reserve_keeps_block);
    YTEST_RUN(&test, test_offset_rejects_non_finite);
    YTEST_RUN(&test, test_resize_remaps_block_anchors);
    YTEST_RUN(&test, test_reopen_replaces_whole_subtree);
    YTEST_RUN(&test, test_reopen_preserves_paint_position);
    YTEST_RUN(&test, test_reopen_nested_subtree_keeps_slot);
    YTEST_RUN(&test, test_binding_churn);
    YTEST_RUN(&test, test_failed_resize_keeps_rich);
    YTEST_RUN(&test, test_binding_key_spaces_disjoint);
    YTEST_RUN(&test, test_reopen_rebinds_update_to_replacement);
    YTEST_RUN(&test, test_paint_plan_total_order);
    YTEST_RUN(&test, test_paint_plan_cache_and_zero_sequences);
    YTEST_RUN(&test, test_paint_plan_deep_ring_smoke);
    YTEST_RUN(&test, test_ambient_paint_z_overrides);
    YTEST_RUN(&test, test_update_extent_refresh_moves_retirement);
    YTEST_RUN(&test, test_local_replacement_keeps_paint_z);
    YTEST_RUN(&test, test_replacement_storage_stays_bounded);
    YTEST_RUN(&test, test_reopen_topology_stays_bounded);
    YTEST_RUN(&test, test_recordless_churn_stays_bounded);
    YTEST_RUN(&test, test_delete_open_scope_then_nested_open);
    YTEST_RUN(&test, test_delete_open_scope_recordless_child_stays_dead);
    YTEST_RUN(&test, test_delete_reinsert_topology_stays_bounded);
    YTEST_RUN(&test, test_replacement_scan_cost_stays_windowed);
    YTEST_RUN(&test, test_rich_transform_contract);
    YTEST_RUN(&test, test_density_transition_over_retained_block);
    YTEST_RUN(&test, test_retirement_row_boundary);
    return ytest_end(&test);
}
