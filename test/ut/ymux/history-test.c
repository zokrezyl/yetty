/*
 * ymux history contract test (#695 phase 2) — headless, no GPU, no yvterm.
 *
 * The tiered store's whole contract: rows pushed with identity resolve back
 * VERBATIM (cells + logical_line_id + logical_cell_start + continuation)
 * from the hot ring, from lz4 warm segments, and from the cold spill file;
 * the total row cap advances the floor by whole segments; dropped and
 * out-of-range indices reject.
 */

#include <yetty/api/ymux/engine.h>
#include <yetty/api/ymux/history.h>

#include "ytest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A deterministic synthetic row: 20 cols, content derived from `seed`. */
static void fill_row(struct yetty_ymux_cell *cells, uint32_t cols, uint32_t seed)
{
    memset(cells, 0, (size_t)cols * sizeof(struct yetty_ymux_cell));
    for (uint32_t col = 0; col < cols; ++col) {
        cells[col].codepoint = 'A' + ((seed + col) % 26);
        cells[col].fg = 0xFF000000u | (seed * 31u + col);
        cells[col].bg = 0xFF000000u | (seed * 17u);
        cells[col].attrs = (uint16_t)(seed % 8);
        cells[col].width = 1;
        if (col == 3) {
            cells[col].mark_count = 2;
            cells[col].marks[0] = 0x300 + seed % 5;
            cells[col].marks[1] = 0x301;
        }
    }
}

static void push_row(struct ytest *test, struct yetty_yclass_object *history, uint32_t seed)
{
    struct yetty_ymux_cell cells[20];
    fill_row(cells, 20, seed);
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_history_push(history, cells, 20, /*logical_line_id=*/1000 + seed,
                                             /*logical_cell_start=*/seed % 3 * 20,
                                             /*continuation=*/(int)(seed % 2)));
}

static void check_row(struct ytest *test, struct yetty_yclass_object *history,
                      uint64_t timeline_idx, uint32_t seed)
{
    struct yetty_ymux_history_row_result row_res =
        yetty_ymux_history_resolve(history, timeline_idx);
    YTEST_REQUIRE_OK(test, row_res);
    struct yetty_ymux_history_row row = row_res.value;
    YTEST_CHECK_EQ_INT(test, row.cols, 20);
    YTEST_CHECK(test, row.logical_line_id == 1000 + seed);
    YTEST_CHECK_EQ_INT(test, row.logical_cell_start, seed % 3 * 20);
    YTEST_CHECK_EQ_INT(test, row.continuation, (int)(seed % 2));
    struct yetty_ymux_cell expected[20];
    fill_row(expected, 20, seed);
    YTEST_CHECK(test, memcmp(row.cells, expected, sizeof(expected)) == 0);
}

/*---------------------------------------------------------------------------
 * Hot ring round-trip.
 *-------------------------------------------------------------------------*/
static void test_hot_round_trip(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result history_res =
        yetty_ymux_history_make(/*hot_rows=*/64, /*total_row_cap=*/0);
    YTEST_REQUIRE_OK(test, history_res);
    struct yetty_yclass_object *history = history_res.value;
    for (uint32_t seed = 0; seed < 10; ++seed) {
        push_row(test, history, seed);
    }
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_history_pushed_rows(history).value, 10);
    for (uint32_t seed = 0; seed < 10; ++seed) {
        check_row(test, history, seed, seed);
    }
    yetty_ymux_history_dispose(history);
}

/*---------------------------------------------------------------------------
 * Warm tier: rows aged out of a tiny hot ring resolve verbatim (identity
 * included) after lz4 round-trip; hot rows still resolve too.
 *-------------------------------------------------------------------------*/
static void test_warm_round_trip(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result history_res =
        yetty_ymux_history_make(/*hot_rows=*/8, /*total_row_cap=*/0);
    YTEST_REQUIRE_OK(test, history_res);
    struct yetty_yclass_object *history = history_res.value;
    for (uint32_t seed = 0; seed < 800; ++seed) {
        push_row(test, history, seed);
    }
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_history_floor(history).value, 0);
    /* Deep archived rows (sealed segments). */
    check_row(test, history, 0, 0);
    check_row(test, history, 300, 300);
    check_row(test, history, 511, 511);
    /* Rows in the open builder seal on demand. */
    check_row(test, history, 700, 700);
    /* Hot rows. */
    check_row(test, history, 795, 795);
    yetty_ymux_history_dispose(history);
}

/*---------------------------------------------------------------------------
 * Cold tier: a 1-byte warm budget spills every sealed segment; deep rows
 * come back from the file with identity intact.
 *-------------------------------------------------------------------------*/
static void test_cold_round_trip(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result history_res =
        yetty_ymux_history_make(/*hot_rows=*/8, /*total_row_cap=*/0);
    YTEST_REQUIRE_OK(test, history_res);
    struct yetty_yclass_object *history = history_res.value;
    YTEST_REQUIRE_OK(test, yetty_ymux_history_set_budgets(history, 1, 0));
    for (uint32_t seed = 0; seed < 1200; ++seed) {
        push_row(test, history, seed);
    }
    YTEST_CHECK_EQ_INT(test, (int)yetty_ymux_history_floor(history).value, 0);
    check_row(test, history, 0, 0);
    check_row(test, history, 600, 600);
    check_row(test, history, 1023, 1023);
    yetty_ymux_history_dispose(history);
}

/*---------------------------------------------------------------------------
 * Row cap: whole oldest segments drop, floor advances, dropped rows reject.
 *-------------------------------------------------------------------------*/
static void test_row_cap_advances_floor(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result history_res =
        yetty_ymux_history_make(/*hot_rows=*/8, /*total_row_cap=*/64);
    YTEST_REQUIRE_OK(test, history_res);
    struct yetty_yclass_object *history = history_res.value;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        push_row(test, history, seed);
    }
    uint64_t floor_value = yetty_ymux_history_floor(history).value;
    YTEST_CHECK(test, floor_value >= 512);
    struct yetty_ymux_history_row_result dropped_res =
        yetty_ymux_history_resolve(history, floor_value - 1);
    YTEST_REQUIRE_ERR(test, dropped_res);
    check_row(test, history, 1499, 1499); /* newest still hot */
    yetty_ymux_history_dispose(history);
}

/*---------------------------------------------------------------------------
 * Out-of-range rejects.
 *-------------------------------------------------------------------------*/
static void test_out_of_range(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result history_res = yetty_ymux_history_make(8, 0);
    YTEST_REQUIRE_OK(test, history_res);
    struct yetty_yclass_object *history = history_res.value;
    struct yetty_ymux_history_row_result empty_res = yetty_ymux_history_resolve(history, 0);
    YTEST_REQUIRE_ERR(test, empty_res);
    push_row(test, history, 1);
    struct yetty_ymux_history_row_result beyond_res = yetty_ymux_history_resolve(history, 1);
    YTEST_REQUIRE_ERR(test, beyond_res);
    yetty_ymux_history_dispose(history);
}

int main(void)
{
    struct ytest test = ytest_begin("ymux_history");
    YTEST_RUN(&test, test_hot_round_trip);
    YTEST_RUN(&test, test_warm_round_trip);
    YTEST_RUN(&test, test_cold_round_trip);
    YTEST_RUN(&test, test_row_cap_advances_floor);
    YTEST_RUN(&test, test_out_of_range);
    return ytest_end(&test);
}
