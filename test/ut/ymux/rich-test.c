/*
 * ymux rich-store contract test (#695 phase 2) — headless, no GPU, no
 * yvterm. Stable ids, store-owned creation records, tagged anchors,
 * lossless journals (replay order), stream mapping incl. ADD re-bind,
 * DELETE tombstone+unbind, clear-all, namespace close.
 */

#include <yetty/api/ymux/rich.h>

#include "ytest.h"

#include <stdint.h>
#include <string.h>

static uint64_t mint(struct ytest *test, struct yetty_yclass_object *rich, uint32_t seed)
{
    uint32_t creation[4] = {0x80000001u, seed, seed * 2, seed * 3};
    struct yetty_ycore_uint64_result mint_res = yetty_ymux_rich_mint(
        rich, creation, 4, YETTY_YMUX_RICH_ANCHOR_PRIMARY, /*anchor_a=*/100 + seed,
        /*anchor_b=*/seed, /*span_rows=*/2);
    YTEST_REQUIRE_OK(test, mint_res);
    YTEST_REQUIRE(test, mint_res.value != 0);
    return mint_res.value;
}

static void test_mint_and_creation_record(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result rich_res = yetty_ymux_rich_make();
    YTEST_REQUIRE_OK(test, rich_res);
    struct yetty_yclass_object *rich = rich_res.value;

    uint32_t creation[4] = {0x80000001u, 7, 14, 21};
    uint64_t first = mint(test, rich, 7);
    uint64_t second = mint(test, rich, 9);
    YTEST_CHECK(test, first != second);

    uint32_t word_count = 0;
    struct yetty_ycore_const_uint32_ptr_result creation_res =
        yetty_ymux_rich_creation(rich, first, &word_count);
    YTEST_REQUIRE_OK(test, creation_res);
    YTEST_CHECK_EQ_INT(test, word_count, 4);
    YTEST_CHECK(test, memcmp(creation_res.value, creation, sizeof(creation)) == 0);

    int kind = -1;
    uint64_t anchor_a = 0;
    uint32_t anchor_b = 0, span_rows = 0;
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_rich_anchor(rich, first, &kind, &anchor_a, &anchor_b, &span_rows));
    YTEST_CHECK_EQ_INT(test, kind, YETTY_YMUX_RICH_ANCHOR_PRIMARY);
    YTEST_CHECK(test, anchor_a == 107);
    YTEST_CHECK_EQ_INT(test, anchor_b, 7);
    YTEST_CHECK_EQ_INT(test, span_rows, 2);

    yetty_ymux_rich_dispose(rich);
}

static void test_journal_replay_order(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result rich_res = yetty_ymux_rich_make();
    YTEST_REQUIRE_OK(test, rich_res);
    struct yetty_yclass_object *rich = rich_res.value;
    uint64_t rich_id = mint(test, rich, 1);

    for (uint32_t update = 0; update < 5; ++update) {
        uint32_t record[2] = {0xCAFEu, update};
        YTEST_REQUIRE_OK(test, yetty_ymux_rich_journal_append(rich, rich_id, record, 2));
    }
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_journal_count(rich, rich_id).value, 5);
    for (uint32_t update = 0; update < 5; ++update) {
        uint32_t word_count = 0;
        struct yetty_ycore_const_uint32_ptr_result entry_res =
            yetty_ymux_rich_journal_entry(rich, rich_id, update, &word_count);
        YTEST_REQUIRE_OK(test, entry_res);
        YTEST_CHECK_EQ_INT(test, word_count, 2);
        YTEST_CHECK_EQ_INT(test, entry_res.value[1], update);
    }

    /* Tombstoned objects reject further updates and never rematerialize. */
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_tombstone(rich, rich_id));
    uint32_t late[2] = {0xCAFEu, 99};
    struct yetty_ycore_void_result late_res =
        yetty_ymux_rich_journal_append(rich, rich_id, late, 2);
    YTEST_REQUIRE_ERR(test, late_res);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_is_tombstoned(rich, rich_id).value, 1);

    yetty_ymux_rich_dispose(rich);
}

static void test_stream_mapping(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result rich_res = yetty_ymux_rich_make();
    YTEST_REQUIRE_OK(test, rich_res);
    struct yetty_yclass_object *rich = rich_res.value;

    uint64_t first = mint(test, rich, 1);
    uint64_t second = mint(test, rich, 2);
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_map_bind(rich, 1, first));
    YTEST_CHECK(test, yetty_ymux_rich_map_resolve(rich, 1).value == first);
    YTEST_CHECK(test, yetty_ymux_rich_map_resolve(rich, 2).value == 0);

    /* ADD re-binds the ordinal to the fresh object. */
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_map_bind(rich, 1, second));
    YTEST_CHECK(test, yetty_ymux_rich_map_resolve(rich, 1).value == second);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_is_tombstoned(rich, first).value, 0);

    /* DELETE tombstones + unbinds (new model behavior). */
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_map_delete(rich, 1));
    YTEST_CHECK(test, yetty_ymux_rich_map_resolve(rich, 1).value == 0);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_is_tombstoned(rich, second).value, 1);
    struct yetty_ycore_void_result missing_res = yetty_ymux_rich_map_delete(rich, 1);
    YTEST_REQUIRE_ERR(test, missing_res);

    /* clear-all tombstones everything and empties the namespace. */
    uint64_t third = mint(test, rich, 3);
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_map_bind(rich, 4, third));
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_clear_all(rich));
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_is_tombstoned(rich, third).value, 1);
    YTEST_CHECK(test, yetty_ymux_rich_map_resolve(rich, 4).value == 0);

    /* Producer exit closes the namespace; objects survive. */
    uint64_t fourth = mint(test, rich, 4);
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_map_bind(rich, 9, fourth));
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_map_close(rich));
    YTEST_CHECK(test, yetty_ymux_rich_map_resolve(rich, 9).value == 0);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_is_tombstoned(rich, fourth).value, 0);

    yetty_ymux_rich_dispose(rich);
}

int main(void)
{
    struct ytest test = ytest_begin("ymux_rich");
    YTEST_RUN(&test, test_mint_and_creation_record);
    YTEST_RUN(&test, test_journal_replay_order);
    YTEST_RUN(&test, test_stream_mapping);
    return ytest_end(&test);
}
