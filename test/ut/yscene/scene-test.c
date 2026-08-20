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

/* Hand-written vtermgrid seam (outside the generated header). */
struct yetty_ycore_uint64_result yetty_yscene_vtermgrid_bell_count(struct yetty_yclass_object *obj);
struct yetty_ycore_uint64_result yetty_yscene_vtermgrid_replies_discarded(
    struct yetty_yclass_object *obj);
#include "yetty/gen/impl/yscene/vtermgrid.h"
#include <yetty/api/ymux/attachment.h>
#include <yetty/api/ymux/engine.h>
#include <yetty/api/ymux/history.h>
#include <yetty/api/ymux/pane.h>
#include <yetty/api/ymux/projector.h>

#include "../../../src/yetty/ymux/proto.h"
#include <yetty/ycore/result.h>
#include <yetty/ydraw-list/cmds.h>
#include <yetty/ydraw-factory/complex-factory.h>
#include <yetty/ydraw-list/drawable-list.h>
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

/* Tear down a scene created by make_scene(): the scene is a yfigure figure, so
 * its destroy override (scene_destroy_slot) runs through yetty_yfigure_destroy.
 * Keeps the ASAN gate green (the scene owns its dom, staging, grid, surface). */
static void destroy_scene(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return;
    }
    struct yetty_ycore_void_result res = yetty_yfigure_destroy(obj);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
}

/* Read one grid cell, absorbing the (only-on-bad-object) error. */
static struct yetty_yscene_vtermgrid_cell grid_cell(struct yetty_yclass_object *grid, uint32_t row,
                                                    uint32_t col)
{
    struct yetty_yscene_vtermgrid_cell cell = {0};
    struct yetty_ycore_void_result res = yetty_yscene_vtermgrid_cell(grid, row, col, &cell);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
    return cell;
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

    /* Navigation regression: a full reset must drop the previous
     * document's view scroll — a stale offset strands the next page
     * above the viewport (the "clicked link shows a blank page" bug). */
    CHECK_OK("scroll before reset", yetty_yfigure_set_scroll(obj, 0, 400));
    struct yetty_ycore_void_result reset_res = yetty_yfigure_reset_content(obj);
    CHECK("reset_content", YETTY_IS_OK(reset_res));
    if (YETTY_IS_ERR(reset_res)) {
        yetty_ycore_error_destroy(reset_res.error);
    }
    CHECK_OK("re-declare after reset", yetty_yscene_node_declare(obj, 1, 0));
    CHECK_OK("re-content after reset", yetty_yscene_node_set_content(obj, 1, list_buffer(box)));
    {
        struct yetty_ycore_uint64_result commit_res = yetty_yscene_commit(obj);
        if (YETTY_IS_ERR(commit_res)) {
            yetty_ycore_error_destroy(commit_res.error);
        }
    }
    /* box spans document (0,0)-(10,10); with the stale scroll (y=400)
     * this screen point would map to document (5,405) and miss. */
    CHECK("fresh document visible at scroll 0", hit(obj, 5, 5) == 1);

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
    struct yetty_ycore_void_result feed_res =
        yetty_yfigure_process_bytes(obj, (const uint8_t *)yetty_ydraw_drawable_list_data(list),
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
        struct yetty_ycore_void_result zero_res = yetty_ydraw_drawable_list_add_cmd_zero(zero_list);
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
    struct yetty_ycore_void_result feed_res = yetty_yfigure_process_bytes(obj, bytes, full_len - 5);
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
    CHECK_OK("grow content extent", yetty_yfigure_set_content_size(obj, 1600.0f, 1200.0f));
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

    /* CMD_UPDATE is ROUTED to complex instances now; an unroutable id
     * (stale target — normal in streaming producers) drops with trace
     * while the rest of the envelope lands. */
    struct yetty_ydraw_drawable_list *with_update = make_list();
    add_box(with_update, 0, 0, 4, 4);
    {
        uint32_t update_record[3] = {YETTY_YDRAW_CMD_UPDATE, 9, 0};
        struct yetty_ydraw_id_result raw_res =
            yetty_ydraw_drawable_list_add_prim(with_update, update_record, sizeof(update_record));
        if (YETTY_IS_ERR(raw_res)) {
            yetty_ycore_error_destroy(raw_res.error);
        }
    }
    struct yetty_ycore_void_result feed_res = yetty_yfigure_process_bytes(
        obj, (const uint8_t *)yetty_ydraw_drawable_list_data(with_update),
        yetty_ydraw_drawable_list_size(with_update));
    CHECK("envelope with unroutable CMD_UPDATE lands", YETTY_IS_OK(feed_res));
    if (YETTY_IS_ERR(feed_res)) {
        yetty_ycore_error_destroy(feed_res.error);
    }
    CHECK("box record published", leaf_count(obj) == 1);

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

    /* The review-H2 counterexample: a group nested inside itself must
     * be rejected BEFORE any mutation. */
    struct yetty_ydraw_drawable_list *self_nested = make_list();
    add_box(self_nested, 0, 0, 4, 4);
    {
        struct yetty_ydraw_id_result outer_res =
            yetty_ydraw_drawable_list_begin_group(self_nested, 1);
        if (YETTY_IS_ERR(outer_res)) {
            exit(2);
        }
        struct yetty_ydraw_id_result inner_res =
            yetty_ydraw_drawable_list_begin_group(self_nested, 1);
        if (YETTY_IS_ERR(inner_res)) {
            exit(2);
        }
        yetty_ydraw_drawable_list_end_group(self_nested, inner_res.value);
        yetty_ydraw_drawable_list_end_group(self_nested, outer_res.value);
    }
    feed_res = yetty_yfigure_process_bytes(
        obj, (const uint8_t *)yetty_ydraw_drawable_list_data(self_nested),
        yetty_ydraw_drawable_list_size(self_nested));
    CHECK("self-nested group rejected pre-apply", YETTY_IS_ERR(feed_res));
    if (YETTY_IS_ERR(feed_res)) {
        yetty_ycore_error_destroy(feed_res.error);
    }
    CHECK("no prefix state from semantic reject", leaf_count(obj) == 1);
    /* Nothing was applied, so the scene is NOT poisoned: the next clean
     * envelope commits normally. */
    struct yetty_ydraw_drawable_list *after = make_list();
    add_box(after, 10, 10, 4, 4);
    feed(obj, after);
    CHECK("clean envelope after semantic reject", leaf_count(obj) == 2);

    yetty_ydraw_drawable_list_destroy(with_update);
    yetty_ydraw_drawable_list_destroy(nested_zero);
    yetty_ydraw_drawable_list_destroy(self_nested);
    yetty_ydraw_drawable_list_destroy(after);
    struct yetty_ycore_void_result destroy_res = yetty_yfigure_destroy(obj);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
}

/*---------------------------------------------------------------------------
 * Atomic content transactions (#695): terminal paint + rich body publish
 * together or not at all. Four scenarios: both halves; rich-only;
 * terminal-only; malformed/rejected halves leave EVERYTHING unpublished.
 *-------------------------------------------------------------------------*/

/* Wire literals (module-private ymux formats; mirrored here as the wire
 * contract the test pins). */
#define TEST_RICH_MAGIC 0x594D5052u
#define TEST_PAINT_MAGIC_FULL 0x594D5046u

static uint32_t rich_body_word(float value)
{
    uint32_t word;
    memcpy(&word, &value, sizeof(word));
    return word;
}

/* One rich record with one BOX prim anchored at (row, col). Returns the
 * total word count written. */
static size_t rich_body_build(uint32_t *words, uint32_t record_count, uint32_t first_row,
                              int corrupt_prim_type)
{
    size_t offset = 0;
    words[offset++] = TEST_RICH_MAGIC;
    words[offset++] = 1; /* version */
    words[offset++] = record_count;
    for (uint32_t index = 0; index < record_count; ++index) {
        words[offset++] = 100 + index; /* rich id lo */
        words[offset++] = 0;           /* rich id hi */
        words[offset++] = 1;           /* revision */
        words[offset++] = first_row + index;
        words[offset++] = 2;  /* anchor col */
        words[offset++] = 0;  /* flags */
        words[offset++] = 10; /* prim words: one BOX */
        words[offset++] = corrupt_prim_type ? 0xDEADBEEFu : YETTY_YSDF_BOX;
        words[offset++] = 0;                    /* layer */
        words[offset++] = 0xFF00FF00u;          /* fill */
        words[offset++] = 0;                    /* stroke */
        words[offset++] = 0;                    /* stroke width */
        words[offset++] = rich_body_word(4.0f); /* cx (anchor-relative) */
        words[offset++] = rich_body_word(9.0f); /* cy */
        words[offset++] = rich_body_word(4.0f); /* hw */
        words[offset++] = rich_body_word(9.0f); /* hh */
        words[offset++] = rich_body_word(0.0f); /* corner radius */
    }
    return offset;
}

/* A DOM-path (YPB1) rich frame with ONE record whose declared payload_words far
 * exceeds the buffer -> truncated. words[10] = 0x31425059 ("YPB1") routes it to
 * the DOM applier (which mutates the published world), exercising the pre-apply
 * validation. Returns the (short) total word count. */
static size_t dom_rich_truncated(uint32_t *words)
{
    size_t offset = 0;
    words[offset++] = TEST_RICH_MAGIC;
    words[offset++] = 1;           /* version */
    words[offset++] = 1;           /* one record */
    words[offset++] = 100;         /* id lo */
    words[offset++] = 0;           /* id hi */
    words[offset++] = 1;           /* revision */
    words[offset++] = 0;           /* row */
    words[offset++] = 0;           /* col */
    words[offset++] = 0;           /* flags */
    words[offset++] = 1000;        /* payload_words: LIES (far past the buffer) */
    words[offset++] = 0x31425059u; /* YPB1 magic -> DOM path */
    return offset;                 /* 11 words, but the record claims 1000 */
}

/* Build a VALID one-record DOM rich frame: a YPB1-wrapped drawable list
 * containing one box, anchored at (0,0). Returns the word count. */
static size_t dom_rich_valid(uint32_t *words, size_t cap, uint64_t rich_id,
                             struct yetty_ydraw_drawable_list *list)
{
    size_t list_bytes = yetty_ydraw_drawable_list_size(list);
    size_t list_words = list_bytes / sizeof(uint32_t);
    size_t offset = 0;
    words[offset++] = TEST_RICH_MAGIC;
    words[offset++] = 1;                                 /* version */
    words[offset++] = 1;                                 /* one record */
    words[offset++] = (uint32_t)(rich_id & 0xFFFFFFFFu); /* id lo */
    words[offset++] = (uint32_t)(rich_id >> 32);         /* id hi */
    words[offset++] = 1;                                 /* revision */
    words[offset++] = 0;                                 /* row */
    words[offset++] = 0;                                 /* col */
    words[offset++] = 0;                                 /* flags */
    words[offset++] = (uint32_t)(6 + list_words);        /* payload: YPB1 hdr + list */
    words[offset++] = 0x31425059u;                       /* YPB1 magic */
    words[offset++] = 0;                                 /* ypb1: version */
    words[offset++] = 0;                                 /* ypb1: reserved */
    words[offset++] = 0;                                 /* ypb1: reserved */
    words[offset++] = 0;                                 /* ypb1: reserved */
    words[offset++] = (uint32_t)list_bytes;              /* ypb1: byte count */
    if (offset + list_words > cap) {
        return 0;
    }
    memcpy(&words[offset], yetty_ydraw_drawable_list_data(list), list_bytes);
    return offset + list_words;
}

/* Review #11 P0: a malformed FIRST dom frame must not flip routing state — a
 * later flat rich frame still publishes via the flat path. */
static void test_failed_first_dom_then_flat_routing(void)
{
    struct yetty_yclass_object *obj = make_scene();

    uint32_t bad_dom[16];
    size_t bad_count = dom_rich_truncated(bad_dom);
    struct yetty_ycore_void_result bad_res =
        yetty_yscene_scene_apply_content_transaction(obj, bad_dom, bad_count);
    CHECK("malformed first DOM frame rejects", YETTY_IS_ERR(bad_res));
    if (YETTY_IS_ERR(bad_res)) {
        yetty_ycore_error_destroy(bad_res.error);
    }

    /* A FLAT rich frame (bare ysdf primitive payload, no YPB1 magic): with
     * rich_dom_active correctly NOT flipped, it must publish and be
     * hit-testable via the flat rich world. */
    struct yetty_ydraw_drawable_list *flat = make_list();
    add_box(flat, 0, 0, 40, 40);
    size_t flat_bytes = yetty_ydraw_drawable_list_size(flat);
    size_t flat_words = flat_bytes / sizeof(uint32_t);
    uint32_t frame[512];
    size_t offset = 0;
    frame[offset++] = TEST_RICH_MAGIC;
    frame[offset++] = 1;
    frame[offset++] = 1;
    frame[offset++] = 7; /* id lo */
    frame[offset++] = 0;
    frame[offset++] = 1;
    frame[offset++] = 0;
    frame[offset++] = 0;
    frame[offset++] = 0;
    frame[offset++] = (uint32_t)flat_words;
    memcpy(&frame[offset], yetty_ydraw_drawable_list_data(flat), flat_bytes);
    struct yetty_ycore_void_result flat_res =
        yetty_yscene_scene_apply_content_transaction(obj, frame, offset + flat_words);
    CHECK_OK("flat frame after failed DOM frame publishes", flat_res);

    yetty_ydraw_drawable_list_destroy(flat);
    destroy_scene(obj);
}

/* Review #11 P0: a mid-frame stage failure (record 2's id maps to the DOM's
 * rejected UINT64_MAX) rolls back record 1 — nothing of the failing frame is
 * ever committed, and the PREVIOUS world survives untouched. */
static void test_dom_apply_mid_frame_rollback(void)
{
    struct yetty_yclass_object *obj = make_scene();

    /* World A: one valid record — commits. */
    struct yetty_ydraw_drawable_list *box_a = make_list();
    add_box(box_a, 0, 0, 30, 30);
    uint32_t frame_a[512];
    size_t count_a = dom_rich_valid(frame_a, 512, 100, box_a);
    CHECK("frame A built", count_a > 0);
    CHECK_OK("world A applies",
             yetty_yscene_scene_apply_content_transaction(obj, frame_a, count_a));

    /* World B: record 1 valid (id 200), record 2's rich_id chosen so the
     * generation-tagged node id becomes UINT64_MAX — declare REJECTS it. */
    struct yetty_ydraw_drawable_list *box_b = make_list();
    add_box(box_b, 0, 0, 50, 50);
    size_t list_bytes = yetty_ydraw_drawable_list_size(box_b);
    size_t list_words = list_bytes / sizeof(uint32_t);
    uint32_t frame_b[1024];
    size_t offset = 0;
    frame_b[offset++] = TEST_RICH_MAGIC;
    frame_b[offset++] = 1;
    frame_b[offset++] = 2; /* two records */
    /* record 1: valid */
    frame_b[offset++] = 200;
    frame_b[offset++] = 0;
    frame_b[offset++] = 1;
    frame_b[offset++] = 0;
    frame_b[offset++] = 0;
    frame_b[offset++] = 0;
    frame_b[offset++] = (uint32_t)(6 + list_words);
    frame_b[offset++] = 0x31425059u;
    frame_b[offset++] = 0;
    frame_b[offset++] = 0;
    frame_b[offset++] = 0;
    frame_b[offset++] = 0;
    frame_b[offset++] = (uint32_t)list_bytes;
    memcpy(&frame_b[offset], yetty_ydraw_drawable_list_data(box_b), list_bytes);
    offset += list_words;
    /* record 2: id 0x7FFFFFFFFFFFFFFF -> node id UINT64_MAX under the stamp
     * bit (regardless of the generation tag) -> dom declare rejects. */
    frame_b[offset++] = 0xFFFFFFFFu;
    frame_b[offset++] = 0x7FFFFFFFu;
    frame_b[offset++] = 1;
    frame_b[offset++] = 0;
    frame_b[offset++] = 0;
    frame_b[offset++] = 0;
    frame_b[offset++] = (uint32_t)(6 + list_words);
    frame_b[offset++] = 0x31425059u;
    frame_b[offset++] = 0;
    frame_b[offset++] = 0;
    frame_b[offset++] = 0;
    frame_b[offset++] = 0;
    frame_b[offset++] = (uint32_t)list_bytes;
    memcpy(&frame_b[offset], yetty_ydraw_drawable_list_data(box_b), list_bytes);
    offset += list_words;

    struct yetty_ycore_void_result bad_res =
        yetty_yscene_scene_apply_content_transaction(obj, frame_b, offset);
    CHECK("mid-frame failure rejects the whole frame", YETTY_IS_ERR(bad_res));
    if (YETTY_IS_ERR(bad_res)) {
        yetty_ycore_error_destroy(bad_res.error);
    }

    /* World A survives; a follow-up GOOD frame still applies cleanly. */
    uint32_t frame_c[512];
    size_t count_c = dom_rich_valid(frame_c, 512, 300, box_a);
    CHECK("frame C built", count_c > 0);
    CHECK_OK("world C applies after the failed frame",
             yetty_yscene_scene_apply_content_transaction(obj, frame_c, count_c));

    yetty_ydraw_drawable_list_destroy(box_a);
    yetty_ydraw_drawable_list_destroy(box_b);
    destroy_scene(obj);
}

/* Review #11: alternate screen on the INDEPENDENT grid — entering alt
 * (\e[?1049h) gives a blank screen, drawing there does not touch the
 * primary, and leaving (\e[?1049l) restores the primary content exactly.
 * Plus DECSCA end-to-end: \e[1"q protects, DECSEL (\e[?K) spares, and the
 * six-codepoint cluster (base + 5 marks) survives the store round trip. */
static void test_terminal_grid_altscreen_and_decsca(void)
{
    struct yetty_yclass_object *obj = make_scene();
    CHECK_OK("grid create", yetty_yscene_scene_terminal_grid_create(obj, 4, 20, 9.0f, 18.0f));
    struct yetty_yclass_object *grid = yetty_yscene_scene_terminal_grid(obj).value;
    CHECK("grid", grid != NULL);

    CHECK_OK("primary text",
             yetty_yscene_scene_terminal_grid_write(obj, (const uint8_t *)"PRIMARY", 7));
    CHECK("primary visible", grid_cell(grid, 0, 0).glyph == 'P');

    CHECK_OK("enter alt",
             yetty_yscene_scene_terminal_grid_write(obj, (const uint8_t *)"\x1b[?1049h", 8));
    CHECK("alt starts blank", grid_cell(grid, 0, 0).glyph == 0);
    CHECK_OK("alt text",
             yetty_yscene_scene_terminal_grid_write(obj, (const uint8_t *)"\x1b[HALT", 7));
    CHECK("alt visible", grid_cell(grid, 0, 0).glyph == 'A');

    CHECK_OK("leave alt",
             yetty_yscene_scene_terminal_grid_write(obj, (const uint8_t *)"\x1b[?1049l", 8));
    CHECK("primary restored", grid_cell(grid, 0, 0).glyph == 'P');
    CHECK("primary column intact", grid_cell(grid, 0, 6).glyph == 'Y');

    /* DECSCA: protect 'S', unprotect, write 'U', selective-erase the line —
     * the protected cell survives, the unprotected one clears. */
    CHECK_OK("decsca line",
             yetty_yscene_scene_terminal_grid_write(
                 obj, (const uint8_t *)"\x1b[2;1H\x1b[1\"qS\x1b[0\"qU\x1b[2;1H\x1b[?K", 28));
    CHECK("protected survives DECSEL", grid_cell(grid, 1, 0).glyph == 'S');
    CHECK("unprotected erased by DECSEL", grid_cell(grid, 1, 1).glyph == 0);

    /* Pen inheritance across the altscreen switch (review #12): enter alt
     * under red SGR — text drawn in alt carries it; SGR 0 inside alt resets;
     * leaving restores the primary content untouched. */
    CHECK_OK("red then enter alt", yetty_yscene_scene_terminal_grid_write(
                                       obj, (const uint8_t *)"\x1b[31m\x1b[?1049h", 13));
    CHECK_OK("alt colored text",
             yetty_yscene_scene_terminal_grid_write(obj, (const uint8_t *)"\x1b[HR", 4));
    CHECK("alt text is red (pen followed the switch)",
          (grid_cell(grid, 0, 0).fg & 0x00FF0000u) == 0 &&
              (grid_cell(grid, 0, 0).fg & 0x000000FFu) != 0);
    CHECK_OK("sgr0 in alt then text",
             yetty_yscene_scene_terminal_grid_write(obj, (const uint8_t *)"\x1b[0mW", 5));
    CHECK_OK("leave alt",
             yetty_yscene_scene_terminal_grid_write(obj, (const uint8_t *)"\x1b[?1049l", 8));
    CHECK("primary still restored", grid_cell(grid, 0, 0).glyph == 'P');

    /* Selection span API (rendered inverted by the grid shader — the state
     * round-trip is what a headless test can pin). */
    CHECK_OK("set selection", yetty_yscene_vtermgrid_set_selection(grid, 0, 2, 1, 4, 1));
    CHECK_OK("clear selection", yetty_yscene_vtermgrid_set_selection(grid, 0, 0, 0, 0, 0));
    /* The scene-level RPC slot the bridge drives (production caller). */
    CHECK_OK("scene selection slot", yetty_yscene_set_terminal_selection(obj, 1, 0, 2, 5, 1));
    CHECK_OK("scene selection clear", yetty_yscene_set_terminal_selection(obj, 0, 0, 0, 0, 0));

    destroy_scene(obj);
}

/* Review #12: a REPOSITION frame must move the ACTIVE generation's nodes —
 * the untagged lookup regression froze anchored content during scroll after
 * the first full apply. Full YPB1 frame at row 0, reposition to row 2, and
 * the node's hit position must follow. */
static void test_dom_reposition_moves_active_generation(void)
{
    struct yetty_yclass_object *obj = make_scene();

    struct yetty_ydraw_drawable_list *box = make_list();
    add_box(box, 0, 0, 30, 30);
    uint32_t frame[512];
    size_t count = dom_rich_valid(frame, 512, 400, box);
    CHECK("frame built", count > 0);
    CHECK_OK("full frame applies", yetty_yscene_scene_apply_content_transaction(obj, frame, count));
    CHECK("content hits at origin", hit(obj, 5, 5) != 0);

    /* Producer REPOSITION frame: same record id, new anchor row 2, the
     * REPOSITION flag, no payload. */
    uint32_t repo[16];
    size_t offset = 0;
    repo[offset++] = TEST_RICH_MAGIC;
    repo[offset++] = 1;
    repo[offset++] = 1;
    repo[offset++] = 400; /* id lo */
    repo[offset++] = 0;
    repo[offset++] = 2;    /* revision */
    repo[offset++] = 2;    /* anchor row */
    repo[offset++] = 0;    /* anchor col */
    repo[offset++] = 0x2u; /* YMUX_RICH_FLAG_REPOSITION */
    repo[offset++] = 0;    /* no payload */
    CHECK_OK("reposition applies", yetty_yscene_scene_apply_content_transaction(obj, repo, offset));

    /* Cell height defaults to 18: row 2 = y 36. The old position is empty,
     * the new one hits. */
    CHECK("origin no longer hits", hit(obj, 5, 5) == 0);
    CHECK("node hits at the new anchor", hit(obj, 5, 41) != 0);

    yetty_ydraw_drawable_list_destroy(box);
    destroy_scene(obj);
}

/* Review #12: the rollback FAULT MATRIX — every fallible apply stage
 * (declare, transform, append, complex mint; then old-node retirement) is
 * forced to fail via the armed countdown, and each failure must leave the
 * previous world intact (or, for retirement, the DEFINED empty wipe), leak
 * no partial node, keep the complex registry consistent, and admit a
 * follow-up full frame. */
/* Review #14: the REAL complex-factory fault rig. A minimal concrete
 * factory mints REAL (heap, ops-wired) complex instances and can be armed
 * to fail the Nth mint; the transactional assertions are that a retirement
 * fault destroys every instance minted for the incoming world (registry
 * snapshot rollback), the outgoing world's instances survive, and scene
 * destroy releases every remaining instance (leak accounting = zero). */
enum { SCENE_TEST_COMPLEX_TYPE = 0x80754321 };

struct test_concrete_factory {
    struct yetty_ydraw_concrete_factory base;
    int mint_count;
    int fail_at_mint; /* 1-based; 0 = never fail */
    int live_instances;
    int destroy_count;
};

struct test_complex_instance {
    struct yetty_ydraw_complex base;
    struct test_concrete_factory *owner;
};

static void test_complex_instance_destroy(struct yetty_ydraw_complex *self)
{
    struct test_complex_instance *instance = (struct test_complex_instance *)self;
    ++instance->owner->destroy_count;
    --instance->owner->live_instances;
    free(instance);
}

static const struct yetty_ydraw_complex_ops *test_complex_instance_ops(void)
{
    static const struct yetty_ydraw_complex_ops ops = {
        .destroy = test_complex_instance_destroy,
    };
    return &ops;
}

static struct yetty_ydraw_complex_ptr_result test_factory_create_instance(
    struct yetty_ydraw_concrete_factory *self, const void *buffer_data, size_t size,
    uint32_t rolling_row)
{
    (void)buffer_data;
    (void)size;
    (void)rolling_row;
    struct test_concrete_factory *factory = (struct test_concrete_factory *)self;
    ++factory->mint_count;
    if (factory->fail_at_mint && factory->mint_count == factory->fail_at_mint) {
        return YETTY_ERR(yetty_ydraw_complex_ptr, "test factory: armed mint failure");
    }
    struct test_complex_instance *instance = calloc(1, sizeof(struct test_complex_instance));
    if (!instance) {
        return YETTY_ERR(yetty_ydraw_complex_ptr, "test factory: alloc");
    }
    instance->base.ops = test_complex_instance_ops();
    instance->base.type = SCENE_TEST_COMPLEX_TYPE;
    instance->base.factory = self;
    instance->owner = factory;
    ++factory->live_instances;
    return YETTY_OK(yetty_ydraw_complex_ptr, &instance->base);
}

static struct yetty_ycore_void_result test_factory_compile_pipeline(
    struct yetty_ydraw_concrete_factory *self, WGPUDevice device, WGPUQueue queue,
    WGPUTextureFormat target_format, struct yetty_ydraw_gpu_allocator *allocator)
{
    (void)self;
    (void)device;
    (void)queue;
    (void)target_format;
    (void)allocator;
    return YETTY_OK_VOID();
}

static void test_factory_destroy(struct yetty_ydraw_concrete_factory *self)
{
    (void)self; /* embedded in the test frame — nothing to free */
}

/* A rich frame with one hit-testable BOX record + `complex_count`
 * complex records (rich ids base_id, base_id+1, ...). */
static size_t dom_rich_with_complexes(uint32_t *words, size_t cap, uint64_t base_id,
                                       struct yetty_ydraw_drawable_list *list,
                                       uint32_t complex_count)
{
    size_t list_bytes = yetty_ydraw_drawable_list_size(list);
    size_t list_words = list_bytes / sizeof(uint32_t);
    size_t offset = 0;
    words[offset++] = TEST_RICH_MAGIC;
    words[offset++] = 1;                   /* version */
    words[offset++] = 1 + complex_count; /* records */
    /* Record 1: the drawable-list box (world-survival hit probe). */
    words[offset++] = (uint32_t)(base_id & 0xFFFFFFFFu);
    words[offset++] = (uint32_t)(base_id >> 32);
    words[offset++] = 1; /* revision */
    words[offset++] = 0; /* row */
    words[offset++] = 0; /* col */
    words[offset++] = 0; /* flags */
    words[offset++] = (uint32_t)(6 + list_words);
    words[offset++] = 0x31425059u; /* YPB1 magic */
    words[offset++] = 0;
    words[offset++] = 0;
    words[offset++] = 0;
    words[offset++] = 0;
    words[offset++] = (uint32_t)list_bytes;
    if (offset + list_words + (size_t)complex_count * 13 > cap) {
        return 0;
    }
    memcpy(&words[offset], yetty_ydraw_drawable_list_data(list), list_bytes);
    offset += list_words;
    for (uint32_t index = 0; index < complex_count; ++index) {
        uint64_t rich_id = base_id + 1 + index;
        words[offset++] = (uint32_t)(rich_id & 0xFFFFFFFFu);
        words[offset++] = (uint32_t)(rich_id >> 32);
        words[offset++] = 1;     /* revision */
        words[offset++] = 0;     /* row */
        words[offset++] = 0;     /* col */
        words[offset++] = 0;     /* flags */
        words[offset++] = 6 + 6; /* payload: YPB1 hdr + complex record */
        words[offset++] = 0x31425059u;
        words[offset++] = 0;
        words[offset++] = 0;
        words[offset++] = 0;
        words[offset++] = 0;
        words[offset++] = 6 * sizeof(uint32_t); /* record bytes */
        words[offset++] = SCENE_TEST_COMPLEX_TYPE;
        words[offset++] = 4 * sizeof(uint32_t); /* payload: the bounds */
        float bounds[4] = {0.0f, 0.0f, 24.0f, 24.0f};
        memcpy(&words[offset], bounds, sizeof(bounds));
        offset += 4;
    }
    return offset;
}

static void test_complex_factory_fault_rig(void)
{
    struct yetty_ydraw_complex_factory_ptr_result factory_res =
        yetty_ydraw_complex_factory_create(NULL, NULL, 0, NULL, NULL);
    CHECK("complex factory created", YETTY_IS_OK(factory_res));
    if (YETTY_IS_ERR(factory_res)) {
        return;
    }
    struct yetty_ydraw_complex_factory *factory = factory_res.value;
    struct test_concrete_factory concrete = {
        .base = {.type_id = SCENE_TEST_COMPLEX_TYPE,
                 .destroy = test_factory_destroy,
                 .compile_pipeline = test_factory_compile_pipeline,
                 .create_instance = test_factory_create_instance},
    };
    CHECK_OK("concrete registered",
             yetty_ydraw_complex_factory_register(factory, &concrete.base));

    struct yetty_ydraw_drawable_list *box = make_list();
    add_box(box, 0, 0, 30, 30);
    struct yetty_yclass_object *obj = make_scene();
    CHECK_OK("factory wired", yetty_yscene_set_complex_factory(obj, factory));

    /* World A: box + TWO complex subtrees — real instances minted. */
    uint32_t frame_a[512];
    size_t count_a = dom_rich_with_complexes(frame_a, 512, 500, box, 2);
    CHECK("A built", count_a > 0);
    CHECK_OK("world A applies",
             yetty_yscene_scene_apply_content_transaction(obj, frame_a, count_a));
    CHECK("A hits", hit(obj, 5, 5) != 0);
    /* Complexes mint AT APPLY (the stage-4 batch scan). */
    CHECK("A minted two real instances", concrete.live_instances == 2);

    /* Retirement fault with MULTIPLE old subtrees: world B mints its own
     * instances during staging; the armed stage-5 failure must destroy
     * EXACTLY those (registry snapshot rollback) and preserve world A —
     * dom subtrees AND live complex instances. */
    /* The seam countdown is PER CALL: 3 records x 4 stage seams + 1 puts
     * the fault on the RETIRE seam — after B's complexes have minted. */
    CHECK_OK("arm retire fault", yetty_yscene_scene_rich_fault_arm(obj, 13));
    uint32_t frame_b[512];
    size_t count_b = dom_rich_with_complexes(frame_b, 512, 600, box, 2);
    struct yetty_ycore_void_result fail_res =
        yetty_yscene_scene_apply_content_transaction(obj, frame_b, count_b);
    CHECK("retire fault rejects B", YETTY_IS_ERR(fail_res));
    if (YETTY_IS_ERR(fail_res)) {
        yetty_ycore_error_destroy(fail_res.error);
    }
    CHECK("world A still hits", hit(obj, 5, 5) != 0);
    /* B's two instances minted during its stage loop; the retire-fault
     * rollback (registry snapshot restore) must destroy EXACTLY those and
     * leave A's live — real ops-wired destroys, counted. */
    CHECK("B's instances minted then destroyed by rollback",
          concrete.mint_count == 4 && concrete.destroy_count == 2);
    CHECK("A's instances survive", concrete.live_instances == 2);

    /* An armed FACTORY failure at mint is per-record best-effort (the
     * record drops, the frame lands, the OTHER records mint). */
    concrete.fail_at_mint = concrete.mint_count + 1;
    uint32_t frame_c[512];
    size_t count_c = dom_rich_with_complexes(frame_c, 512, 700, box, 2);
    CHECK_OK("world C applies despite factory mint failure",
             yetty_yscene_scene_apply_content_transaction(obj, frame_c, count_c));
    CHECK("C hits", hit(obj, 5, 5) != 0);
    concrete.fail_at_mint = 0;
    /* Retired worlds' instances die at the next STAGING rebuild (the sweep
     * is render-path work): force one and A's two retired instances go,
     * leaving exactly C's surviving mint. */
    struct yetty_ycore_char_ptr_result plan_res = yetty_yscene_render_plan(obj);
    if (YETTY_IS_OK(plan_res)) {
        free(plan_res.value);
    } else {
        yetty_ycore_error_destroy(plan_res.error);
    }
    CHECK("retired instances swept at staging rebuild",
          concrete.live_instances == 1 && concrete.destroy_count == 4);

    /* Scene destroy releases every remaining instance — zero leaks. */
    destroy_scene(obj);
    CHECK("every instance released at scene destroy", concrete.live_instances == 0);
    yetty_ydraw_drawable_list_destroy(box);
    yetty_ydraw_complex_factory_destroy(factory);
}

static void test_dom_rollback_fault_matrix(void)
{
    struct yetty_ydraw_drawable_list *box = make_list();
    add_box(box, 0, 0, 30, 30);

    /* Stages 1..4 fire inside the STAGE loop for a one-record frame:
     * declare, transform, append, mint. Each must roll back completely. */
    for (int stage = 1; stage <= 4; ++stage) {
        struct yetty_yclass_object *obj = make_scene();
        uint32_t frame_a[512];
        size_t count_a = dom_rich_valid(frame_a, 512, 500, box);
        CHECK("A built", count_a > 0);
        CHECK_OK("world A applies",
                 yetty_yscene_scene_apply_content_transaction(obj, frame_a, count_a));
        CHECK("A hits", hit(obj, 5, 5) != 0);

        CHECK_OK("arm fault", yetty_yscene_scene_rich_fault_arm(obj, stage));
        uint32_t frame_b[512];
        size_t count_b = dom_rich_valid(frame_b, 512, 600, box);
        struct yetty_ycore_void_result fail_res =
            yetty_yscene_scene_apply_content_transaction(obj, frame_b, count_b);
        CHECK("faulted stage rejects the frame", YETTY_IS_ERR(fail_res));
        if (YETTY_IS_ERR(fail_res)) {
            yetty_ycore_error_destroy(fail_res.error);
        }
        /* The OLD world survives; nothing of B is committed. */
        CHECK("world A survives the faulted stage", hit(obj, 5, 5) != 0);

        /* A follow-up frame applies cleanly (no staged leftovers). */
        uint32_t frame_c[512];
        size_t count_c = dom_rich_valid(frame_c, 512, 700, box);
        CHECK_OK("world C applies after the fault",
                 yetty_yscene_scene_apply_content_transaction(obj, frame_c, count_c));
        CHECK("world C hits", hit(obj, 5, 5) != 0);
        destroy_scene(obj);
    }

    /* Stage 5 = RETIREMENT (review #14): one transaction-wide atomic
     * delete — any failure preserves the OLD world completely. The former
     * empty-wipe fallback no longer exists (no post-collection fallible
     * step remains). */
    {
        struct yetty_yclass_object *obj = make_scene();
        uint32_t frame_a[512];
        size_t count_a = dom_rich_valid(frame_a, 512, 500, box);
        CHECK_OK("world A applies",
                 yetty_yscene_scene_apply_content_transaction(obj, frame_a, count_a));
        CHECK_OK("arm preflight fault", yetty_yscene_scene_rich_fault_arm(obj, 5));
        uint32_t frame_b[512];
        size_t count_b = dom_rich_valid(frame_b, 512, 600, box);
        struct yetty_ycore_void_result fail_res =
            yetty_yscene_scene_apply_content_transaction(obj, frame_b, count_b);
        CHECK("preflight fault rejects", YETTY_IS_ERR(fail_res));
        if (YETTY_IS_ERR(fail_res)) {
            yetty_ycore_error_destroy(fail_res.error);
        }
        CHECK("old world PRESERVED across the preflight fault", hit(obj, 5, 5) != 0);
        destroy_scene(obj);
    }

    yetty_ydraw_drawable_list_destroy(box);
}

/* Review #13: the receiver barrier — a discarded epoch may end mid-CSI and
 * inside the alternate screen; recreating the grid (the resync/reset op)
 * yields a VIRGIN receiver (fresh parser, primary screen), and the fresh
 * epoch's complete redraw renders cleanly. */
static void test_terminal_grid_recreate_resets_parser_and_modes(void)
{
    struct yetty_yclass_object *obj = make_scene();
    CHECK_OK("grid create", yetty_yscene_scene_terminal_grid_create(obj, 4, 20, 9.0f, 18.0f));
    struct yetty_yclass_object *grid = yetty_yscene_scene_terminal_grid(obj).value;
    CHECK("grid", grid != NULL);

    /* Poison the receiver: enter the alternate screen and stop MID-CSI. */
    CHECK_OK("alt + partial CSI", yetty_yscene_scene_terminal_grid_write(
                                      obj, (const uint8_t *)"\x1b[?1049hX\x1b[3", 12));

    /* The barrier: recreate the grid (what attach_reset_receiver issues). */
    CHECK_OK("grid recreate", yetty_yscene_scene_terminal_grid_create(obj, 4, 20, 9.0f, 18.0f));
    struct yetty_yclass_object *fresh = yetty_yscene_scene_terminal_grid(obj).value;
    CHECK("fresh grid", fresh != NULL);

    /* A clean redraw renders text at home — no half-parsed CSI eats the
     * bytes, no stale alternate screen hides them. */
    CHECK_OK("fresh redraw",
             yetty_yscene_scene_terminal_grid_write(obj, (const uint8_t *)"\x1b[HOK", 5));
    CHECK("clean render", grid_cell(fresh, 0, 0).glyph == 'O');
    CHECK("clean render col1", grid_cell(fresh, 0, 1).glyph == 'K');

    destroy_scene(obj);
}

/* Atomicity of the DOM rich path (#699.3: rich-only — the paint half is
 * retired): a malformed rich body must reject the whole transaction without
 * mutating the published world, and a transaction carrying paint words is a
 * protocol violation. */
static void test_content_transaction_dom_atomicity(void)
{
    struct yetty_yclass_object *obj = make_scene();

    uint32_t dom_rich[16];
    size_t dom_rich_count = dom_rich_truncated(dom_rich);
    struct yetty_ycore_void_result txn =
        yetty_yscene_scene_apply_content_transaction(obj, dom_rich, dom_rich_count);
    CHECK("malformed DOM rich rejects the transaction", YETTY_IS_ERR(txn));
    if (YETTY_IS_ERR(txn)) {
        yetty_ycore_error_destroy(txn.error);
    }

    destroy_scene(obj);
}

/* Atomicity of terminal_write_content (#699.6 review): valid VT bytes paired
 * with a MALFORMED rich body must change NEITHER half — the fallible rich
 * validation runs before the terminal feed, so the grid text is untouched. */
static void test_terminal_write_content_rejects_bad_rich(void)
{
    struct yetty_yclass_object *obj = make_scene();
    CHECK_OK("grid create", yetty_yscene_scene_terminal_grid_create(obj, 3, 12, 9.0f, 18.0f));

    uint32_t dom_rich[16];
    size_t dom_rich_count = dom_rich_truncated(dom_rich);
    struct yetty_ycore_void_result txn = yetty_yscene_scene_terminal_write_content(
        obj, (const uint8_t *)"XY", 2, dom_rich, dom_rich_count);
    CHECK("bad rich rejects the whole write", YETTY_IS_ERR(txn));
    if (YETTY_IS_ERR(txn)) {
        yetty_ycore_error_destroy(txn.error);
    }
    /* The terminal half did NOT apply: cell (0,0) is still blank. */
    struct yetty_yclass_object *grid = yetty_yscene_scene_terminal_grid(obj).value;
    CHECK("grid", grid != NULL);
    if (grid) {
        struct yetty_yscene_vtermgrid_cell cell = {0};
        CHECK_OK("cell read", yetty_yscene_vtermgrid_cell(grid, 0, 0, &cell));
        CHECK("terminal untouched by rejected transaction", cell.glyph == 0);
    }
    destroy_scene(obj);
}

/* #699/#4 overlay-first input consumption: the hit_opaque virtual slot. An
 * EMPTY scene is input-transparent everywhere (an overlay with no chrome
 * yields the whole pane to the content below); staged dom content makes it
 * opaque exactly where the dom hits; a scene hosting the terminal grid is
 * opaque everywhere (the terminal consumes its whole pane). */
static int hit_opaque(struct yetty_yclass_object *obj, float x, float y)
{
    struct yetty_ycore_int_result opaque_res = yetty_yfigure_hit_opaque(obj, x, y);
    if (YETTY_IS_ERR(opaque_res)) {
        yetty_ycore_error_destroy(opaque_res.error);
        return -1;
    }
    return opaque_res.value;
}

/* Review #17: the PRODUCTION overlay chrome frame — the exact shape the
 * attach bridge stages (scroll-indicator strip, top-right). It must apply,
 * consume hits AT the strip, pass through beside it, and a press on it
 * must claim key focus. */
static void test_production_chrome_frame(void)
{
    struct yetty_yclass_object *overlay = make_scene();
    uint32_t cols = 80;
    float cell_w = 9.0f, cell_h = 18.0f;
    float strip_w = 6.0f * cell_w;
    float strip_x = (float)cols * cell_w - strip_w - cell_w * 0.5f;
    struct yetty_ydraw_drawable_list *list = make_list();
    struct yetty_ysdf_box geometry = {
        .center_x = strip_x + strip_w * 0.5f,
        .center_y = cell_h * 0.5f,
        .half_width = strip_w * 0.5f,
        .half_height = cell_h * 0.45f,
        .corner_radius = 3.0f,
    };
    CHECK_OK("indicator box", yetty_ydraw_drawable_list_add_cmd_add_box(list, 0, 0, 0xff92a86bu, 0,
                                                                        0.0f, &geometry));
    size_t list_bytes = yetty_ydraw_drawable_list_size(list);
    size_t list_words = list_bytes / sizeof(uint32_t);
    uint32_t words[512];
    size_t offset = 0;
    words[offset++] = TEST_RICH_MAGIC;
    words[offset++] = 1;
    words[offset++] = 1;
    words[offset++] = 0x43524F4Du;
    words[offset++] = 0;
    words[offset++] = 1;
    words[offset++] = 0;
    words[offset++] = 0;
    words[offset++] = 0;
    words[offset++] = (uint32_t)(6 + list_words);
    words[offset++] = 0x31425059u;
    words[offset++] = 0;
    words[offset++] = 0;
    words[offset++] = 0;
    words[offset++] = 0;
    words[offset++] = (uint32_t)list_bytes;
    memcpy(&words[offset], yetty_ydraw_drawable_list_data(list), list_bytes);
    offset += list_words;
    CHECK_OK("chrome frame applies",
             yetty_yscene_scene_apply_content_transaction(overlay, words, offset));
    CHECK("indicator consumes", hit_opaque(overlay, (uint32_t)(strip_x + strip_w * 0.5f),
                                           (uint32_t)(cell_h * 0.5f)) == 1);
    CHECK("beside the strip falls through", hit_opaque(overlay, 40, 300) == 0);
    struct yetty_ycore_uint64_result press_res = yetty_yscene_scene_dispatch_pointer(
        overlay, (uint32_t)(strip_x + strip_w * 0.5f), (uint32_t)(cell_h * 0.5f), 1, 0, 0, 1);
    CHECK("press on the indicator claims focus",
          YETTY_IS_OK(press_res) && press_res.value != 0 &&
              yetty_yscene_scene_key_focus(overlay).value == 1);
    {
        struct yetty_ycore_char_ptr_result plan_res = yetty_yscene_render_plan(overlay);
        if (YETTY_IS_OK(plan_res)) {
            fprintf(stderr, "CHROME PLAN:\n%s\n", plan_res.value);
            free(plan_res.value);
        }
    }
    yetty_ydraw_drawable_list_destroy(list);
    destroy_scene(overlay);
}

static void test_hit_opaque_overlay_routing(void)
{
    struct yetty_yclass_object *overlay = make_scene();

    /* Empty overlay: transparent at every point. */
    CHECK("empty overlay transparent", hit_opaque(overlay, 15, 15) == 0);
    CHECK("empty overlay transparent (far)", hit_opaque(overlay, 500, 400) == 0);

    /* Stage chrome (a 20x20 box at 10,10): opaque exactly where it hits. */
    struct yetty_ydraw_drawable_list *chrome = make_list();
    add_box(chrome, 10, 10, 20, 20);
    CHECK_OK("declare chrome", yetty_yscene_node_declare(overlay, 1, 0));
    CHECK_OK("chrome content", yetty_yscene_node_set_content(overlay, 1, list_buffer(chrome)));
    {
        struct yetty_ycore_uint64_result commit_res = yetty_yscene_commit(overlay);
        if (YETTY_IS_ERR(commit_res)) {
            yetty_ycore_error_destroy(commit_res.error);
        }
    }
    CHECK_OK("derive overlay", yetty_yscene_derive(overlay));
    CHECK("chrome point consumes", hit_opaque(overlay, 15, 15) == 1);
    CHECK("off-chrome point falls through", hit_opaque(overlay, 500, 400) == 0);

    /* Production pointer DISPATCH (review #12): a consumed pointer event on
     * staged chrome resolves to the hit node and records the event (serial
     * advances); off-chrome dispatch records a zero node. */
    CHECK("no event yet", yetty_yscene_scene_pointer_event_serial(overlay).value == 0);
    struct yetty_ycore_uint64_result dispatch_res =
        yetty_yscene_scene_dispatch_pointer(overlay, 15, 15, 1, 0, 0, 1);
    CHECK("dispatch on chrome ok", YETTY_IS_OK(dispatch_res));
    CHECK("chrome node resolved", YETTY_IS_OK(dispatch_res) && dispatch_res.value != 0);
    CHECK("serial advanced", yetty_yscene_scene_pointer_event_serial(overlay).value == 1);
    /* A consumed press CLAIMS key focus for the chrome (review #14). */
    CHECK("press claims key focus", yetty_yscene_scene_key_focus(overlay).value == 1);

    /* Key/paste dispatch (review #14): with focus held, events are CONSUMED
     * and enter the ordered queue the chrome consumer drains. */
    CHECK("no key event yet", yetty_yscene_scene_key_event_serial(overlay).value == 0);
    struct yetty_ycore_buffer key_bytes = {.data = (uint8_t *)"q", .size = 1, .capacity = 1};
    struct yetty_ycore_uint32_result key_dispatch_res =
        yetty_yscene_dispatch_key(overlay, 2 /* KEY */, key_bytes);
    CHECK("dispatch key consumed", YETTY_IS_OK(key_dispatch_res) && key_dispatch_res.value == 1);
    CHECK("key serial advanced", yetty_yscene_scene_key_event_serial(overlay).value == 1);
    struct yetty_ycore_buffer paste_bytes = {
        .data = (uint8_t *)"\x1b[200~p\x1b[201~", .size = 14, .capacity = 14};
    struct yetty_ycore_uint32_result paste_dispatch_res =
        yetty_yscene_dispatch_key(overlay, 3 /* PASTE */, paste_bytes);
    CHECK("dispatch paste consumed",
          YETTY_IS_OK(paste_dispatch_res) && paste_dispatch_res.value == 1);
    CHECK("paste serial advanced", yetty_yscene_scene_key_event_serial(overlay).value == 2);

    /* The chrome consumer drains the queue IN ORDER. */
    uint32_t taken_class = 0;
    uint8_t taken_bytes[48];
    struct yetty_ycore_int_result take_res = yetty_yscene_scene_take_input_event(
        overlay, &taken_class, taken_bytes, sizeof(taken_bytes));
    CHECK("first taken = key", YETTY_IS_OK(take_res) && take_res.value == 1 && taken_class == 2 &&
                                   taken_bytes[0] == 'q');
    take_res = yetty_yscene_scene_take_input_event(overlay, &taken_class, taken_bytes,
                                                   sizeof(taken_bytes));
    CHECK("second taken = paste",
          YETTY_IS_OK(take_res) && take_res.value == 14 && taken_class == 3);
    take_res = yetty_yscene_scene_take_input_event(overlay, &taken_class, taken_bytes,
                                                   sizeof(taken_bytes));
    CHECK("queue drained", YETTY_IS_OK(take_res) && take_res.value == -1);

    /* LOSSLESS queue (review #15): a paste longer than any fixed head must
     * round-trip byte-exact. A short drain buffer pops NOTHING (the return
     * is the required size); the retry with a big enough buffer pops the
     * full payload. */
    {
        uint8_t long_paste[200];
        long_paste[0] = 0x1b;
        for (size_t fill = 1; fill < sizeof(long_paste); ++fill) {
            long_paste[fill] = (uint8_t)('a' + (fill % 26));
        }
        struct yetty_ycore_buffer long_buffer = {
            .data = long_paste, .size = sizeof(long_paste), .capacity = sizeof(long_paste)};
        struct yetty_ycore_uint32_result long_res =
            yetty_yscene_dispatch_key(overlay, 3 /* PASTE */, long_buffer);
        CHECK("long paste consumed", YETTY_IS_OK(long_res) && long_res.value == 1);
        uint8_t small[32];
        uint32_t long_class = 0;
        struct yetty_ycore_int_result short_take =
            yetty_yscene_scene_take_input_event(overlay, &long_class, small, sizeof(small));
        CHECK("short buffer reports required size, keeps event",
              YETTY_IS_OK(short_take) && short_take.value == 200);
        uint8_t big[256];
        struct yetty_ycore_int_result full_take =
            yetty_yscene_scene_take_input_event(overlay, &long_class, big, sizeof(big));
        CHECK("full take byte-exact", YETTY_IS_OK(full_take) && full_take.value == 200 &&
                                          memcmp(big, long_paste, sizeof(long_paste)) == 0 &&
                                          long_class == 3);
        /* FULL queue = backpressure: the 17th dispatch reports UNCONSUMED
         * (falls through to the daemon) — nothing is silently dropped. */
        struct yetty_ycore_buffer one = {.data = (uint8_t *)"k", .size = 1, .capacity = 1};
        for (int fill_queue = 0; fill_queue < 16; ++fill_queue) {
            struct yetty_ycore_uint32_result fill_res = yetty_yscene_dispatch_key(overlay, 2, one);
            CHECK("fill consumed", YETTY_IS_OK(fill_res) && fill_res.value == 1);
        }
        struct yetty_ycore_uint32_result overflow_res = yetty_yscene_dispatch_key(overlay, 2, one);
        CHECK("full queue backpressures (unconsumed)",
              YETTY_IS_OK(overflow_res) && overflow_res.value == 0);
        for (int drain_queue = 0; drain_queue < 16; ++drain_queue) {
            struct yetty_ycore_int_result drain_res =
                yetty_yscene_scene_take_input_event(overlay, &long_class, big, sizeof(big));
            CHECK("drained", YETTY_IS_OK(drain_res) && drain_res.value == 1);
        }
    }

    /* A press that falls THROUGH releases key focus — dispatch then reports
     * UNCONSUMED and queues nothing (the bridge falls the bytes through). */
    struct yetty_ycore_uint64_result miss_res =
        yetty_yscene_scene_dispatch_pointer(overlay, 500, 400, 1, 0, 0, 1);
    CHECK("dispatch off chrome ok", YETTY_IS_OK(miss_res));
    CHECK("off-chrome resolves zero", YETTY_IS_OK(miss_res) && miss_res.value == 0);
    CHECK("fall-through press releases focus", yetty_yscene_scene_key_focus(overlay).value == 0);
    struct yetty_ycore_uint32_result unfocused_res =
        yetty_yscene_dispatch_key(overlay, 2 /* KEY */, key_bytes);
    CHECK("unfocused dispatch unconsumed", YETTY_IS_OK(unfocused_res) && unfocused_res.value == 0);
    take_res = yetty_yscene_scene_take_input_event(overlay, &taken_class, taken_bytes,
                                                   sizeof(taken_bytes));
    CHECK("unconsumed not queued", YETTY_IS_OK(take_res) && take_res.value == -1);

    /* Focus release must NOT strand a consumed event (review #16): consume
     * with focus held, then release focus — the queued event still drains.
     * Focus gates admission, never delivery. */
    struct yetty_ycore_uint64_result reclaim_res =
        yetty_yscene_scene_dispatch_pointer(overlay, 15, 15, 1, 0, 0, 1);
    CHECK("focus reclaimed", YETTY_IS_OK(reclaim_res) && reclaim_res.value != 0);
    struct yetty_ycore_uint32_result stranded_res =
        yetty_yscene_dispatch_key(overlay, 2, key_bytes);
    CHECK("event consumed pre-release", YETTY_IS_OK(stranded_res) && stranded_res.value == 1);
    struct yetty_ycore_uint64_result release_res =
        yetty_yscene_scene_dispatch_pointer(overlay, 500, 400, 1, 0, 0, 1);
    CHECK("focus released", YETTY_IS_OK(release_res) && release_res.value == 0);
    take_res = yetty_yscene_scene_take_input_event(overlay, &taken_class, taken_bytes,
                                                   sizeof(taken_bytes));
    CHECK("consumed event drains AFTER focus release",
          YETTY_IS_OK(take_res) && take_res.value == 1 && taken_bytes[0] == 'q');

    /* A scene hosting the terminal grid consumes its whole pane. */
    struct yetty_yclass_object *content = make_scene();
    CHECK_OK("grid create", yetty_yscene_scene_terminal_grid_create(content, 3, 12, 9.0f, 18.0f));
    CHECK("grid scene opaque anywhere", hit_opaque(content, 500, 400) == 1);

    yetty_ydraw_drawable_list_destroy(chrome);
    destroy_scene(content);
    destroy_scene(overlay);
}

/* #699/#4 atomic publish: terminal_write_content feeds the grid AND applies the
 * rich body in one call — both land together (a frame never shows one half). */
static void test_terminal_write_content_atomic(void)
{
    struct yetty_yclass_object *obj = make_scene();
    CHECK_OK("grid create", yetty_yscene_scene_terminal_grid_create(obj, 3, 12, 9.0f, 18.0f));

    uint32_t rich[64];
    size_t rich_count = rich_body_build(rich, 1, 0, 0);
    CHECK_OK("atomic content", yetty_yscene_scene_terminal_write_content(obj, (const uint8_t *)"Hi",
                                                                         2, rich, rich_count));

    /* Both halves published: the grid shows the text AND the rich world grew. */
    struct yetty_yclass_object_ptr_result grid_res = yetty_yscene_scene_terminal_grid(obj);
    CHECK("grid present", YETTY_IS_OK(grid_res));
    if (YETTY_IS_OK(grid_res)) {
        struct yetty_yscene_vtermgrid_cell cell = grid_cell(grid_res.value, 0, 0);
        CHECK("atomic: grid text landed", cell.glyph == 'H');
    } else {
        yetty_ycore_error_destroy(grid_res.error);
    }
    CHECK("atomic: rich landed", yetty_yscene_scene_rich_entry_count(obj).value == 1);
    destroy_scene(obj);
}

/*===========================================================================
 * Independent client terminal grid (#699): the scene embeds a
 * class@yscene:vtermgrid, driven by ordinary terminal bytes through
 * terminal_grid_create + terminal_grid_write, and the embedded libvterm
 * produces the correct cell grid (verified through the borrowed grid object).
 *=========================================================================*/
static void test_terminal_grid_embed(void)
{
    struct yetty_yclass_object *obj = make_scene();

    CHECK_OK("grid create", yetty_yscene_scene_terminal_grid_create(obj, 10, 40, 9.0f, 18.0f));
    CHECK_OK("grid write", yetty_yscene_scene_terminal_grid_write(
                               obj, (const uint8_t *)"Hello\r\n\x1b[31mWorld", 17));

    /* BEL routing (review #10: .bell was NULL — dropped): a BEL in the fed
     * stream advances the grid's bell counter. */
    struct yetty_yclass_object *bell_grid = yetty_yscene_scene_terminal_grid(obj).value;
    CHECK("grid for bell", bell_grid != NULL);
    if (bell_grid) {
        uint64_t bells_before = yetty_yscene_vtermgrid_bell_count(bell_grid).value;
        CHECK_OK("bell write",
                 yetty_yscene_scene_terminal_grid_write(obj, (const uint8_t *)"\a", 1));
        CHECK("bell counted",
              yetty_yscene_vtermgrid_bell_count(bell_grid).value == bells_before + 1);

        /* Reply DRAIN over the scalar-word shape (review #15) — the exact
         * calls the bridge polls: pending -> words -> consume. */
        CHECK_OK("drain query write",
                 yetty_yscene_scene_terminal_grid_write(obj, (const uint8_t *)"\x1b[6n", 4));
        struct yetty_ycore_uint32_result drain_pending_res =
            yetty_yscene_terminal_reply_pending(obj);
        CHECK("drain pending", YETTY_IS_OK(drain_pending_res) && drain_pending_res.value >= 6);
        if (YETTY_IS_OK(drain_pending_res) && drain_pending_res.value >= 6) {
            uint32_t drain_len = drain_pending_res.value;
            uint8_t drained[64];
            for (uint32_t offset = 0; offset < drain_len && offset < 64; offset += 8) {
                struct yetty_ycore_uint64_result word_res =
                    yetty_yscene_terminal_reply_word(obj, offset / 8);
                CHECK("drain word", YETTY_IS_OK(word_res));
                uint32_t chunk = drain_len - offset < 8 ? drain_len - offset : 8;
                for (uint32_t byte_index = 0; byte_index < chunk; ++byte_index) {
                    drained[offset + byte_index] = (uint8_t)(word_res.value >> (byte_index * 8));
                }
            }
            CHECK("drained CPR", drained[0] == 0x1b && drained[1] == '[');
            CHECK_OK("drain consume", yetty_yscene_terminal_reply_consume(obj, drain_len));
            struct yetty_ycore_uint32_result after_res = yetty_yscene_terminal_reply_pending(obj);
            CHECK("drain empty after consume", YETTY_IS_OK(after_res) && after_res.value == 0);
        }

        /* Terminal replies: the projection ACCUMULATES them (review #14 — the
         * embedder can route grid-parsed replies through attachment input;
         * in production the projected stream carries no queries, so this
         * stays empty). DSR here produces a captured CPR the take API
         * returns; the daemon engine remains the authority at the pane. */
        uint8_t captured[64];
        CHECK_OK("query write",
                 yetty_yscene_scene_terminal_grid_write(obj, (const uint8_t *)"\x1b[6n", 4));
        struct yetty_ycore_uint32_result take_res =
            yetty_yscene_vtermgrid_take_replies(bell_grid, captured, sizeof(captured));
        CHECK("reply captured", YETTY_IS_OK(take_res) && take_res.value >= 6);
    }

    struct yetty_yclass_object_ptr_result grid_res = yetty_yscene_scene_terminal_grid(obj);
    CHECK("grid embedded", YETTY_IS_OK(grid_res));
    if (YETTY_IS_ERR(grid_res)) {
        yetty_ycore_error_destroy(grid_res.error);
        return;
    }
    struct yetty_yclass_object *grid = grid_res.value;

    struct yetty_yscene_vtermgrid_cell h = {0};
    struct yetty_yscene_vtermgrid_cell w = {0};
    struct yetty_ycore_void_result hc = yetty_yscene_vtermgrid_cell(grid, 0, 0, &h);
    struct yetty_ycore_void_result wc = yetty_yscene_vtermgrid_cell(grid, 1, 0, &w);
    if (YETTY_IS_ERR(hc)) {
        yetty_ycore_error_destroy(hc.error);
    }
    if (YETTY_IS_ERR(wc)) {
        yetty_ycore_error_destroy(wc.error);
    }
    CHECK("grid: H at 0,0", h.glyph == 'H');
    CHECK("grid: W at 1,0 (after CRLF)", w.glyph == 'W');
    uint32_t wr = w.fg & 0xFFu, wg = (w.fg >> 8) & 0xFFu, wb = (w.fg >> 16) & 0xFFu;
    CHECK("grid: W is red", wr > wg && wr > wb);

    /* Resize through the scene surface reaches the embedded grid. */
    CHECK_OK("grid resize", yetty_yscene_scene_terminal_grid_resize(obj, 12, 50));
    uint32_t rows = 0, cols = 0;
    struct yetty_ycore_void_result dims = yetty_yscene_vtermgrid_dims(grid, &rows, &cols);
    if (YETTY_IS_ERR(dims)) {
        yetty_ycore_error_destroy(dims.error);
    }
    CHECK("grid: resized rows", rows == 12);
    CHECK("grid: resized cols", cols == 50);
    destroy_scene(obj);
}

/*===========================================================================
 * #699 render parity (CPU level): the SERVER projector's actual VT redraw,
 * consumed by an independent CLIENT vtermgrid, reproduces the server screen
 * cell-for-cell — for both the initial complete redraw AND an incremental
 * delta. This is what "render parity" reduces to below the GPU rasterizer:
 * the projected tmux-style bytes drive the receiving libvterm to the right
 * cells. (Pixel-level GPU parity is verified live on a device; see #20.)
 *=========================================================================*/
/* Review #17 live-freeze repro: a SCROLL BURST (30 lines through a 24-row
 * pane) projected as an incremental delta must land on the receiver grid
 * cell-for-cell — the live pane froze on exactly this shape. */
static void test_vtermgrid_scroll_burst_parity(void)
{
    struct yetty_yclass_object *pane = yetty_ymux_pane_make(24, 80, 200, 0, NULL).value;
    struct yetty_yclass_object *attachment = yetty_ymux_attachment_make(pane, 24, 80).value;
    struct yetty_yclass_object *projector = yetty_ymux_projector_make(pane, attachment).value;
    CHECK("burst rig", pane && attachment && projector);
    CHECK_OK("burst caps", yetty_ymux_projector_set_capabilities(
                               projector, YMUX_TERM_CAPS_XTERM_256COLOR | YMUX_TERM_CAP_VT_TEXT));
    CHECK_OK("burst base feed", yetty_ymux_pane_feed(pane, "MARKER\r\n$ seq 1 30\r\n", 20));

    struct yetty_yclass_object *grid = yetty_yscene_vtermgrid_make(24, 80).value;
    CHECK("burst grid", grid != NULL);
    struct yetty_ycore_buffer full = yetty_ycore_buffer_create(65536).value;
    CHECK_OK("burst full", yetty_ymux_projector_project_vt(projector, &full));
    CHECK_OK("burst grid full", yetty_yscene_vtermgrid_write(grid, full.data, full.size));

    /* The burst: 30 numbered lines — scrolls the 24-row screen by 8+. The
     * LIVE path projects incrementally (one delta per PTY drain), so feed
     * AND PROJECT line by line — the multi-projection shape is where the
     * live divergence hides. */
    struct yetty_ycore_buffer delta = yetty_ycore_buffer_create(65536).value;
    for (int line = 1; line <= 30; ++line) {
        /* Each iteration mirrors an interactive-shell step: output line,
         * then a zsh-style prompt repaint (CR, SGR resets, CLEAR BELOW,
         * prompt text) — the \e[J-at-prompt interleaving with scrolls is
         * the live shape. */
        char one_line[96];
        int one_len = snprintf(one_line, sizeof(one_line),
                               "line-%d\r\n\r\x1b[0m\x1b[27m\x1b[24m\x1b[J$ ", line);
        CHECK_OK("burst feed", yetty_ymux_pane_feed(pane, one_line, (size_t)one_len));
        delta.size = 0;
        CHECK_OK("burst project", yetty_ymux_projector_project_vt(projector, &delta));
        CHECK_OK("burst grid delta", yetty_yscene_vtermgrid_write(grid, delta.data, delta.size));
    }

    /* The receiver mirrors the engine screen cell-for-cell. */
    uint32_t rows = 24, cols = 80;
    struct yetty_yclass_object *engine = yetty_ymux_pane_engine(pane).value;
    CHECK("burst engine", engine != NULL);
    int mismatches = 0;
    for (uint32_t row = 0; row < rows && mismatches < 5; ++row) {
        struct yetty_ymux_cell_const_ptr_result cells_res =
            yetty_ymux_engine_row_cells(engine, row);
        CHECK("burst engine row", YETTY_IS_OK(cells_res) && cells_res.value != NULL);
        if (YETTY_IS_ERR(cells_res) || !cells_res.value) {
            break;
        }
        for (uint32_t col = 0; col < cols && mismatches < 5; ++col) {
            struct yetty_yscene_vtermgrid_cell receiver_cell = grid_cell(grid, row, col);
            uint32_t expect =
                cells_res.value[col].codepoint ? cells_res.value[col].codepoint : 0x20u;
            uint32_t got = receiver_cell.glyph ? receiver_cell.glyph : 0x20u;
            if (expect != got) {
                fprintf(stderr, "burst mismatch at %u,%u: engine=%02x grid=%02x\n", row, col,
                        expect, got);
                ++mismatches;
            }
        }
    }
    CHECK("burst cell-for-cell", mismatches == 0);

    yetty_ycore_buffer_destroy(&full);
    yetty_ycore_buffer_destroy(&delta);
    (void)yetty_yscene_vtermgrid_dispose(grid);
    (void)yetty_ymux_projector_dispose(projector);
    (void)yetty_ymux_attachment_dispose(attachment);
    (void)yetty_ymux_pane_dispose(pane);
}

static void test_vtermgrid_projector_parity(void)
{
    struct yetty_yclass_object *pane = yetty_ymux_pane_make(3, 12, 8, 0, NULL).value;
    struct yetty_yclass_object *attachment = yetty_ymux_attachment_make(pane, 3, 12).value;
    struct yetty_yclass_object *projector = yetty_ymux_projector_make(pane, attachment).value;
    CHECK("parity rig", pane && attachment && projector);
    CHECK_OK("parity feed", yetty_ymux_pane_feed(pane, "Hello\r\n\x1b[31mWorld", 17));

    struct yetty_yclass_object *grid = yetty_yscene_vtermgrid_make(3, 12).value;
    CHECK("parity grid", grid != NULL);

    /* Complete redraw: the projector's ordinary bytes drive the receiver. */
    struct yetty_ycore_buffer full = yetty_ycore_buffer_create(8192).value;
    CHECK_OK("parity project full", yetty_ymux_projector_project_vt(projector, &full));
    CHECK("parity vt produced", full.size > 0);
    CHECK_OK("parity grid full", yetty_yscene_vtermgrid_write(grid, full.data, full.size));

    const char *row0 = "Hello";
    const char *row1 = "World";
    for (uint32_t col = 0; col < 5; ++col) {
        CHECK("parity row0 glyph",
              grid_cell(grid, 0, col).glyph == (uint32_t)(unsigned char)row0[col]);
        CHECK("parity row1 glyph",
              grid_cell(grid, 1, col).glyph == (uint32_t)(unsigned char)row1[col]);
    }
    /* 'World' was projected red (SGR 31); 'Hello' is default — the receiver
     * decoded the colour, so the two rows' fg differ. */
    CHECK("parity colour applied", grid_cell(grid, 1, 0).fg != grid_cell(grid, 0, 0).fg);
    /* The cursor followed the content to (row 1, col 5). */
    uint32_t cursor_row = 99, cursor_col = 99;
    int cursor_visible = 0;
    struct yetty_ycore_void_result cursor_res =
        yetty_yscene_vtermgrid_cursor(grid, &cursor_row, &cursor_col, &cursor_visible);
    if (YETTY_IS_ERR(cursor_res)) {
        yetty_ycore_error_destroy(cursor_res.error);
    }
    CHECK("parity cursor row", cursor_row == 1);
    CHECK("parity cursor col", cursor_col == 5);

    /* Incremental parity: one more character projects a small DELTA (not a
     * re-dump) that lands at the right cell on the receiver. */
    CHECK_OK("parity feed 2", yetty_ymux_pane_feed(pane, "!", 1));
    struct yetty_ycore_buffer delta = yetty_ycore_buffer_create(8192).value;
    CHECK_OK("parity project delta", yetty_ymux_projector_project_vt(projector, &delta));
    CHECK("parity delta small", delta.size < 64); /* incremental, not a full redraw */
    CHECK_OK("parity grid delta", yetty_yscene_vtermgrid_write(grid, delta.data, delta.size));
    CHECK("parity delta landed", grid_cell(grid, 1, 5).glyph == (uint32_t)'!');

    yetty_ycore_buffer_destroy(&full);
    yetty_ycore_buffer_destroy(&delta);
    struct yetty_ycore_void_result grid_dispose = yetty_yscene_vtermgrid_dispose(grid);
    if (YETTY_IS_ERR(grid_dispose)) {
        yetty_ycore_error_destroy(grid_dispose.error);
    }
    struct yetty_ycore_void_result projector_dispose = yetty_ymux_projector_dispose(projector);
    if (YETTY_IS_ERR(projector_dispose)) {
        yetty_ycore_error_destroy(projector_dispose.error);
    }
    struct yetty_ycore_void_result attachment_dispose = yetty_ymux_attachment_dispose(attachment);
    if (YETTY_IS_ERR(attachment_dispose)) {
        yetty_ycore_error_destroy(attachment_dispose.error);
    }
    struct yetty_ycore_void_result pane_dispose = yetty_ymux_pane_dispose(pane);
    if (YETTY_IS_ERR(pane_dispose)) {
        yetty_ycore_error_destroy(pane_dispose.error);
    }
}

/* Review #19 endpoint semantics: SGR-2 faint carries into the grid cell
 * attrs; extended underline styles stay DISTINCT (single / double / curly);
 * SGR 22 clears bold AND faint. */

/* Review #21/#22: the endpoint capability advertisement names exactly
 * 256,RGB,strikethrough (sync dropped — the yscene path does not defer frames
 * across RPC publications). This pins the STATE side of that advertised set on
 * the receiving grid: SGR 9/29 strikethrough carriage and truecolor (SGR
 * 38;2 / 48;2) plus 256-palette (38;5) fidelity per cell. */
static void test_grid_advertised_caps_state(void)
{
    struct yetty_yclass_object *obj = make_scene();
    CHECK_OK("grid create", yetty_yscene_scene_terminal_grid_create(obj, 4, 40, 9.0f, 18.0f));
    struct yetty_yclass_object *grid = yetty_yscene_scene_terminal_grid(obj).value;
    CHECK("grid", grid != NULL);

    enum {
        GRID_ATTR_STRIKE = 1u << 6,
    };
    /* S: strikethrough on. P: 29 clears it. R: truecolor red fg on
     * truecolor blue bg. G: palette-256 index 46 (green) fg. */
    const char *bytes = "\x1b[9mS\x1b[29mP"
                        "\x1b[38;2;255;0;0;48;2;0;0;255mR"
                        "\x1b[0m\x1b[38;5;46mG\x1b[0m";
    CHECK_OK("caps text",
             yetty_yscene_scene_terminal_grid_write(obj, (const uint8_t *)bytes, strlen(bytes)));
    CHECK("caps row landed", grid_cell(grid, 0, 3).glyph == 'G');
    CHECK("strike set", (grid_cell(grid, 0, 0).attrs & GRID_ATTR_STRIKE) != 0);
    CHECK("29 clears strike", (grid_cell(grid, 0, 1).attrs & GRID_ATTR_STRIKE) == 0);
    /* Cell colours are packed 0xFFBBGGRR. */
    struct yetty_yscene_vtermgrid_cell rgb_cell = grid_cell(grid, 0, 2);
    CHECK("truecolor fg red", (rgb_cell.fg & 0x00FFFFFFu) == 0x000000FFu);
    CHECK("truecolor bg blue", (rgb_cell.bg & 0x00FFFFFFu) == 0x00FF0000u);
    struct yetty_yscene_vtermgrid_cell pal_cell = grid_cell(grid, 0, 3);
    CHECK("palette-256 fg green", (pal_cell.fg & 0x0000FF00u) != 0);

    destroy_scene(obj);
}

static void test_grid_faint_and_underline_styles(void)
{
    struct yetty_yclass_object *obj = make_scene();
    CHECK_OK("grid create", yetty_yscene_scene_terminal_grid_create(obj, 4, 40, 9.0f, 18.0f));
    struct yetty_yclass_object *grid = yetty_yscene_scene_terminal_grid(obj).value;
    CHECK("grid", grid != NULL);

    enum {
        GRID_ATTR_BOLD = 1u << 0,
        GRID_ATTR_UNDERLINE = 1u << 1,
        GRID_ATTR_UNDERLINE2 = 1u << 2,
        GRID_ATTR_FAINT = 1u << 8,
        GRID_ATTR_UNDERLINE_CURLY = 1u << 9,
    };
    CHECK_OK("styled text", yetty_yscene_scene_terminal_grid_write(
                                obj,
                                (const uint8_t *)"\x1b[2mF\x1b[22m\x1b[1;2mB\x1b[22mN"
                                                 "\x1b[4mS\x1b[4:2mD\x1b[4:3mC\x1b[24mP",
                                48));
    CHECK("styled row landed", grid_cell(grid, 0, 6).glyph == 'P');
    /* F: faint only. */
    CHECK("faint set", (grid_cell(grid, 0, 0).attrs & GRID_ATTR_FAINT) != 0);
    /* B: bold+faint — then 22 cleared BOTH for N. */
    CHECK("bold+faint set", (grid_cell(grid, 0, 1).attrs & (GRID_ATTR_BOLD | GRID_ATTR_FAINT)) ==
                                (GRID_ATTR_BOLD | GRID_ATTR_FAINT));
    CHECK("22 clears bold+faint",
          (grid_cell(grid, 0, 2).attrs & (GRID_ATTR_BOLD | GRID_ATTR_FAINT)) == 0);
    /* S: single underline. */
    uint16_t single_attrs = grid_cell(grid, 0, 3).attrs;
    CHECK("single underline", (single_attrs & GRID_ATTR_UNDERLINE) != 0);
    CHECK("single is not double", (single_attrs & GRID_ATTR_UNDERLINE2) == 0);
    /* D: double underline — distinct from single AND from curly. */
    uint16_t double_attrs = grid_cell(grid, 0, 4).attrs;
    CHECK("double underline", (double_attrs & GRID_ATTR_UNDERLINE2) != 0);
    CHECK("double is not curly", (double_attrs & GRID_ATTR_UNDERLINE_CURLY) == 0);
    /* C: curly — carries the curly bit. */
    uint16_t curly_attrs = grid_cell(grid, 0, 5).attrs;
    CHECK("curly underline", (curly_attrs & GRID_ATTR_UNDERLINE_CURLY) != 0);
    /* P: 24 clears every underline form. */
    CHECK("24 clears underline",
          (grid_cell(grid, 0, 6).attrs &
           (GRID_ATTR_UNDERLINE | GRID_ATTR_UNDERLINE2 | GRID_ATTR_UNDERLINE_CURLY)) == 0);

    destroy_scene(obj);
}

int main(void)
{
    test_grid_faint_and_underline_styles();
    test_grid_advertised_caps_state();
    test_typed_api_paint_order();
    test_world_inheritance_and_hit();
    test_wire_adapter();
    test_commit_isolation();
    test_envelope_atomicity();
    test_view_scale_parity_and_plan();
    test_structural_rejections();
    test_terminal_write_content_atomic();
    test_content_transaction_dom_atomicity();
    test_terminal_write_content_rejects_bad_rich();
    test_terminal_grid_embed();
    test_hit_opaque_overlay_routing();
    test_failed_first_dom_then_flat_routing();
    test_dom_apply_mid_frame_rollback();
    test_terminal_grid_altscreen_and_decsca();
    test_dom_reposition_moves_active_generation();
    test_production_chrome_frame();
    test_dom_rollback_fault_matrix();
    test_complex_factory_fault_rig();
    test_terminal_grid_recreate_resets_parser_and_modes();
    test_vtermgrid_projector_parity();
    test_vtermgrid_scroll_burst_parity();
    fprintf(stderr, "%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
