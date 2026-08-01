/*
 * scene-test.c — yscene:scene behavioral tests through the public
 * generated API (headless: no GPU, no registry wiring — the scene and
 * its dom fall back to owned default registries).
 *
 * Covers: the typed mutation surface + commit/derive/leaf ordering (the
 * converged paint key), the legacy wire-envelope adapter (CMD_GROUP /
 * CMD_DELETE / CMD_ZERO bodies, re-emission stability — the flicker
 * class), world transform/clip inheritance, view-state scroll (no
 * re-derive), and hit-testing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/api/yfigure/figure.h>
#include <yetty/api/yscene/scene.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

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

/*===========================================================================
 * Helpers
 *=========================================================================*/

static struct yetty_yclass_object *make_scene(void)
{
    struct yetty_ycore_rectangle rect = {{0, 0}, {800, 600}};
    struct yetty_yscene_scene_ptr_result scene_res = yetty_yscene_create(rect, NULL);
    if (YETTY_IS_ERR(scene_res)) {
        fprintf(stderr, "yscene_create failed: %s\n", scene_res.error.msg);
        exit(2);
    }
    struct yetty_yclass_object_ptr_result object_res = yetty_yscene_scene_to(scene_res.value);
    if (YETTY_IS_ERR(object_res)) {
        fprintf(stderr, "scene_to failed\n");
        exit(2);
    }
    return object_res.value;
}

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
        list, /*id=*/0, /*z_order=*/0, /*fill=*/0xff00ff00u, /*stroke=*/0, /*stroke_w=*/0.0f,
        &geometry);
    if (YETTY_IS_ERR(add_res)) {
        fprintf(stderr, "add box failed\n");
        exit(2);
    }
}

static struct yetty_ycore_buffer list_buffer(const struct yetty_ydraw_drawable_list *list)
{
    return (struct yetty_ycore_buffer){
        .data = (uint8_t *)yetty_ydraw_drawable_list_data(list),
        .size = yetty_ydraw_drawable_list_size(list),
        .capacity = yetty_ydraw_drawable_list_size(list),
    };
}

static uint32_t leaf_count(struct yetty_yclass_object *obj)
{
    struct yetty_ycore_uint32_result count_res = yetty_yscene_leaf_count(obj);
    if (YETTY_IS_ERR(count_res)) {
        yetty_ycore_error_destroy(count_res.error);
        return UINT32_MAX;
    }
    return count_res.value;
}

static char *dump_scene(struct yetty_yclass_object *obj)
{
    struct yetty_ycore_char_ptr_result dump_res = yetty_yfigure_dump_state(obj, 0);
    if (YETTY_IS_ERR(dump_res)) {
        yetty_ycore_error_destroy(dump_res.error);
        return NULL;
    }
    return dump_res.value;
}

static uint64_t hit(struct yetty_yclass_object *obj, float x, float y)
{
    struct yetty_ycore_uint64_result hit_res = yetty_yscene_hit_test(obj, x, y);
    if (YETTY_IS_ERR(hit_res)) {
        yetty_ycore_error_destroy(hit_res.error);
        return UINT64_MAX;
    }
    return hit_res.value;
}

/*===========================================================================
 * Typed API: mutate → commit → derive → paint order
 *=========================================================================*/

static void test_typed_api_paint_order(void)
{
    struct yetty_yclass_object *obj = make_scene();
    struct yetty_ydraw_drawable_list *background = make_list();
    add_box(background, 0, 0, 100, 100);
    struct yetty_ydraw_drawable_list *widget = make_list();
    add_box(widget, 10, 10, 20, 20);

    /* parent 1 (background), child 2 (widget) — child paints above at
     * equal z by seq, regardless of emission order. */
    CHECK_OK("declare 1", yetty_yscene_node_declare(obj, 1, 0));
    CHECK_OK("content 1", yetty_yscene_node_set_content(obj, 1, list_buffer(background)));
    CHECK_OK("declare 2 under 1", yetty_yscene_node_declare(obj, 2, 1));
    CHECK_OK("content 2", yetty_yscene_node_set_content(obj, 2, list_buffer(widget)));

    /* Nothing derives before commit. */
    CHECK_OK("derive pre-commit", yetty_yscene_derive(obj));
    CHECK("no leaves before first commit", leaf_count(obj) == 0);

    struct yetty_ycore_uint64_result commit_res = yetty_yscene_commit(obj);
    CHECK("commit", YETTY_IS_OK(commit_res) && commit_res.value == 1);
    CHECK_OK("derive", yetty_yscene_derive(obj));
    CHECK("two leaves", leaf_count(obj) == 2);

    char *dump = dump_scene(obj);
    CHECK("dump present", dump != NULL);
    if (dump) {
        /* Child's leaf must come AFTER the parent's in paint order. */
        const char *parent_leaf = strstr(dump, "node=1");
        const char *child_leaf = strstr(dump, "node=2");
        CHECK("parent leaf listed", parent_leaf != NULL);
        CHECK("child above parent (seq order)",
              parent_leaf && child_leaf && parent_leaf < child_leaf);
        free(dump);
    }

    /* RE-EMISSION (the flicker class): re-send the parent's content;
     * the child must STAY on top. */
    CHECK_OK("re-emit parent content",
             yetty_yscene_node_set_content(obj, 1, list_buffer(background)));
    commit_res = yetty_yscene_commit(obj);
    CHECK("re-emit commit", YETTY_IS_OK(commit_res) && commit_res.value == 2);
    CHECK_OK("re-derive", yetty_yscene_derive(obj));
    dump = dump_scene(obj);
    if (dump) {
        const char *parent_leaf = strstr(dump, "node=1");
        const char *child_leaf = strstr(dump, "node=2");
        CHECK("re-emitted parent still under child",
              parent_leaf && child_leaf && parent_leaf < child_leaf);
        free(dump);
    }

    /* Explicit z beats seq. */
    CHECK_OK("raise parent z", yetty_yscene_node_set_z(obj, 1, 10));
    commit_res = yetty_yscene_commit(obj);
    if (YETTY_IS_ERR(commit_res)) {
        yetty_ycore_error_destroy(commit_res.error);
    }
    CHECK_OK("derive after z", yetty_yscene_derive(obj));
    dump = dump_scene(obj);
    if (dump) {
        const char *parent_leaf = strstr(dump, "node=1");
        const char *child_leaf = strstr(dump, "node=2");
        CHECK("higher z paints later", parent_leaf && child_leaf && child_leaf < parent_leaf);
        free(dump);
    }

    yetty_ydraw_drawable_list_destroy(background);
    yetty_ydraw_drawable_list_destroy(widget);
    struct yetty_ycore_void_result destroy_res = yetty_yfigure_destroy(obj);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
}

/*===========================================================================
 * Transform / clip inheritance + hit-test + view scroll
 *=========================================================================*/

static void test_world_inheritance_and_hit(void)
{
    struct yetty_yclass_object *obj = make_scene();
    struct yetty_ydraw_drawable_list *box = make_list();
    add_box(box, 0, 0, 10, 10); /* local 0,0..10,10 */

    CHECK_OK("declare panel", yetty_yscene_node_declare(obj, 1, 0));
    CHECK_OK("panel translate (100,50)",
             yetty_yscene_node_set_transform(obj, 1, 1, 0, 0, 1, 100, 50));
    CHECK_OK("declare item under panel", yetty_yscene_node_declare(obj, 2, 1));
    CHECK_OK("item translate (20,0)", yetty_yscene_node_set_transform(obj, 2, 1, 0, 0, 1, 20, 0));
    CHECK_OK("item content", yetty_yscene_node_set_content(obj, 2, list_buffer(box)));
    {
        struct yetty_ycore_uint64_result commit_res = yetty_yscene_commit(obj);
        if (YETTY_IS_ERR(commit_res)) {
            yetty_ycore_error_destroy(commit_res.error);
        }
    }
    CHECK_OK("derive", yetty_yscene_derive(obj));

    /* World AABB = (120,50)-(130,60): composed translate. */
    char *dump = dump_scene(obj);
    CHECK("composed world aabb in dump",
          dump && strstr(dump, "aabb=(120.0,50.0,130.0,60.0)") != NULL);
    free(dump);

    CHECK("hit inside item", hit(obj, 125, 55) == 2);
    CHECK("miss outside", hit(obj, 5, 5) == 0);

    /* Scroll is view state: hits shift, derived state does not rebuild. */
    CHECK_OK("set_scroll", yetty_yfigure_set_scroll(obj, 100, 0));
    CHECK("hit follows scroll", hit(obj, 25, 55) == 2);
    CHECK("old point now misses", hit(obj, 5, 55) == 0);

    /* Clip: shrink the panel's clip so the item is culled from hits
     * outside it. Clip is in panel-local space (0..25 wide covers x
     * 100..125 world). */
    CHECK_OK("reset scroll", yetty_yfigure_set_scroll(obj, 0, 0));
    CHECK_OK("panel clip", yetty_yscene_node_set_clip(obj, 1, 0, 0, 25, 25));
    {
        struct yetty_ycore_uint64_result commit_res = yetty_yscene_commit(obj);
        if (YETTY_IS_ERR(commit_res)) {
            yetty_ycore_error_destroy(commit_res.error);
        }
    }
    CHECK("hit inside clip", hit(obj, 122, 55) == 2);
    CHECK("clipped part misses", hit(obj, 128, 55) == 0);

    yetty_ydraw_drawable_list_destroy(box);
    struct yetty_ycore_void_result destroy_res = yetty_yfigure_destroy(obj);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
}

/*===========================================================================
 * Legacy wire adapter — envelopes with embedded structure
 *=========================================================================*/

static void feed(struct yetty_yclass_object *obj, const struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ycore_void_result feed_res = yetty_yfigure_process_bytes(
        obj, (const uint8_t *)yetty_ydraw_drawable_list_data(list),
        yetty_ydraw_drawable_list_size(list));
    if (YETTY_IS_ERR(feed_res)) {
        fprintf(stderr, "process_bytes failed: %s\n", feed_res.error.msg);
        yetty_ycore_error_destroy(feed_res.error);
        g_failures++;
    }
}

static void test_wire_adapter(void)
{
    struct yetty_yclass_object *obj = make_scene();

    /* Envelope: root box + GROUP(7){box, box} + root box.
     * Expected: root run A, then node 7's records, then root run B —
     * exact source order via batch/node seq interleave. */
    struct yetty_ydraw_drawable_list *envelope = make_list();
    add_box(envelope, 0, 0, 50, 50);
    struct yetty_ydraw_id_result group_res = yetty_ydraw_drawable_list_begin_group(envelope, 7);
    if (YETTY_IS_ERR(group_res)) {
        fprintf(stderr, "begin_group failed\n");
        exit(2);
    }
    add_box(envelope, 60, 0, 20, 20);
    add_box(envelope, 90, 0, 20, 20);
    yetty_ydraw_drawable_list_end_group(envelope, group_res.value);
    add_box(envelope, 0, 60, 50, 50);
    feed(obj, envelope);

    CHECK_OK("derive after envelope", yetty_yscene_derive(obj));
    CHECK("four leaves", leaf_count(obj) == 4);
    char *dump = dump_scene(obj);
    if (dump) {
        const char *run_a = strstr(dump, "aabb=(0.0,0.0,50.0,50.0)");
        const char *group_first = strstr(dump, "aabb=(60.0,0.0,80.0,20.0)");
        const char *run_b = strstr(dump, "aabb=(0.0,60.0,50.0,110.0)");
        CHECK("run A before group content", run_a && group_first && run_a < group_first);
        CHECK("group content before run B", group_first && run_b && group_first < run_b);
        CHECK("group node exists in tree", strstr(dump, "node id=7") != NULL);
        free(dump);
    }

    /* Re-emit GROUP(7) with new content: it must keep its paint slot
     * (between run A and run B), replacing content in place. */
    struct yetty_ydraw_drawable_list *reemit = make_list();
    struct yetty_ydraw_id_result reopen_res = yetty_ydraw_drawable_list_begin_group(reemit, 7);
    if (YETTY_IS_ERR(reopen_res)) {
        exit(2);
    }
    add_box(reemit, 70, 5, 10, 10);
    yetty_ydraw_drawable_list_end_group(reemit, reopen_res.value);
    feed(obj, reemit);
    CHECK_OK("derive after re-emit", yetty_yscene_derive(obj));
    CHECK("re-emit shrank group content", leaf_count(obj) == 3);
    dump = dump_scene(obj);
    if (dump) {
        const char *run_a = strstr(dump, "aabb=(0.0,0.0,50.0,50.0)");
        const char *group_new = strstr(dump, "aabb=(70.0,5.0,80.0,15.0)");
        const char *run_b = strstr(dump, "aabb=(0.0,60.0,50.0,110.0)");
        CHECK("re-emitted group stays between runs",
              run_a && group_new && run_b && run_a < group_new && group_new < run_b);
        free(dump);
    }

    /* CMD_DELETE removes the group node. */
    struct yetty_ydraw_drawable_list *delete_list = make_list();
    {
        struct yetty_ycore_void_result delete_res =
            yetty_ydraw_drawable_list_add_cmd_delete(delete_list, 7);
        if (YETTY_IS_ERR(delete_res)) {
            exit(2);
        }
    }
    feed(obj, delete_list);
    CHECK_OK("derive after delete", yetty_yscene_derive(obj));
    CHECK("group leaves gone", leaf_count(obj) == 2);

    /* CMD_ZERO wipes everything; records after it land in the fresh
     * root. */
    struct yetty_ydraw_drawable_list *zero_list = make_list();
    {
        struct yetty_ycore_void_result zero_res =
            yetty_ydraw_drawable_list_add_cmd_zero(zero_list);
        if (YETTY_IS_ERR(zero_res)) {
            exit(2);
        }
    }
    add_box(zero_list, 1, 1, 2, 2);
    feed(obj, zero_list);
    CHECK_OK("derive after zero", yetty_yscene_derive(obj));
    CHECK("only the post-zero record", leaf_count(obj) == 1);

    yetty_ydraw_drawable_list_destroy(envelope);
    yetty_ydraw_drawable_list_destroy(reemit);
    yetty_ydraw_drawable_list_destroy(delete_list);
    yetty_ydraw_drawable_list_destroy(zero_list);
    struct yetty_ycore_void_result destroy_res = yetty_yfigure_destroy(obj);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
}

/*===========================================================================
 * P0 + hardening regressions
 *=========================================================================*/

static void test_commit_isolation(void)
{
    /* commit A -> pending mutation B -> the published snapshot must
     * still show A (commit derives synchronously; later pending
     * mutations cannot corrupt it). */
    struct yetty_yclass_object *obj = make_scene();
    struct yetty_ydraw_drawable_list *content_a = make_list();
    add_box(content_a, 0, 0, 30, 30);
    struct yetty_ydraw_drawable_list *content_b = make_list();
    add_box(content_b, 100, 100, 5, 5);
    add_box(content_b, 110, 100, 5, 5);

    CHECK_OK("declare node", yetty_yscene_node_declare(obj, 1, 0));
    CHECK_OK("content A", yetty_yscene_node_set_content(obj, 1, list_buffer(content_a)));
    struct yetty_ycore_uint64_result commit_res = yetty_yscene_commit(obj);
    CHECK("commit A", YETTY_IS_OK(commit_res) && commit_res.value == 1);

    /* Pending mutations AFTER the commit — replace content, move
     * placement, add a node — none may leak into the published plan. */
    CHECK_OK("pending content B", yetty_yscene_node_set_content(obj, 1, list_buffer(content_b)));
    CHECK_OK("pending declare", yetty_yscene_node_declare(obj, 2, 0));
    CHECK_OK("pending z", yetty_yscene_node_set_z(obj, 1, 50));

    CHECK("published snapshot still has A's one leaf", leaf_count(obj) == 1);
    char *dump = dump_scene(obj);
    CHECK("published aabb is A's", dump && strstr(dump, "aabb=(0.0,0.0,30.0,30.0)") != NULL);
    CHECK("B's content not published", dump && strstr(dump, "aabb=(100.0,") == NULL);
    free(dump);

    /* The NEXT commit publishes B. */
    commit_res = yetty_yscene_commit(obj);
    CHECK("commit B", YETTY_IS_OK(commit_res) && commit_res.value == 2);
    CHECK("B published (two leaves)", leaf_count(obj) == 2);

    yetty_ydraw_drawable_list_destroy(content_a);
    yetty_ydraw_drawable_list_destroy(content_b);
    struct yetty_ycore_void_result destroy_res = yetty_yfigure_destroy(obj);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
}

static void test_envelope_atomicity(void)
{
    /* valid prefix + malformed suffix -> error AND no state; a later
     * valid envelope must not resurrect the failed prefix. */
    struct yetty_yclass_object *obj = make_scene();
    struct yetty_ydraw_drawable_list *poisoned = make_list();
    add_box(poisoned, 0, 0, 10, 10);
    struct yetty_ydraw_id_result group_res = yetty_ydraw_drawable_list_begin_group(poisoned, 5);
    if (YETTY_IS_ERR(group_res)) {
        exit(2);
    }
    add_box(poisoned, 20, 0, 10, 10);
    yetty_ydraw_drawable_list_end_group(poisoned, group_res.value);

    const uint8_t *bytes = (const uint8_t *)yetty_ydraw_drawable_list_data(poisoned);
    size_t full_len = yetty_ydraw_drawable_list_size(poisoned);
    /* Truncate mid-record: the trailing group record is cut short. */
    struct yetty_ycore_void_result feed_res =
        yetty_yfigure_process_bytes(obj, bytes, full_len - 5);
    CHECK("poisoned envelope rejected", YETTY_IS_ERR(feed_res));
    if (YETTY_IS_ERR(feed_res)) {
        yetty_ycore_error_destroy(feed_res.error);
    }

    /* A later CLEAN envelope commits — nothing from the failed prefix
     * (the 0,0 box, node 5) may appear. */
    struct yetty_ydraw_drawable_list *clean = make_list();
    add_box(clean, 50, 50, 10, 10);
    feed(obj, clean);
    CHECK("only the clean record published", leaf_count(obj) == 1);
    char *dump = dump_scene(obj);
    CHECK("prefix box absent", dump && strstr(dump, "aabb=(0.0,0.0,10.0,10.0)") == NULL);
    CHECK("prefix group absent", dump && strstr(dump, "node id=5") == NULL);
    free(dump);

    yetty_ydraw_drawable_list_destroy(poisoned);
    yetty_ydraw_drawable_list_destroy(clean);
    struct yetty_ycore_void_result destroy_res = yetty_yfigure_destroy(obj);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
}

static void test_view_scale_parity_and_plan(void)
{
    struct yetty_yclass_object *obj = make_scene();
    struct yetty_ydraw_drawable_list *box = make_list();
    add_box(box, 100, 100, 20, 20);
    CHECK_OK("declare", yetty_yscene_node_declare(obj, 1, 0));
    CHECK_OK("content", yetty_yscene_node_set_content(obj, 1, list_buffer(box)));
    {
        struct yetty_ycore_uint64_result commit_res = yetty_yscene_commit(obj);
        if (YETTY_IS_ERR(commit_res)) {
            yetty_ycore_error_destroy(commit_res.error);
        }
    }

    /* scale 2: document (110,110) shows at screen (220,220). */
    CHECK_OK("set scale 2", yetty_yscene_set_view_scale(obj, 2.0f));
    CHECK("hit at scaled screen point", hit(obj, 220, 220) == 1);
    CHECK("unscaled point misses", hit(obj, 110, 110) == 0);
    /* The render plan carries the same scale the hit-test used. */
    struct yetty_ycore_char_ptr_result plan_res = yetty_yscene_render_plan(obj);
    CHECK("plan built headlessly", YETTY_IS_OK(plan_res) && plan_res.value != NULL);
    if (YETTY_IS_OK(plan_res)) {
        CHECK("plan shows scale", strstr(plan_res.value, "scale=2.00") != NULL);
        CHECK("plan staged the prim", strstr(plan_res.value, "staged=1") != NULL);
        free(plan_res.value);
    } else {
        yetty_ycore_error_destroy(plan_res.error);
    }

    /* Extent change re-buckets staging without a dom generation change:
     * the plan's cell size must follow the new extent. */
    CHECK_OK("grow content extent",
             yetty_yfigure_set_content_size(obj, 1600.0f, 1200.0f));
    plan_res = yetty_yscene_render_plan(obj);
    CHECK("plan rebuilt for new extent",
          YETTY_IS_OK(plan_res) && strstr(plan_res.value, "extent=1600.0x1200.0") != NULL &&
              strstr(plan_res.value, "cell=100.00x75.00") != NULL);
    if (YETTY_IS_OK(plan_res)) {
        free(plan_res.value);
    } else {
        yetty_ycore_error_destroy(plan_res.error);
    }

    yetty_ydraw_drawable_list_destroy(box);
    struct yetty_ycore_void_result destroy_res = yetty_yfigure_destroy(obj);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
}

static void test_structural_rejections(void)
{
    struct yetty_yclass_object *obj = make_scene();

    /* CMD_UPDATE is rejected, not silently dropped. */
    struct yetty_ydraw_drawable_list *with_update = make_list();
    add_box(with_update, 0, 0, 4, 4);
    {
        uint32_t update_record[3] = {YETTY_YDRAW_CMD_UPDATE, 9, 0};
        struct yetty_ydraw_id_result raw_res = yetty_ydraw_drawable_list_add_prim(
            with_update, update_record, sizeof(update_record));
        if (YETTY_IS_ERR(raw_res)) {
            yetty_ycore_error_destroy(raw_res.error);
        }
    }
    struct yetty_ycore_void_result feed_res = yetty_yfigure_process_bytes(
        obj, (const uint8_t *)yetty_ydraw_drawable_list_data(with_update),
        yetty_ydraw_drawable_list_size(with_update));
    CHECK("CMD_UPDATE rejected", YETTY_IS_ERR(feed_res));
    if (YETTY_IS_ERR(feed_res)) {
        yetty_ycore_error_destroy(feed_res.error);
    }
    CHECK("rejected envelope left nothing", leaf_count(obj) == 0);

    /* Nested CMD_ZERO is rejected. */
    struct yetty_ydraw_drawable_list *nested_zero = make_list();
    struct yetty_ydraw_id_result group_res = yetty_ydraw_drawable_list_begin_group(nested_zero, 3);
    if (YETTY_IS_ERR(group_res)) {
        exit(2);
    }
    {
        struct yetty_ycore_void_result zero_res =
            yetty_ydraw_drawable_list_add_cmd_zero(nested_zero);
        if (YETTY_IS_ERR(zero_res)) {
            exit(2);
        }
    }
    yetty_ydraw_drawable_list_end_group(nested_zero, group_res.value);
    feed_res = yetty_yfigure_process_bytes(
        obj, (const uint8_t *)yetty_ydraw_drawable_list_data(nested_zero),
        yetty_ydraw_drawable_list_size(nested_zero));
    CHECK("nested CMD_ZERO rejected", YETTY_IS_ERR(feed_res));
    if (YETTY_IS_ERR(feed_res)) {
        yetty_ycore_error_destroy(feed_res.error);
    }

    yetty_ydraw_drawable_list_destroy(with_update);
    yetty_ydraw_drawable_list_destroy(nested_zero);
    struct yetty_ycore_void_result destroy_res = yetty_yfigure_destroy(obj);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
}

int main(void)
{
    test_typed_api_paint_order();
    test_world_inheritance_and_hit();
    test_wire_adapter();
    test_commit_isolation();
    test_envelope_atomicity();
    test_view_scale_parity_and_plan();
    test_structural_rejections();
    fprintf(stderr, "%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
