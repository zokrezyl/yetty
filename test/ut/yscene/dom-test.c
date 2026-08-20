/*
 * dom-test.c — yscene retained-tree contract tests, straight against the
 * module-internal dom API (internal.h; the test include path carries
 * ${YETTY_ROOT}/src).
 *
 * Covers: tree declare/move/delete/zero, the paint-seq semantics of
 * set_content / append / replace / remove (re-emission keeps depth,
 * interleaved runs and children order by seq), the commit + retire +
 * reclaim span lifecycle, dirty tracking + O(dirtied) clear, id-index
 * churn (tombstone rehash), and content-span validation (structural
 * commands rejected, NULL-with-size rejected, truncation rejected).
 *
 * Returns 0 on success, non-zero on first failed assertion group.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-list/cmds.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include "yetty/yscene/internal.h"

static int g_failures;
static int g_checks;

#define CHECK(name, condition)                                                                     \
    do {                                                                                           \
        g_checks++;                                                                                \
        if (condition) {                                                                           \
            fprintf(stderr, "ok   %s\n", (name));                                                  \
        } else {                                                                                   \
            fprintf(stderr, "FAIL %s (%s:%d)\n", (name), __FILE__, __LINE__);                      \
            g_failures++;                                                                          \
        }                                                                                          \
    } while (0)

#define CHECK_OK(name, result_expr)                                                                \
    do {                                                                                           \
        g_checks++;                                                                                \
        struct yetty_ycore_void_result check_res = (result_expr);                                  \
        if (YETTY_IS_OK(check_res)) {                                                              \
            fprintf(stderr, "ok   %s\n", (name));                                                  \
        } else {                                                                                   \
            fprintf(stderr, "FAIL %s (%s:%d): %s\n", (name), __FILE__, __LINE__,                   \
                    check_res.error.msg);                                                          \
            yetty_ycore_error_destroy(check_res.error);                                            \
            g_failures++;                                                                          \
        }                                                                                          \
    } while (0)

#define CHECK_ERR(name, result_expr)                                                               \
    do {                                                                                           \
        g_checks++;                                                                                \
        struct yetty_ycore_void_result check_res = (result_expr);                                  \
        if (YETTY_IS_ERR(check_res)) {                                                             \
            fprintf(stderr, "ok   %s\n", (name));                                                  \
            yetty_ycore_error_destroy(check_res.error);                                            \
        } else {                                                                                   \
            fprintf(stderr, "FAIL %s (%s:%d): unexpectedly succeeded\n", (name), __FILE__,         \
                    __LINE__);                                                                     \
            g_failures++;                                                                          \
        }                                                                                          \
    } while (0)

/*===========================================================================
 * Span builders — leaf-only wire records via the drawable-list API
 *=========================================================================*/

static struct yetty_ydraw_drawable_list *make_list(void)
{
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(list_res)) {
        fprintf(stderr, "drawable list create failed\n");
        exit(2);
    }
    return list_res.value;
}

static void add_box(struct yetty_ydraw_drawable_list *list, float x, float y, float w, float h)
{
    struct yetty_ysdf_box geometry = {
        .center_x = x + w * 0.5f,
        .center_y = y + h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
        .corner_radius = 0.0f,
    };
    struct yetty_ycore_void_result add_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, /*id=*/0, /*z_order=*/0, /*fill=*/0xff0000ffu, /*stroke=*/0, /*stroke_w=*/0.0f,
        &geometry);
    if (YETTY_IS_ERR(add_res)) {
        fprintf(stderr, "add box failed\n");
        exit(2);
    }
}

/* One span holding `box_count` unit boxes. Caller frees via list destroy;
 * bytes/len alias the list until then. */
static const uint8_t *span_bytes(const struct yetty_ydraw_drawable_list *list)
{
    return (const uint8_t *)yetty_ydraw_drawable_list_data(list);
}

static size_t span_len(const struct yetty_ydraw_drawable_list *list)
{
    return yetty_ydraw_drawable_list_size(list);
}

static struct yetty_yscene_dom *make_dom(void)
{
    struct yetty_yscene_dom_ptr_result dom_res = yetty_yscene_dom_create(NULL);
    if (YETTY_IS_ERR(dom_res)) {
        fprintf(stderr, "dom create failed: %s\n", dom_res.error.msg);
        exit(2);
    }
    return dom_res.value;
}

static uint32_t must_slot(const struct yetty_yscene_dom *dom, uint64_t external_id)
{
    struct yetty_ycore_uint32_result slot_res = yetty_yscene_dom_lookup(dom, external_id);
    if (YETTY_IS_ERR(slot_res)) {
        fprintf(stderr, "lookup(%llu) failed\n", (unsigned long long)external_id);
        yetty_ycore_error_destroy(slot_res.error);
        exit(2);
    }
    return slot_res.value;
}

/*===========================================================================
 * Test groups
 *=========================================================================*/

static void test_tree_shape(void)
{
    struct yetty_yscene_dom *dom = make_dom();

    CHECK("fresh dom: root alone", dom->live_node_count == 1);
    CHECK_OK("declare 1 under root", yetty_yscene_dom_node_declare(dom, 1, 0));
    CHECK_OK("declare 2 under 1", yetty_yscene_dom_node_declare(dom, 2, 1));
    CHECK_OK("declare 3 under 1", yetty_yscene_dom_node_declare(dom, 3, 1));
    CHECK("three live + root", dom->live_node_count == 4);

    uint32_t slot_1 = must_slot(dom, 1);
    uint32_t slot_2 = must_slot(dom, 2);
    uint32_t slot_3 = must_slot(dom, 3);
    CHECK("parent of 1 is root", dom->nodes[slot_1].parent_slot == YETTY_YSCENE_DOM_ROOT_SLOT);
    CHECK("parent of 2 is 1", dom->nodes[slot_2].parent_slot == slot_1);
    CHECK("child order preserved", dom->nodes[slot_1].child_count == 2 &&
                                       dom->nodes[slot_1].children[0] == slot_2 &&
                                       dom->nodes[slot_1].children[1] == slot_3);
    CHECK("seq monotonic in declare order",
          dom->nodes[slot_1].node_seq < dom->nodes[slot_2].node_seq &&
              dom->nodes[slot_2].node_seq < dom->nodes[slot_3].node_seq);

    /* Re-declare with same parent: no-op, seq preserved. */
    uint32_t seq_before = dom->nodes[slot_2].node_seq;
    CHECK_OK("re-declare 2 under 1 (no-op)", yetty_yscene_dom_node_declare(dom, 2, 1));
    CHECK("re-declare keeps seq", dom->nodes[slot_2].node_seq == seq_before);

    /* Move: 3 becomes child of 2; seq preserved. */
    uint32_t seq_3 = dom->nodes[slot_3].node_seq;
    CHECK_OK("move 3 under 2", yetty_yscene_dom_node_declare(dom, 3, 2));
    CHECK("move keeps seq", dom->nodes[slot_3].node_seq == seq_3);
    CHECK("move relinks", dom->nodes[slot_3].parent_slot == slot_2 &&
                              dom->nodes[slot_1].child_count == 1 &&
                              dom->nodes[slot_2].child_count == 1);

    /* Cycle rejection: 1 cannot move under its own descendant 3. */
    CHECK_ERR("cycle move rejected", yetty_yscene_dom_node_declare(dom, 1, 3));
    /* Reserved / root ids. */
    CHECK_ERR("declare id 0 rejected", yetty_yscene_dom_node_declare(dom, 0, 1));
    CHECK_ERR("declare UINT64_MAX rejected", yetty_yscene_dom_node_declare(dom, UINT64_MAX, 0));
    CHECK_ERR("unknown parent rejected", yetty_yscene_dom_node_declare(dom, 9, 777));

    /* Subtree delete: 1 takes 2 and 3 with it. */
    CHECK_OK("delete subtree 1", yetty_yscene_dom_node_delete(dom, 1));
    CHECK("subtree released", dom->live_node_count == 1);
    {
        struct yetty_ycore_uint32_result gone_res = yetty_yscene_dom_lookup(dom, 3);
        CHECK("descendant unindexed", YETTY_IS_ERR(gone_res));
        if (YETTY_IS_ERR(gone_res)) {
            yetty_ycore_error_destroy(gone_res.error);
        }
    }
    CHECK_ERR("delete root rejected", yetty_yscene_dom_node_delete(dom, 0));

    struct yetty_ycore_void_result destroy_res = yetty_yscene_dom_destroy(dom);
    CHECK("destroy", YETTY_IS_OK(destroy_res));
}

static void test_content_and_paint_seq(void)
{
    struct yetty_yscene_dom *dom = make_dom();
    struct yetty_ydraw_drawable_list *two_boxes = make_list();
    add_box(two_boxes, 0, 0, 10, 10);
    add_box(two_boxes, 20, 0, 10, 10);
    struct yetty_ydraw_drawable_list *one_box = make_list();
    add_box(one_box, 5, 5, 4, 4);

    CHECK_OK("declare node 1", yetty_yscene_dom_node_declare(dom, 1, 0));
    CHECK_OK("set_content 2 boxes",
             yetty_yscene_dom_node_set_content(dom, 1, span_bytes(two_boxes), span_len(two_boxes)));
    uint32_t slot_1 = must_slot(dom, 1);
    CHECK("one batch, two records",
          dom->nodes[slot_1].batch_count == 1 &&
              dom->batches[dom->nodes[slot_1].batch_slots[0]].record_count == 2);
    uint32_t first_seq = dom->batches[dom->nodes[slot_1].batch_slots[0]].paint_seq;
    CHECK("first batch anchors at node seq", first_seq == dom->nodes[slot_1].node_seq);
    CHECK("owner_seq stamps node identity",
          dom->batches[dom->nodes[slot_1].batch_slots[0]].owner_seq == dom->nodes[slot_1].node_seq);

    /* Interleave: batch A < child < batch B, expressible by seq. */
    CHECK_OK("declare child 2 mid-content", yetty_yscene_dom_node_declare(dom, 2, 1));
    CHECK_OK("append batch B",
             yetty_yscene_dom_node_append_batch(dom, 1, span_bytes(one_box), span_len(one_box)));
    uint32_t slot_2 = must_slot(dom, 2);
    uint32_t batch_b_seq = dom->batches[dom->nodes[slot_1].batch_slots[1]].paint_seq;
    CHECK("source interleave order by seq",
          first_seq < dom->nodes[slot_2].node_seq && dom->nodes[slot_2].node_seq < batch_b_seq);

    /* Re-emission (wholesale set_content) keeps the paint position. */
    CHECK_OK("re-emit content",
             yetty_yscene_dom_node_set_content(dom, 1, span_bytes(one_box), span_len(one_box)));
    CHECK("re-emit collapses to one batch", dom->nodes[slot_1].batch_count == 1);
    CHECK("re-emit preserves paint_seq",
          dom->batches[dom->nodes[slot_1].batch_slots[0]].paint_seq == first_seq);
    CHECK("old spans retired, not freed", yetty_yscene_dom_retired_batch_count(dom) == 2);

    /* Batch-granular replace preserves; append mints. */
    CHECK_OK("append second batch",
             yetty_yscene_dom_node_append_batch(dom, 1, span_bytes(one_box), span_len(one_box)));
    uint32_t appended_seq = dom->batches[dom->nodes[slot_1].batch_slots[1]].paint_seq;
    CHECK("append mints fresh seq", appended_seq > batch_b_seq);
    CHECK_OK("replace batch 0", yetty_yscene_dom_node_replace_batch(
                                    dom, 1, 0, span_bytes(two_boxes), span_len(two_boxes)));
    CHECK("replace preserves seq",
          dom->batches[dom->nodes[slot_1].batch_slots[0]].paint_seq == first_seq);
    CHECK_OK("remove batch 0", yetty_yscene_dom_node_remove_batch(dom, 1, 0));
    CHECK("remove shifts list",
          dom->nodes[slot_1].batch_count == 1 &&
              dom->batches[dom->nodes[slot_1].batch_slots[0]].paint_seq == appended_seq);
    CHECK_ERR("replace out of range", yetty_yscene_dom_node_replace_batch(
                                          dom, 1, 7, span_bytes(one_box), span_len(one_box)));

    /* Span lifecycle: commit, then reclaim frees retired spans. */
    uint32_t retired_before = yetty_yscene_dom_retired_batch_count(dom);
    CHECK("retired spans pending", retired_before > 0);
    struct yetty_ycore_uint64_result commit_res = yetty_yscene_dom_commit(dom);
    CHECK("commit bumps generation", YETTY_IS_OK(commit_res) && commit_res.value == 1);
    CHECK_OK("reclaim at derived generation", yetty_yscene_dom_reclaim(dom, commit_res.value));
    CHECK("retired spans freed", yetty_yscene_dom_retired_batch_count(dom) == 0);

    /* Commit without pending: unchanged. */
    struct yetty_ycore_uint64_result idle_commit_res = yetty_yscene_dom_commit(dom);
    CHECK("idle commit keeps generation",
          YETTY_IS_OK(idle_commit_res) && idle_commit_res.value == 1);

    yetty_ydraw_drawable_list_destroy(two_boxes);
    yetty_ydraw_drawable_list_destroy(one_box);
    struct yetty_ycore_void_result destroy_res = yetty_yscene_dom_destroy(dom);
    CHECK("destroy", YETTY_IS_OK(destroy_res));
}

static void test_span_validation(void)
{
    struct yetty_yscene_dom *dom = make_dom();
    CHECK_OK("declare node", yetty_yscene_dom_node_declare(dom, 1, 0));

    /* Structural commands inside a content span are rejected. */
    struct yetty_ydraw_drawable_list *with_group = make_list();
    add_box(with_group, 0, 0, 4, 4);
    struct yetty_ydraw_id_result marker_res = yetty_ydraw_drawable_list_begin_group(with_group, 42);
    if (YETTY_IS_OK(marker_res)) {
        yetty_ydraw_drawable_list_end_group(with_group, marker_res.value);
    }
    CHECK_ERR(
        "embedded CMD_GROUP rejected",
        yetty_yscene_dom_node_set_content(dom, 1, span_bytes(with_group), span_len(with_group)));
    uint32_t slot_1 = must_slot(dom, 1);
    CHECK("rejected span leaves node untouched", dom->nodes[slot_1].batch_count == 0);

    /* NULL bytes with a size. */
    CHECK_ERR("NULL bytes with size rejected", yetty_yscene_dom_node_set_content(dom, 1, NULL, 64));

    /* Truncated record tail. */
    struct yetty_ydraw_drawable_list *good = make_list();
    add_box(good, 0, 0, 4, 4);
    CHECK_ERR("mid-record truncation rejected",
              yetty_yscene_dom_node_set_content(dom, 1, span_bytes(good), span_len(good) - 3));
    CHECK_OK("intact span accepted",
             yetty_yscene_dom_node_set_content(dom, 1, span_bytes(good), span_len(good)));

    /* Empty clears. */
    CHECK_OK("empty span clears", yetty_yscene_dom_node_set_content(dom, 1, NULL, 0));
    CHECK("cleared", dom->nodes[slot_1].batch_count == 0);

    yetty_ydraw_drawable_list_destroy(with_group);
    yetty_ydraw_drawable_list_destroy(good);
    struct yetty_ycore_void_result destroy_res = yetty_yscene_dom_destroy(dom);
    CHECK("destroy", YETTY_IS_OK(destroy_res));
}

static void test_dirty_and_zero(void)
{
    struct yetty_yscene_dom *dom = make_dom();
    struct yetty_ydraw_drawable_list *one_box = make_list();
    add_box(one_box, 0, 0, 4, 4);

    CHECK_OK("declare 1", yetty_yscene_dom_node_declare(dom, 1, 0));
    CHECK_OK("declare 2 under 1", yetty_yscene_dom_node_declare(dom, 2, 1));
    CHECK_OK("content on 2",
             yetty_yscene_dom_node_set_content(dom, 2, span_bytes(one_box), span_len(one_box)));

    uint32_t slot_1 = must_slot(dom, 1);
    uint32_t slot_2 = must_slot(dom, 2);
    CHECK("leaf content-dirty", dom->nodes[slot_2].content_dirty);
    CHECK("rollup at ancestor", dom->nodes[slot_1].subtree_dirty);
    CHECK("rollup at root", dom->nodes[YETTY_YSCENE_DOM_ROOT_SLOT].subtree_dirty);
    CHECK("dirty list bounded by dirtied nodes",
          dom->dirty_slot_count >= 3 && dom->dirty_slot_count != UINT32_MAX);

    CHECK_OK("clear dirty", yetty_yscene_dom_clear_dirty(dom));
    CHECK("all clean after clear", !dom->nodes[slot_1].subtree_dirty &&
                                       !dom->nodes[slot_2].content_dirty &&
                                       !dom->nodes[YETTY_YSCENE_DOM_ROOT_SLOT].subtree_dirty);
    CHECK("dirty list drained", dom->dirty_slot_count == 0);

    /* A placement change dirties placement (not content) + rollup. */
    struct yetty_yscene_dom_placement placement = {
        .paint_z = 5,
        .m00 = 1.0f,
        .m11 = 1.0f,
        .translate_x = 10.0f,
        .opacity = 1.0f,
    };
    CHECK_OK("set placement", yetty_yscene_dom_node_set_placement(dom, 2, &placement));
    CHECK("placement dirty, content clean",
          dom->nodes[slot_2].placement_dirty && !dom->nodes[slot_2].content_dirty);

    /* zero: everything gone, root reset + both dirty kinds. */
    CHECK_OK("zero", yetty_yscene_dom_zero(dom));
    CHECK("only root lives", dom->live_node_count == 1);
    CHECK("root marked placement+content dirty",
          dom->nodes[YETTY_YSCENE_DOM_ROOT_SLOT].placement_dirty &&
              dom->nodes[YETTY_YSCENE_DOM_ROOT_SLOT].content_dirty);
    {
        struct yetty_ycore_uint32_result gone_res = yetty_yscene_dom_lookup(dom, 2);
        CHECK("index emptied", YETTY_IS_ERR(gone_res));
        if (YETTY_IS_ERR(gone_res)) {
            yetty_ycore_error_destroy(gone_res.error);
        }
    }
    /* Retired spans survive zero until commit + reclaim. */
    CHECK("spans retired by zero", yetty_yscene_dom_retired_batch_count(dom) == 1);
    struct yetty_ycore_uint64_result commit_res = yetty_yscene_dom_commit(dom);
    CHECK_OK("reclaim after zero",
             yetty_yscene_dom_reclaim(dom, YETTY_IS_OK(commit_res) ? commit_res.value : 0));
    CHECK("spans freed after reclaim", yetty_yscene_dom_retired_batch_count(dom) == 0);

    yetty_ydraw_drawable_list_destroy(one_box);
    struct yetty_ycore_void_result destroy_res = yetty_yscene_dom_destroy(dom);
    CHECK("destroy", YETTY_IS_OK(destroy_res));
}

static void test_id_churn(void)
{
    struct yetty_yscene_dom *dom = make_dom();
    /* SPA-style declare/delete cycles: tombstones must not degrade or
     * corrupt lookups (occupied-load rehash purges them). */
    bool all_ok = true;
    for (uint32_t round = 0; round < 200 && all_ok; round++) {
        for (uint64_t id = 1; id <= 50; id++) {
            struct yetty_ycore_void_result declare_res =
                yetty_yscene_dom_node_declare(dom, round * 1000 + id, 0);
            if (YETTY_IS_ERR(declare_res)) {
                yetty_ycore_error_destroy(declare_res.error);
                all_ok = false;
                break;
            }
        }
        for (uint64_t id = 1; id <= 50 && all_ok; id++) {
            struct yetty_ycore_void_result delete_res =
                yetty_yscene_dom_node_delete(dom, round * 1000 + id);
            if (YETTY_IS_ERR(delete_res)) {
                yetty_ycore_error_destroy(delete_res.error);
                all_ok = false;
            }
        }
    }
    CHECK("10k declare/delete churn survives", all_ok);
    CHECK("only root lives after churn", dom->live_node_count == 1);
    CHECK("live accounting zeroed", dom->id_index_live == 0);
    CHECK("occupied stays bounded (tombstones purged by rehash)",
          dom->id_index_occupied * 10 < dom->id_index_capacity * 8);

    /* Node slots recycle: high-water far below total mints. */
    CHECK("slots recycled", dom->node_high_water <= 64);

    struct yetty_ycore_void_result destroy_res = yetty_yscene_dom_destroy(dom);
    CHECK("destroy", YETTY_IS_OK(destroy_res));
}

static void test_typed_interleave_anchor(void)
{
    /* The reviewed ambiguity: append parent batch A, declare child,
     * append parent batch B, THEN attach the child's content — the
     * child must still paint between A and B (its first batch anchors
     * at its node_seq, minted between the two appends). */
    struct yetty_yscene_dom *dom = make_dom();
    struct yetty_ydraw_drawable_list *one_box = make_list();
    add_box(one_box, 0, 0, 4, 4);

    CHECK_OK("declare parent", yetty_yscene_dom_node_declare(dom, 1, 0));
    CHECK_OK("parent batch A",
             yetty_yscene_dom_node_append_batch(dom, 1, span_bytes(one_box), span_len(one_box)));
    CHECK_OK("declare child mid-stream", yetty_yscene_dom_node_declare(dom, 2, 1));
    CHECK_OK("parent batch B",
             yetty_yscene_dom_node_append_batch(dom, 1, span_bytes(one_box), span_len(one_box)));
    CHECK_OK("child content attached LAST",
             yetty_yscene_dom_node_set_content(dom, 2, span_bytes(one_box), span_len(one_box)));

    uint32_t parent_slot = must_slot(dom, 1);
    uint32_t child_slot = must_slot(dom, 2);
    uint32_t seq_a = dom->batches[dom->nodes[parent_slot].batch_slots[0]].paint_seq;
    uint32_t seq_b = dom->batches[dom->nodes[parent_slot].batch_slots[1]].paint_seq;
    uint32_t seq_child = dom->batches[dom->nodes[child_slot].batch_slots[0]].paint_seq;
    CHECK("A < child < B despite late attachment", seq_a < seq_child && seq_child < seq_b);

    yetty_ydraw_drawable_list_destroy(one_box);
    struct yetty_ycore_void_result destroy_res = yetty_yscene_dom_destroy(dom);
    CHECK("destroy", YETTY_IS_OK(destroy_res));
}

static void test_hash_and_placement_hardening(void)
{
    struct yetty_yscene_dom *dom = make_dom();

    /* Tombstone reuse must not double-count occupied buckets. */
    CHECK_OK("declare", yetty_yscene_dom_node_declare(dom, 7, 0));
    CHECK_OK("delete", yetty_yscene_dom_node_delete(dom, 7));
    CHECK_OK("re-declare into tombstone", yetty_yscene_dom_node_declare(dom, 7, 0));
    CHECK("occupied equals live after tombstone reuse",
          dom->id_index_occupied == dom->id_index_live);

    /* Non-finite placement values are rejected. */
    struct yetty_yscene_dom_placement bad = {
        .m00 = 1.0f, .m11 = 1.0f, .translate_x = 0.0f / 0.0f, .opacity = 1.0f};
    CHECK_ERR("NaN translate rejected", yetty_yscene_dom_node_set_placement(dom, 7, &bad));
    struct yetty_yscene_dom_placement inverted_clip = {
        .m00 = 1.0f,
        .m11 = 1.0f,
        .opacity = 1.0f,
        .has_clip = true,
        .clip = {{10, 10}, {0, 0}},
    };
    CHECK_ERR("inverted clip rejected",
              yetty_yscene_dom_node_set_placement(dom, 7, &inverted_clip));

    struct yetty_ycore_void_result destroy_res = yetty_yscene_dom_destroy(dom);
    CHECK("destroy", YETTY_IS_OK(destroy_res));
}

/* Review #15: the allocation-free LEAF delete — the staged-rollback
 * primitive. Deletes a leaf; REFUSES a parent (tree intact); atomic
 * multi-delete still handles subtrees. */
static void test_leaf_delete(void)
{
    struct yetty_yscene_dom *dom = make_dom();
    CHECK_OK("declare leaf", yetty_yscene_dom_node_declare(dom, 21, 0));
    CHECK_OK("declare parent", yetty_yscene_dom_node_declare(dom, 22, 0));
    CHECK_OK("declare child under parent", yetty_yscene_dom_node_declare(dom, 23, 22));

    CHECK_OK("leaf delete removes the leaf", yetty_yscene_dom_node_delete_leaf(dom, 21));
    CHECK_ERR("leaf delete refuses a parent", yetty_yscene_dom_node_delete_leaf(dom, 22));
    /* The refused delete left the pair fully intact. */
    CHECK_OK("parent still declared (re-declare no-op)", yetty_yscene_dom_node_declare(dom, 22, 0));
    CHECK_OK("child still declared (re-declare no-op)", yetty_yscene_dom_node_declare(dom, 23, 22));
    CHECK_OK("child (a leaf) deletes", yetty_yscene_dom_node_delete_leaf(dom, 23));
    CHECK_OK("then the parent (now a leaf) deletes", yetty_yscene_dom_node_delete_leaf(dom, 22));
    /* Reuse after the churn: the index stays consistent (the un-reused
     * tombstones are reclaimed at the next rehash, not immediately). */
    CHECK_OK("re-declare into leaf tombstone", yetty_yscene_dom_node_declare(dom, 21, 0));
    CHECK_OK("and it deletes again as a leaf", yetty_yscene_dom_node_delete_leaf(dom, 21));

    struct yetty_ycore_void_result destroy_res = yetty_yscene_dom_destroy(dom);
    CHECK("destroy", YETTY_IS_OK(destroy_res));
}

int main(void)
{
    test_tree_shape();
    test_content_and_paint_seq();
    test_span_validation();
    test_dirty_and_zero();
    test_id_churn();
    test_typed_interleave_anchor();
    test_hash_and_placement_hardening();
    test_leaf_delete();
    fprintf(stderr, "%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
