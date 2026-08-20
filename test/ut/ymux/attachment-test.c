/*
 * ymux attachment contract test (#695 phase 2) — headless, no GPU, no
 * yvterm. Per-client view state: follow-live vs stable anchoring (identity
 * wins over the timeline hint; floor drops clamp), independent selections
 * on one pane, generation/ack bookkeeping.
 */

#include <yetty/api/ymux/attachment.h>
#include <yetty/api/ymux/engine.h>
#include <yetty/api/ymux/history.h>
#include <yetty/api/ymux/pane.h>

#include "ytest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void feed_line(struct ytest *test, struct yetty_yclass_object *pane, uint32_t number)
{
    char line[32];
    snprintf(line, sizeof(line), "line-%04u\r\n", number);
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(pane, line, strlen(line)));
}

/*---------------------------------------------------------------------------
 * Follow-live tracks; anchoring pins by identity and survives more output.
 *-------------------------------------------------------------------------*/
static void test_follow_and_anchor(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result pane_res = yetty_ymux_pane_make(4, 20, 8, 0, NULL);
    YTEST_REQUIRE_OK(test, pane_res);
    struct yetty_yclass_object *pane = pane_res.value;
    struct yetty_yclass_object_ptr_result attachment_res = yetty_ymux_attachment_make(pane, 4, 20);
    YTEST_REQUIRE_OK(test, attachment_res);
    struct yetty_yclass_object *attachment = attachment_res.value;

    for (uint32_t number = 0; number < 20; ++number) {
        feed_line(test, pane, number);
    }
    uint64_t live_top = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_timeline(pane, NULL, &live_top));
    YTEST_CHECK_EQ_INT(test, yetty_ymux_attachment_is_following(attachment).value, 1);
    YTEST_CHECK(test, yetty_ymux_attachment_view_top(attachment).value == live_top);

    /* Anchor at row 5; more output must NOT move the anchored view. */
    YTEST_REQUIRE_OK(test, yetty_ymux_attachment_anchor(attachment, 5));
    YTEST_CHECK_EQ_INT(test, yetty_ymux_attachment_is_following(attachment).value, 0);
    YTEST_CHECK(test, yetty_ymux_attachment_view_top(attachment).value == 5);
    for (uint32_t number = 20; number < 40; ++number) {
        feed_line(test, pane, number);
    }
    YTEST_CHECK(test, yetty_ymux_attachment_view_top(attachment).value == 5);

    /* Back to follow: tracks the new live top. */
    YTEST_REQUIRE_OK(test, yetty_ymux_attachment_follow(attachment));
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_timeline(pane, NULL, &live_top));
    YTEST_CHECK(test, yetty_ymux_attachment_view_top(attachment).value == live_top);

    yetty_ymux_attachment_dispose(attachment);
    yetty_ymux_pane_dispose(pane);
}

/*---------------------------------------------------------------------------
 * Floor movement past the anchor clamps deterministically.
 *-------------------------------------------------------------------------*/
static void test_anchor_clamps_after_drop(struct ytest *test)
{
    /* Tiny cap: old segments drop as output continues. */
    struct yetty_yclass_object_ptr_result pane_res = yetty_ymux_pane_make(4, 20, 8, 64, NULL);
    YTEST_REQUIRE_OK(test, pane_res);
    struct yetty_yclass_object *pane = pane_res.value;
    struct yetty_yclass_object_ptr_result attachment_res = yetty_ymux_attachment_make(pane, 4, 20);
    YTEST_REQUIRE_OK(test, attachment_res);
    struct yetty_yclass_object *attachment = attachment_res.value;

    for (uint32_t number = 0; number < 30; ++number) {
        feed_line(test, pane, number);
    }
    YTEST_REQUIRE_OK(test, yetty_ymux_attachment_anchor(attachment, 2));
    for (uint32_t number = 30; number < 1500; ++number) {
        feed_line(test, pane, number);
    }
    uint64_t floor_value = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_timeline(pane, &floor_value, NULL));
    YTEST_REQUIRE(test, floor_value > 2);
    YTEST_CHECK(test, yetty_ymux_attachment_view_top(attachment).value == floor_value);

    yetty_ymux_attachment_dispose(attachment);
    yetty_ymux_pane_dispose(pane);
}

/*---------------------------------------------------------------------------
 * Two attachments on one pane: independent views, selections, generations.
 *-------------------------------------------------------------------------*/
static void test_two_attachments_independent(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result pane_res = yetty_ymux_pane_make(4, 20, 8, 0, NULL);
    YTEST_REQUIRE_OK(test, pane_res);
    struct yetty_yclass_object *pane = pane_res.value;
    struct yetty_yclass_object *first = yetty_ymux_attachment_make(pane, 4, 20).value;
    struct yetty_yclass_object *second = yetty_ymux_attachment_make(pane, 6, 30).value;
    YTEST_REQUIRE_NOT_NULL(test, first);
    YTEST_REQUIRE_NOT_NULL(test, second);

    for (uint32_t number = 0; number < 30; ++number) {
        feed_line(test, pane, number);
    }
    YTEST_REQUIRE_OK(test, yetty_ymux_attachment_anchor(first, 3));
    /* second keeps following. */
    uint64_t live_top = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_timeline(pane, NULL, &live_top));
    YTEST_CHECK(test, yetty_ymux_attachment_view_top(first).value == 3);
    YTEST_CHECK(test, yetty_ymux_attachment_view_top(second).value == live_top);

    /* Selections do not leak across attachments. */
    YTEST_REQUIRE_OK(test, yetty_ymux_attachment_set_selection(first, 11, 2, 12, 5));
    uint64_t anchor_id = 0;
    uint32_t anchor_offset = 0;
    YTEST_REQUIRE_OK(
        test, yetty_ymux_attachment_selection(second, &anchor_id, &anchor_offset, NULL, NULL));
    YTEST_CHECK(test, anchor_id == 0);
    YTEST_REQUIRE_OK(
        test, yetty_ymux_attachment_selection(first, &anchor_id, &anchor_offset, NULL, NULL));
    YTEST_CHECK(test, anchor_id == 11);
    YTEST_CHECK_EQ_INT(test, anchor_offset, 2);

    /* Generations advance and ack independently; over-ack rejects. */
    YTEST_CHECK(test, yetty_ymux_attachment_next_generation(first).value == 1);
    YTEST_CHECK(test, yetty_ymux_attachment_next_generation(first).value == 2);
    YTEST_CHECK(test, yetty_ymux_attachment_next_generation(second).value == 1);
    YTEST_REQUIRE_OK(test, yetty_ymux_attachment_ack(first, 1));
    uint64_t published = 0, acked = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_attachment_generations(first, &published, &acked));
    YTEST_CHECK(test, published == 2);
    YTEST_CHECK(test, acked == 1);
    struct yetty_ycore_void_result over_ack_res = yetty_ymux_attachment_ack(second, 5);
    YTEST_REQUIRE_ERR(test, over_ack_res);

    yetty_ymux_attachment_dispose(first);
    yetty_ymux_attachment_dispose(second);
    yetty_ymux_pane_dispose(pane);
}

int main(void)
{
    struct ytest test = ytest_begin("ymux_attachment");
    YTEST_RUN(&test, test_follow_and_anchor);
    YTEST_RUN(&test, test_anchor_clamps_after_drop);
    YTEST_RUN(&test, test_two_attachments_independent);
    return ytest_end(&test);
}
