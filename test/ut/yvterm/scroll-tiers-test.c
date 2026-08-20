/*
 * yvterm tiered-scrollback contract test — headless, no process fork, no GPU.
 *
 * Exercises the tiered scroll buffer end to end on the grid model:
 *   - hot tier: a figure keeps its runtime while its line stays in the ring;
 *   - age-out: a line recycled off the ring destroys the runtime, serializes
 *     the line (text runs + arena incl. the retained wire envelope) into the
 *     warm tier, and the timeline/floor stay exact;
 *   - resolver: a scrolled-back view window serves archived lines back —
 *     text round-trips bit-identically, figures re-materialize through the
 *     registered hook, and leaving the window destroys them again;
 *   - budgets: a tiny warm budget spills sealed segments to the session
 *     spill file and deep history remains readable from disk; a total line
 *     cap advances the floor by dropping whole segments;
 *   - lazy reflow: resize drains ring history into the archive, so archived
 *     text stays readable afterwards at its stored layout.
 *
 * Complexes are fabricated with a test-local ops table (destroy counts into
 * the fixture); no factory, pipeline, or GPU device is involved.
 */

#include <yetty/ydraw-factory/complex-factory.h>
#include <yetty/api/yvterm/grid.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A complex-tier type id (>= 0x80000000) that no real factory owns. */
#define FAKE_COMPLEX_TYPE 0x80abcdefu

struct tier_fixture {
    int destroy_count;
    int materialize_calls;
    int materialize_fail;
    /* Envelope words the hook last received, for verbatim-retention checks. */
    uint32_t seen_type;
    uint32_t seen_word_count;
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
                                                       uint32_t envelope_word_count, void *userdata,
                                                       struct yetty_ydraw_complex **out_instance)
{
    struct tier_fixture *fixture = userdata;
    fixture->materialize_calls++;
    fixture->seen_type = envelope_word_count ? envelope_words[0] : 0u;
    fixture->seen_word_count = envelope_word_count;
    if (fixture->materialize_fail) {
        return YETTY_ERR(yetty_ycore_void, "fake materialize failure");
    }
    *out_instance = fake_complex_create(&fixture->destroy_count);
    if (!*out_instance) {
        return YETTY_ERR(yetty_ycore_void, "fake complex alloc failed");
    }
    return YETTY_OK_VOID();
}

static struct yetty_yclass_object *make_grid(struct ytest *test, uint32_t cols, uint32_t rows,
                                             uint32_t total_cap, uint32_t hot_rows)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_yvterm_grid_make(cols, rows, total_cap, hot_rows);
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

static uint64_t live_anchor(struct ytest *test, struct yetty_yclass_object *grid)
{
    struct yetty_ycore_uint64_result r = yetty_yvterm_grid_live_anchor(grid);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

static uint64_t history_floor(struct ytest *test, struct yetty_yclass_object *grid)
{
    struct yetty_ycore_uint64_result r = yetty_yvterm_grid_history_floor(grid);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

/* Point the view at timeline line `view_top` and resolve a window of
 * `row_count` rows; returns the grid-owned slot array. */
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

static uint32_t slot_complex_count(struct ytest *test, struct yetty_yclass_object *grid,
                                     uint32_t slot)
{
    uint32_t count = 0;
    struct yetty_ydraw_complex_const_ptr_ptr_result r =
        yetty_yvterm_grid_slot_complexes(grid, slot, &count);
    YTEST_REQUIRE_OK(test, r);
    return count;
}

static uint32_t slot_primitive_count(struct ytest *test, struct yetty_yclass_object *grid,
                                     uint32_t slot)
{
    struct yetty_ycore_uint32_result r = yetty_yvterm_grid_slot_primitive_count(grid, slot);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

static uint32_t slot_codepoint(struct ytest *test, struct yetty_yclass_object *grid, uint32_t slot,
                               uint32_t col)
{
    struct yetty_yvterm_text_cell_const_ptr_result cells = yetty_yvterm_grid_slot_cells(grid, slot);
    YTEST_REQUIRE_OK(test, cells);
    YTEST_REQUIRE_NOT_NULL(test, cells.value);
    return cells.value[col].codepoint;
}

static const struct yetty_yvterm_text_cell *slot_cell(struct ytest *test,
                                                      struct yetty_yclass_object *grid,
                                                      uint32_t slot, uint32_t col)
{
    struct yetty_yvterm_text_cell_const_ptr_result cells = yetty_yvterm_grid_slot_cells(grid, slot);
    YTEST_REQUIRE_OK(test, cells);
    YTEST_REQUIRE_NOT_NULL(test, cells.value);
    return &cells.value[col];
}

/* Anchor one fake figure + its wire envelope on the cursor row, mirroring the
 * ingest order (envelope first, then the instance). Returns the line's
 * timeline index (== rows scrolled so far, since content starts at row 0). */
static uint64_t anchor_figure(struct ytest *test, struct yetty_yclass_object *grid,
                              struct tier_fixture *fixture)
{
    uint32_t row = 0, col = 0, visible = 0;
    struct yetty_ycore_void_result cur = yetty_yvterm_grid_cursor(grid, &row, &col, &visible);
    YTEST_REQUIRE_OK(test, cur);

    uint32_t envelope[4] = {FAKE_COMPLEX_TYPE, 8u, 0x11111111u, 0x22222222u};
    struct yetty_ycore_uint32_result env_res =
        yetty_yvterm_grid_append_primitive(grid, row, envelope, 4u);
    YTEST_REQUIRE_OK(test, env_res);

    struct yetty_ydraw_complex *instance = fake_complex_create(&fixture->destroy_count);
    YTEST_REQUIRE_NOT_NULL(test, instance);
    struct yetty_ycore_uint32_result attach_res =
        yetty_yvterm_grid_attach_complex(grid, row, instance);
    YTEST_REQUIRE_OK(test, attach_res);

    return live_anchor(test, grid) + row;
}

/*---------------------------------------------------------------------------
 * Hot tier: runtime survives while the line is in the ring; leaving the ring
 * destroys it and archives the line (envelope + text included).
 *-------------------------------------------------------------------------*/
static void test_age_out_archives_line(struct ytest *test)
{
    struct tier_fixture fixture = {0};
    /* rows=4, hot=8: ring history capacity 8. Content on timeline line 0 is
     * recycled when total_scrolled crosses 8 — the 12th newline from row 0
     * (the first 3 only move the cursor down). */
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 0, 8);
    feed(test, grid, "figure-line");
    uint32_t figure_line = anchor_figure(test, grid, &fixture);
    YTEST_CHECK_EQ_INT(test, figure_line, 0);

    feed_newlines(test, grid, 11); /* total_scrolled = 8 — still in the ring */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 0);
    YTEST_CHECK_EQ_INT(test, history_floor(test, grid), 0);
    YTEST_CHECK_EQ_INT(test, live_anchor(test, grid), 8);

    feed_newlines(test, grid, 1); /* recycles timeline line 0 into the archive */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);
    YTEST_CHECK_EQ_INT(test, live_anchor(test, grid), 9);
    /* The archive keeps it reachable: the floor must NOT move. */
    YTEST_CHECK_EQ_INT(test, history_floor(test, grid), 0);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Grapheme clusters survive the archive round-trip (#570): a combining cluster
 * aged into warm/cold history must materialize back with every mark intact, not
 * just its base codepoint. Exercises the variable-length mark payload in the
 * tier serialize/inflate path.
 *-------------------------------------------------------------------------*/
static void test_cluster_survives_archive(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 0, 8);
    /* col 0: 'e' + U+0301 acute; col 1: space; col 2: 'a' + U+0301 + U+0323. */
    feed(test, grid, "e\xcc\x81 a\xcc\x81\xcc\xa3");
    feed_newlines(test, grid, 40); /* age timeline line 0 into the archive */

    const uint32_t *window = view_window(test, grid, 1, 0, 4);
    const struct yetty_yvterm_text_cell *acute = slot_cell(test, grid, window[0], 0);
    YTEST_CHECK_EQ_INT(test, acute->codepoint, 'e');
    YTEST_CHECK_EQ_INT(test, acute->mark_count, 1);
    YTEST_CHECK_EQ_INT(test, acute->marks[0], 0x0301);

    const struct yetty_yvterm_text_cell *stacked = slot_cell(test, grid, window[0], 2);
    YTEST_CHECK_EQ_INT(test, stacked->codepoint, 'a');
    YTEST_CHECK_EQ_INT(test, stacked->mark_count, 2);
    YTEST_CHECK_EQ_INT(test, stacked->marks[0], 0x0301);
    YTEST_CHECK_EQ_INT(test, stacked->marks[1], 0x0323);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Resolver: text round-trips bit-identically from the archive; figures
 * re-materialize on view and die when the window leaves.
 *-------------------------------------------------------------------------*/
static void test_view_resolves_archived_lines(struct ytest *test)
{
    struct tier_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 0, 8);
    struct yetty_ycore_void_result hook_res =
        yetty_yvterm_grid_set_materialize(grid, fake_materialize, &fixture);
    YTEST_REQUIRE_OK(test, hook_res);

    feed(test, grid, "Zebra!");
    uint32_t figure_line = anchor_figure(test, grid, &fixture);
    feed_newlines(test, grid, 40); /* deep past the ring */
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);

    /* Window over the archived line: text must round-trip verbatim. */
    const uint32_t *window = view_window(test, grid, 1, figure_line, 4);
    YTEST_CHECK_EQ_INT(test, slot_codepoint(test, grid, window[0], 0), 'Z');
    YTEST_CHECK_EQ_INT(test, slot_codepoint(test, grid, window[0], 5), '!');
    /* The envelope replayed through the hook, verbatim. */
    YTEST_CHECK_EQ_INT(test, fixture.materialize_calls, 1);
    YTEST_CHECK_EQ_INT(test, (int)fixture.seen_type, (int)FAKE_COMPLEX_TYPE);
    YTEST_CHECK_EQ_INT(test, fixture.seen_word_count, 4);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[0]), 1);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[0]), 1);

    /* Same window again: cached — the hook must not re-fire. */
    window = view_window(test, grid, 1, figure_line, 4);
    YTEST_CHECK_EQ_INT(test, fixture.materialize_calls, 1);

    /* Back to the live view: the transient runtime is swept. */
    (void)view_window(test, grid, 0, 0, 4);
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 2);

    /* Second visit re-materializes from the (still cached) segment. */
    window = view_window(test, grid, 1, figure_line, 4);
    YTEST_CHECK_EQ_INT(test, fixture.materialize_calls, 2);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[0]), 1);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * A failing hook must not crash the view pass or consume the envelope.
 *-------------------------------------------------------------------------*/
static void test_materialize_failure_is_absorbed(struct ytest *test)
{
    struct tier_fixture fixture = {0};
    fixture.materialize_fail = 1;
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 0, 8);
    struct yetty_ycore_void_result hook_res =
        yetty_yvterm_grid_set_materialize(grid, fake_materialize, &fixture);
    YTEST_REQUIRE_OK(test, hook_res);

    uint32_t figure_line = anchor_figure(test, grid, &fixture);
    feed_newlines(test, grid, 40);

    const uint32_t *window = view_window(test, grid, 1, figure_line, 4);
    YTEST_CHECK_EQ_INT(test, fixture.materialize_calls, 1);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[0]), 0);
    /* The envelope stays for a later retry. */
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[0]), 1);

    window = view_window(test, grid, 1, figure_line, 4);
    YTEST_CHECK_EQ_INT(test, fixture.materialize_calls, 2);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Warm budget: sealed segments spill to the session file; deep history stays
 * readable from disk and the floor stays 0.
 *-------------------------------------------------------------------------*/
static void test_warm_budget_spills_to_disk(struct ytest *test)
{
    struct tier_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 0, 8);
    /* Warm budget so small every sealed segment must spill. */
    struct yetty_ycore_void_result budget_res = yetty_yvterm_grid_set_tier_budgets(grid, 1, 0);
    YTEST_REQUIRE_OK(test, budget_res);

    feed(test, grid, "deep-history-marker");
    uint32_t marker_line = live_anchor(test, grid);
    YTEST_CHECK_EQ_INT(test, marker_line, 0);

    /* Push well past two segment seals (512 lines each). */
    char line_text[32];
    for (uint32_t index = 0; index < 1200; ++index) {
        int written = snprintf(line_text, sizeof(line_text), "line %u\r\n", index);
        YTEST_REQUIRE(test, written > 0);
        feed(test, grid, line_text);
    }
    YTEST_CHECK_EQ_INT(test, history_floor(test, grid), 0);
    YTEST_CHECK(test, live_anchor(test, grid) > 1100);

    /* Deep view: the marker line must come back from the spill file. */
    const uint32_t *window = view_window(test, grid, 1, 0, 4);
    YTEST_CHECK_EQ_INT(test, slot_codepoint(test, grid, window[0], 0), 'd');
    YTEST_CHECK_EQ_INT(test, slot_codepoint(test, grid, window[0], 4), '-');

    /* And a mid-history line renders its text too. */
    window = view_window(test, grid, 1, 600, 4);
    YTEST_CHECK_EQ_INT(test, slot_codepoint(test, grid, window[0], 0), 'l');

    yetty_yvterm_grid_dispose(grid);
    (void)fixture;
}

/*---------------------------------------------------------------------------
 * Total line cap: whole old segments drop and the floor advances.
 *-------------------------------------------------------------------------*/
static void test_total_cap_advances_floor(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 50, 8);
    feed_newlines(test, grid, 700);
    /* First sealed segment (512 lines) is fully beyond the 50-line cap once
     * the live top passes 562 — it must be gone. */
    YTEST_CHECK(test, history_floor(test, grid) >= 512);
    YTEST_CHECK(test, history_floor(test, grid) <= live_anchor(test, grid));
    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Text overwriting an anchored ring line invalidates runtime AND envelope
 * (unchanged hot-tier behaviour).
 *-------------------------------------------------------------------------*/
static void test_text_overwrite_drops_envelope(struct ytest *test)
{
    struct tier_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 0, 8);
    (void)anchor_figure(test, grid, &fixture);

    /* Print over the anchor row (cursor is still on it). */
    feed(test, grid, "overwritten");
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 1);
    const uint32_t *window = view_window(test, grid, 0, 0, 4);
    YTEST_CHECK_EQ_INT(test, slot_complex_count(test, grid, window[0]), 0);
    YTEST_CHECK_EQ_INT(test, slot_primitive_count(test, grid, window[0]), 0);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Lazy reflow: resize drains ring history to the archive; the archived text
 * stays readable afterwards (stored layout, current-width clip/pad).
 *-------------------------------------------------------------------------*/
static void test_resize_drains_history(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 0, 32);
    feed(test, grid, "before-resize");
    feed_newlines(test, grid, 10); /* 7 rows of ring history, none archived */
    uint32_t floor_before = history_floor(test, grid);
    YTEST_CHECK_EQ_INT(test, floor_before, 0);

    struct yetty_ycore_void_result resize_res = yetty_yvterm_grid_resize(grid, 30, 6);
    YTEST_REQUIRE_OK(test, resize_res);

    /* The pre-resize history now lives in the archive; the first line still
     * reads back through the resolver. */
    YTEST_CHECK_EQ_INT(test, history_floor(test, grid), 0);
    const uint32_t *window = view_window(test, grid, 1, 0, 6);
    YTEST_CHECK_EQ_INT(test, slot_codepoint(test, grid, window[0], 0), 'b');
    YTEST_CHECK_EQ_INT(test, slot_codepoint(test, grid, window[0], 6), '-');

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Default hot window (0 → 2000) keeps figures alive across a modest scroll.
 *-------------------------------------------------------------------------*/
static void test_default_hot_window_keeps_runtime(struct ytest *test)
{
    struct tier_fixture fixture = {0};
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 0, 0);
    (void)anchor_figure(test, grid, &fixture);
    feed_newlines(test, grid, 200);
    YTEST_CHECK_EQ_INT(test, fixture.destroy_count, 0);
    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * #486: a parked historical view must never resolve stale identities after
 * eviction moved the floor past it — the grid clamps deterministically.
 *-------------------------------------------------------------------------*/
static void test_view_clamps_after_eviction(struct ytest *test)
{
    /* Total cap 50 with hot 8: old segments drop as output continues. */
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 50, 8);
    feed(test, grid, "old-line");
    feed_newlines(test, grid, 100);
    /* Park a view at the current floor... */
    uint64_t parked = history_floor(test, grid);
    (void)view_window(test, grid, 1, parked, 4);
    /* ...then let continued output drop the parked range entirely. */
    feed_newlines(test, grid, 700);
    uint64_t floor_now = history_floor(test, grid);
    YTEST_REQUIRE(test, floor_now > parked);
    /* Resolving the window must clamp the grid's own view forward — never
     * alias newer rows through ring modulo arithmetic. */
    uint32_t resolved = 0;
    struct yetty_ycore_const_uint32_ptr_result window_res =
        yetty_yvterm_grid_view_window(grid, 4, &resolved);
    YTEST_REQUIRE_OK(test, window_res);
    int active = 0;
    uint64_t view_top = 0;
    struct yetty_ycore_void_result view_res = yetty_yvterm_grid_view(grid, &active, &view_top);
    YTEST_REQUIRE_OK(test, view_res);
    YTEST_CHECK(test, view_top >= floor_now);
    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * #486: logical line identities are 64-bit end to end — anchor, floor and
 * view survive crossing UINT32_MAX without wrapping.
 *-------------------------------------------------------------------------*/
static void test_timeline_crosses_uint32_max(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 0, 8);
    uint64_t base = (uint64_t)UINT32_MAX - 4u;
    struct yetty_ycore_void_result seed_res = yetty_yvterm_grid_seed_timeline(grid, base);
    YTEST_REQUIRE_OK(test, seed_res);

    feed(test, grid, "wrap-marker");
    uint64_t marker_line = live_anchor(test, grid);
    YTEST_CHECK(test, marker_line == base);
    feed_newlines(test, grid, 40); /* pushes anchor + archive across 2^32 */

    uint64_t anchor = live_anchor(test, grid);
    uint64_t floor_val = history_floor(test, grid);
    YTEST_CHECK(test, anchor > (uint64_t)UINT32_MAX);
    YTEST_CHECK(test, floor_val == base);
    YTEST_CHECK(test, floor_val < anchor);

    /* A view across the 2^32 boundary resolves the archived text. */
    const uint32_t *window = view_window(test, grid, 1, marker_line, 4);
    YTEST_CHECK_EQ_INT(test, slot_codepoint(test, grid, window[0], 0), 'w');
    YTEST_CHECK_EQ_INT(test, slot_codepoint(test, grid, window[0], 4), '-');
    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * #486: fault-injected resize — a failing allocation in EITHER ring build
 * leaves rings, geometry and content untouched; a later resize succeeds.
 *-------------------------------------------------------------------------*/
static void check_resize_failure(struct ytest *test, uint32_t failing_allocation)
{
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 0, 8);
    feed(test, grid, "keepsake");

    struct yetty_ycore_void_result inject_res =
        yetty_yvterm_grid_inject_ring_alloc_failure(grid, failing_allocation);
    YTEST_REQUIRE_OK(test, inject_res);
    struct yetty_ycore_void_result resize_res = yetty_yvterm_grid_resize(grid, 30, 6);
    YTEST_REQUIRE_ERR(test, resize_res); /* the macro consumes the error chain */
    (void)yetty_yvterm_grid_inject_ring_alloc_failure(grid, 0);

    /* Geometry and content unchanged. */
    uint32_t cols = 0, rows = 0;
    struct yetty_ycore_void_result dims_res = yetty_yvterm_grid_dims(grid, &cols, &rows, NULL);
    YTEST_REQUIRE_OK(test, dims_res);
    YTEST_CHECK_EQ_INT(test, cols, 20);
    YTEST_CHECK_EQ_INT(test, rows, 4);
    const uint32_t *window = view_window(test, grid, 0, 0, 4);
    YTEST_CHECK_EQ_INT(test, slot_codepoint(test, grid, window[0], 0), 'k');
    YTEST_CHECK_EQ_INT(test, slot_codepoint(test, grid, window[0], 7), 'e');

    /* The grid still works: feeding and a clean resize both succeed. */
    feed(test, grid, "!");
    struct yetty_ycore_void_result retry_res = yetty_yvterm_grid_resize(grid, 30, 6);
    YTEST_REQUIRE_OK(test, retry_res);
    dims_res = yetty_yvterm_grid_dims(grid, &cols, &rows, NULL);
    YTEST_REQUIRE_OK(test, dims_res);
    YTEST_CHECK_EQ_INT(test, cols, 30);
    YTEST_CHECK_EQ_INT(test, rows, 6);
    yetty_yvterm_grid_dispose(grid);
}

static void test_resize_failure_is_transactional(struct ytest *test)
{
    /* Allocation 1 = the first line of the new PRIMARY ring; with 6 new rows
     * and no retained history the new primary consumes 6, so allocation 7 is
     * the first line of the new ALTERNATE ring. Covers both build orders the
     * acceptance asks for. */
    check_resize_failure(test, 1);
    check_resize_failure(test, 7);
}

/*---------------------------------------------------------------------------
 * Scroll-back racing live output: while a historical window is resolved,
 * continued feeding seals/drops segments underneath it. Cache pinning must
 * keep every window row readable (never freed lines) each frame.
 *-------------------------------------------------------------------------*/
static void test_view_survives_streaming_output(struct ytest *test)
{
    /* Tiny cap + tiny hot: drops chase the view aggressively. */
    struct yetty_yclass_object *grid = make_grid(test, 20, 4, 600, 8);
    feed_newlines(test, grid, 1200);
    uint64_t start_top = history_floor(test, grid) + 4;
    (void)view_window(test, grid, 1, start_top, 8);
    for (uint32_t round = 0; round < 200; ++round) {
        feed_newlines(test, grid, 37); /* advances floor + seals + drops */
        uint32_t resolved = 0;
        struct yetty_ycore_const_uint32_ptr_result window_res =
            yetty_yvterm_grid_view_window(grid, 8, &resolved);
        YTEST_REQUIRE_OK(test, window_res);
        for (uint32_t row = 0; row < resolved; ++row) {
            /* Touch every window row's cells — a dangling cache line dies
             * here under ASAN. */
            (void)slot_codepoint(test, grid, window_res.value[row], 0);
        }
    }
    yetty_yvterm_grid_dispose(grid);
}

int main(void)
{
    struct ytest test = ytest_begin("yvterm_scroll_tiers");
    YTEST_RUN(&test, test_age_out_archives_line);
    YTEST_RUN(&test, test_cluster_survives_archive);
    YTEST_RUN(&test, test_view_resolves_archived_lines);
    YTEST_RUN(&test, test_materialize_failure_is_absorbed);
    YTEST_RUN(&test, test_warm_budget_spills_to_disk);
    YTEST_RUN(&test, test_total_cap_advances_floor);
    YTEST_RUN(&test, test_text_overwrite_drops_envelope);
    YTEST_RUN(&test, test_resize_drains_history);
    YTEST_RUN(&test, test_default_hot_window_keeps_runtime);
    YTEST_RUN(&test, test_view_clamps_after_eviction);
    YTEST_RUN(&test, test_timeline_crosses_uint32_max);
    YTEST_RUN(&test, test_resize_failure_is_transactional);
    YTEST_RUN(&test, test_view_survives_streaming_output);
    return ytest_end(&test);
}
