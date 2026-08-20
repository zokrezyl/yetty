/*
 * ymux pane contract test (#695 phase 2) — headless, no GPU, no yvterm.
 *
 * The pane composes engine + tiered history into ONE timeline address
 * space. The identity contract end-to-end through the REAL scroll path:
 * a row's logical id observed live is the SAME id resolved from history
 * after the row ages hot → warm → cold, and its text round-trips verbatim.
 */

#include <yetty/api/ymux/engine.h>
#include <yetty/api/ymux/history.h>
#include <yetty/api/ymux/pane.h>

#include "ytest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void feed(struct ytest *test, struct yetty_yclass_object *pane, const char *bytes)
{
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(pane, bytes, strlen(bytes)));
}

static void feed_line(struct ytest *test, struct yetty_yclass_object *pane, uint32_t number)
{
    char line[32];
    snprintf(line, sizeof(line), "line-%04u\r\n", number);
    feed(test, pane, line);
}

static void check_resolved_text(struct ytest *test, struct yetty_ymux_history_row row,
                                uint32_t number)
{
    char expected[16];
    int expected_len = snprintf(expected, sizeof(expected), "line-%04u", number);
    YTEST_REQUIRE(test, expected_len > 0);
    for (int col = 0; col < expected_len; ++col) {
        YTEST_CHECK_EQ_INT(test, row.cells[col].codepoint, (uint32_t)expected[col]);
    }
}

/*---------------------------------------------------------------------------
 * One timeline across history and the live screen.
 *-------------------------------------------------------------------------*/
static void test_timeline_resolution(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result pane_res =
        yetty_ymux_pane_make(4, 20, /*hot_rows=*/8, /*total_row_cap=*/0, NULL);
    YTEST_REQUIRE_OK(test, pane_res);
    struct yetty_yclass_object *pane = pane_res.value;

    for (uint32_t number = 0; number < 40; ++number) {
        feed_line(test, pane, number);
    }
    uint64_t floor_value = 0, live_top = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_timeline(pane, &floor_value, &live_top));
    YTEST_CHECK_EQ_INT(test, (int)floor_value, 0);
    /* 40 lines through a 4-row screen: rows 0..36 scrolled out (the last
     * newline leaves the cursor on a fresh bottom row). */
    YTEST_CHECK_EQ_INT(test, (int)live_top, 37);

    /* Deep history (cold/warm), mid history, hot history, live rows. */
    struct yetty_ymux_history_row_result row_res = yetty_ymux_pane_resolve_row(pane, 0);
    YTEST_REQUIRE_OK(test, row_res);
    check_resolved_text(test, row_res.value, 0);
    row_res = yetty_ymux_pane_resolve_row(pane, 20);
    YTEST_REQUIRE_OK(test, row_res);
    check_resolved_text(test, row_res.value, 20);
    row_res = yetty_ymux_pane_resolve_row(pane, 36);
    YTEST_REQUIRE_OK(test, row_res);
    check_resolved_text(test, row_res.value, 36);
    /* Live rows: timeline live_top + N == visible row N. */
    row_res = yetty_ymux_pane_resolve_row(pane, live_top);
    YTEST_REQUIRE_OK(test, row_res);
    check_resolved_text(test, row_res.value, 37);
    row_res = yetty_ymux_pane_resolve_row(pane, live_top + 2);
    YTEST_REQUIRE_OK(test, row_res);
    check_resolved_text(test, row_res.value, 39);
    /* Beyond the live bottom rejects. */
    row_res = yetty_ymux_pane_resolve_row(pane, live_top + 4);
    YTEST_REQUIRE_ERR(test, row_res);

    yetty_ymux_pane_dispose(pane);
}

/*---------------------------------------------------------------------------
 * Identity end-to-end: live ids survive into history verbatim.
 *-------------------------------------------------------------------------*/
static void test_identity_through_scroll(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result pane_res =
        yetty_ymux_pane_make(3, 20, /*hot_rows=*/4, /*total_row_cap=*/0, NULL);
    YTEST_REQUIRE_OK(test, pane_res);
    struct yetty_yclass_object *pane = pane_res.value;
    struct yetty_yclass_object *engine = yetty_ymux_pane_engine(pane).value;

    feed(test, pane, "aaa\r\nbbb\r\nccc");
    uint64_t live_ids[3];
    for (uint32_t row = 0; row < 3; ++row) {
        YTEST_REQUIRE_OK(test,
                         yetty_ymux_engine_row_identity(engine, row, &live_ids[row], NULL, NULL));
    }

    /* Scroll all three out (plus enough to age them past the tiny hot
     * ring into warm segments). */
    for (uint32_t number = 0; number < 20; ++number) {
        feed_line(test, pane, number);
    }

    struct yetty_ymux_history_row_result row_res = yetty_ymux_pane_resolve_row(pane, 0);
    YTEST_REQUIRE_OK(test, row_res);
    YTEST_CHECK(test, row_res.value.logical_line_id == live_ids[0]);
    YTEST_CHECK_EQ_INT(test, row_res.value.cells[0].codepoint, 'a');
    row_res = yetty_ymux_pane_resolve_row(pane, 1);
    YTEST_REQUIRE_OK(test, row_res);
    YTEST_CHECK(test, row_res.value.logical_line_id == live_ids[1]);
    YTEST_CHECK_EQ_INT(test, row_res.value.cells[0].codepoint, 'b');
    row_res = yetty_ymux_pane_resolve_row(pane, 2);
    YTEST_REQUIRE_OK(test, row_res);
    YTEST_CHECK(test, row_res.value.logical_line_id == live_ids[2]);
    YTEST_CHECK_EQ_INT(test, row_res.value.cells[0].codepoint, 'c');

    yetty_ymux_pane_dispose(pane);
}

/*---------------------------------------------------------------------------
 * A wrapped logical line keeps one id across both history rows.
 *-------------------------------------------------------------------------*/
static void test_wrap_identity_in_history(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result pane_res =
        yetty_ymux_pane_make(3, 10, /*hot_rows=*/4, /*total_row_cap=*/0, NULL);
    YTEST_REQUIRE_OK(test, pane_res);
    struct yetty_yclass_object *pane = pane_res.value;

    feed(test, pane, "0123456789WRAP\r\n"); /* wraps onto a second row */
    for (uint32_t number = 0; number < 12; ++number) {
        feed_line(test, pane, number);
    }

    struct yetty_ymux_history_row_result head_res = yetty_ymux_pane_resolve_row(pane, 0);
    YTEST_REQUIRE_OK(test, head_res);
    struct yetty_ymux_history_row_result tail_res = yetty_ymux_pane_resolve_row(pane, 1);
    YTEST_REQUIRE_OK(test, tail_res);
    YTEST_CHECK_EQ_INT(test, head_res.value.continuation, 0);
    YTEST_CHECK_EQ_INT(test, tail_res.value.continuation, 1);
    YTEST_CHECK(test, head_res.value.logical_line_id == tail_res.value.logical_line_id);
    YTEST_CHECK_EQ_INT(test, head_res.value.logical_cell_start, 0);
    YTEST_CHECK_EQ_INT(test, tail_res.value.logical_cell_start, 10);
    YTEST_CHECK_EQ_INT(test, tail_res.value.cells[0].codepoint, 'W');

    yetty_ymux_pane_dispose(pane);
}

int main(void)
{
    struct ytest test = ytest_begin("ymux_pane");
    YTEST_RUN(&test, test_timeline_resolution);
    YTEST_RUN(&test, test_identity_through_scroll);
    YTEST_RUN(&test, test_wrap_identity_in_history);
    return ytest_end(&test);
}
