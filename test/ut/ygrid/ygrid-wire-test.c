/*
 * ygrid-wire-test.c — feed ydraw command streams (SDF prims, CMD_GROUP,
 * CMD_DELETE, CMD_ZERO) into ygrid->ops->process_bytes and assert entity
 * tree + prim counts via the polymorphic dump op.
 *
 * The ygrid figure is created in "headless" mode: passing a NULL
 * `yetty_context` makes yetty_ygrid_create skip every GPU-touching init
 * step (no shader load, no binder). The entity tree, the flyweight
 * registry, the cell bucketing, and the process_bytes path all still
 * work — that's the surface this test exercises.
 *
 * Returns 0 on success, non-zero on first failed assertion.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yfigure/figure.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

static int g_failures = 0;
static int g_tests = 0;

#define ASSERT_STR_EQ(name, got, want)                                                             \
    do {                                                                                           \
        const char *_g = (got);                                                                    \
        const char *_w = (want);                                                                   \
        if (!_g || !_w || strcmp(_g, _w) != 0) {                                                   \
            fprintf(stderr, "FAIL %s:\n--- expected ---\n%s\n--- got ---\n%s\n--- end ---\n",      \
                    (name), _w ? _w : "(null)", _g ? _g : "(null)");                               \
            g_failures++;                                                                          \
        } else {                                                                                   \
            fprintf(stderr, "ok   %s\n", (name));                                                  \
        }                                                                                          \
    } while (0)

/*===========================================================================
 * Helpers
 *===========================================================================*/
static struct yetty_ygrid_grid *make_headless_grid(float w, float h)
{
    struct yetty_ycore_rectangle rect = {{0, 0}, {w, h}};
    struct yetty_ygrid_grid_ptr_result r =
        yetty_ygrid_create(rect, /*cols=*/1, /*rows=*/1, /*context=*/NULL);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "ygrid_create failed: %s\n", r.error.msg);
        yetty_ycore_error_destroy(r.error);
        exit(2);
    }
    return r.value;
}

static struct yetty_ydraw_draw_list *make_buf(void)
{
    struct yetty_ydraw_draw_list_result r = yetty_ydraw_draw_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "buffer_create failed\n");
        yetty_ycore_error_destroy(r.error);
        exit(2);
    }
    return r.value;
}

static void feed_grid(struct yetty_ygrid_grid *grid, const struct yetty_ydraw_draw_list *buf)
{
    struct yetty_yfigure_figure *fig = yetty_ygrid_as_figure(grid);
    const uint8_t *bytes = (const uint8_t *)yetty_ydraw_draw_list_data(buf);
    size_t len = yetty_ydraw_draw_list_size(buf);
    struct yetty_ycore_void_result r =
        yetty_yfigure_process_bytes(NULL, yetty_yfigure_figure_self_obj(fig), bytes, len);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "ygrid process_bytes failed: %s\n", r.error.msg);
        yetty_ycore_error_destroy(r.error);
        exit(3);
    }
}

static char *dump_grid(struct yetty_ygrid_grid *grid)
{
    return yetty_yfigure_dump(yetty_ygrid_as_figure(grid), 0);
}

static void destroy_grid(struct yetty_ygrid_grid *grid)
{
    struct yetty_yfigure_figure *fig = yetty_ygrid_as_figure(grid);
    yetty_yfigure_destroy(NULL, (struct yetty_yclass_object *)fig - 1);
}

/* Append a small SDF box record to buf. Coordinates are figure-local. */
static void add_box(struct yetty_ydraw_draw_list *buf, float x, float y, float w, float h,
                    uint32_t color)
{
    struct yetty_ysdf_box geom = {
        .center_x = x + w * 0.5f,
        .center_y = y + h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
        .corner_radius = 0.0f,
    };
    yetty_ydraw_draw_list_add_cmd_add_box(buf, /*id=*/0, /*z_order=*/0, color, /*stroke=*/0,
                                          /*stroke_w=*/0.0f, &geom);
}

/*===========================================================================
 * Tests
 *===========================================================================*/

/* Test 1: empty grid — just root entity, no prims. */
static void test_empty_grid(void)
{
    fprintf(stderr, "\n[test_empty_grid]\n");
    g_tests++;
    struct yetty_ygrid_grid *g = make_headless_grid(100, 100);
    char *dump = dump_grid(g);
    const char *expected =
        "kind: ygrid\n"
        "rect: [0.0, 0.0, 100.0, 100.0]\n"
        "dirty: 1\n"
        "grid_cols: 1\n"
        "grid_rows: 1\n"
        "prim_count: 0\n"
        "prim_count_with_tombstones: 0\n"
        "bytes_len: 0\n"
        "entity_high_water: 1\n"
        "entities:\n"
        "  - slot: 0\n"
        "    external_id: 0\n"
        "    parent_slot: ~\n"
        "    prim_count: 0\n"
        "    children: []\n";
    ASSERT_STR_EQ("empty_grid", dump, expected);
    free(dump);
    destroy_grid(g);
}

/* Test 2: one SDF box added at the ROOT scope (no CMD_GROUP). One prim,
 * attached to the implicit root entity. */
static void test_add_one_box_at_root(void)
{
    fprintf(stderr, "\n[test_add_one_box_at_root]\n");
    g_tests++;
    struct yetty_ygrid_grid *g = make_headless_grid(100, 100);

    struct yetty_ydraw_draw_list *buf = make_buf();
    add_box(buf, 10, 10, 20, 20, 0xff0000ffu);
    feed_grid(g, buf);
    yetty_ydraw_draw_list_destroy(buf);

    char *dump = dump_grid(g);
    /* One live prim attached to the root entity (slot 0). */
    const char *expected_head =
        "kind: ygrid\n"
        "rect: [0.0, 0.0, 100.0, 100.0]\n"
        "dirty: 1\n"
        "grid_cols: 1\n"
        "grid_rows: 1\n"
        "prim_count: 1\n"
        "prim_count_with_tombstones: 1\n";
    if (strncmp(dump, expected_head, strlen(expected_head)) != 0) {
        fprintf(stderr, "FAIL add_one_box_at_root: head mismatch\n--- got ---\n%s", dump);
        g_failures++;
    } else {
        /* The root entity must now own that prim. */
        if (strstr(dump,
                   "entities:\n"
                   "  - slot: 0\n"
                   "    external_id: 0\n"
                   "    parent_slot: ~\n"
                   "    prim_count: 1\n"
                   "    children: []\n") == NULL) {
            fprintf(stderr, "FAIL add_one_box_at_root: root entity prim_count wrong\n"
                            "--- got ---\n%s", dump);
            g_failures++;
        } else {
            fprintf(stderr, "ok   add_one_box_at_root\n");
        }
    }
    free(dump);
    destroy_grid(g);
}

/* Test 3: a single CMD_GROUP with one box inside — creates one named
 * entity under root, with one prim. */
static void test_cmd_group_one_entity(void)
{
    fprintf(stderr, "\n[test_cmd_group_one_entity]\n");
    g_tests++;
    struct yetty_ygrid_grid *g = make_headless_grid(100, 100);

    struct yetty_ydraw_draw_list *buf = make_buf();
    struct yetty_ydraw_id_result m = yetty_ydraw_draw_list_begin_group(buf, /*group_id=*/42u);
    if (YETTY_IS_ERR(m)) {
        fprintf(stderr, "begin_group failed\n");
        yetty_ycore_error_destroy(m.error);
        exit(3);
    }
    add_box(buf, 5, 5, 10, 10, 0xffabcdef);
    yetty_ydraw_draw_list_end_group(buf, m.value);

    feed_grid(g, buf);
    yetty_ydraw_draw_list_destroy(buf);

    char *dump = dump_grid(g);
    const char *expected =
        "kind: ygrid\n"
        "rect: [0.0, 0.0, 100.0, 100.0]\n"
        "dirty: 1\n"
        "grid_cols: 1\n"
        "grid_rows: 1\n"
        "prim_count: 1\n"
        "prim_count_with_tombstones: 1\n"
        /* CMD_GROUP header is parsed but NOT appended to bytes[]; only
         * ADD records land there. So bytes_len = 1 box record = 40 B
         * (10 words: type + 4 style + 5 geom; id=0 omits id slot). */
        "bytes_len: 40\n"
        "entity_high_water: 2\n"
        "entities:\n"
        "  - slot: 0\n"
        "    external_id: 0\n"
        "    parent_slot: ~\n"
        "    prim_count: 0\n"
        "    children: [1]\n"
        "  - slot: 1\n"
        "    external_id: 42\n"
        "    parent_slot: 0\n"
        "    prim_count: 1\n"
        "    children: []\n";
    ASSERT_STR_EQ("cmd_group_one_entity", dump, expected);
    free(dump);
    destroy_grid(g);
}

/* Test 4: nested CMD_GROUPs — outer (id=42) containing inner (id=100). */
static void test_nested_cmd_group(void)
{
    fprintf(stderr, "\n[test_nested_cmd_group]\n");
    g_tests++;
    struct yetty_ygrid_grid *g = make_headless_grid(100, 100);

    struct yetty_ydraw_draw_list *buf = make_buf();
    struct yetty_ydraw_id_result outer = yetty_ydraw_draw_list_begin_group(buf, 42u);
    add_box(buf, 0, 0, 10, 10, 0xff000000); /* in outer scope */
    struct yetty_ydraw_id_result inner = yetty_ydraw_draw_list_begin_group(buf, 100u);
    add_box(buf, 5, 5, 5, 5, 0xff111111); /* in inner scope */
    add_box(buf, 6, 6, 1, 1, 0xff222222); /* second prim in inner */
    yetty_ydraw_draw_list_end_group(buf, inner.value);
    yetty_ydraw_draw_list_end_group(buf, outer.value);

    feed_grid(g, buf);
    yetty_ydraw_draw_list_destroy(buf);

    char *dump = dump_grid(g);
    /* Three live prims: 1 in outer, 2 in inner. Two entities: outer
     * under root, inner under outer. */
    const char *expected =
        "kind: ygrid\n"
        "rect: [0.0, 0.0, 100.0, 100.0]\n"
        "dirty: 1\n"
        "grid_cols: 1\n"
        "grid_rows: 1\n"
        "prim_count: 3\n"
        "prim_count_with_tombstones: 3\n"
        /* CMD_GROUP headers don't land in bytes[] — only the 3 box
         * ADD records do: 3 * 40 = 120 B. */
        "bytes_len: 120\n"
        "entity_high_water: 3\n"
        "entities:\n"
        "  - slot: 0\n"
        "    external_id: 0\n"
        "    parent_slot: ~\n"
        "    prim_count: 0\n"
        "    children: [1]\n"
        "  - slot: 1\n"
        "    external_id: 42\n"
        "    parent_slot: 0\n"
        "    prim_count: 1\n"
        "    children: [2]\n"
        "  - slot: 2\n"
        "    external_id: 100\n"
        "    parent_slot: 1\n"
        "    prim_count: 2\n"
        "    children: []\n";
    ASSERT_STR_EQ("nested_cmd_group", dump, expected);
    free(dump);
    destroy_grid(g);
}

/* Test 5: re-emit a CMD_GROUP — the body REPLACES the previous prims
 * for that entity. (entity_drop_prims on re-open + new prims added.) */
static void test_reopen_cmd_group_replaces_prims(void)
{
    fprintf(stderr, "\n[test_reopen_cmd_group_replaces_prims]\n");
    g_tests++;
    struct yetty_ygrid_grid *g = make_headless_grid(100, 100);

    /* First emission: CMD_GROUP(42) with 2 boxes. */
    struct yetty_ydraw_draw_list *buf1 = make_buf();
    struct yetty_ydraw_id_result m1 = yetty_ydraw_draw_list_begin_group(buf1, 42u);
    add_box(buf1, 0, 0, 10, 10, 0xff111111);
    add_box(buf1, 10, 0, 10, 10, 0xff222222);
    yetty_ydraw_draw_list_end_group(buf1, m1.value);
    feed_grid(g, buf1);
    yetty_ydraw_draw_list_destroy(buf1);

    /* Second emission: CMD_GROUP(42) again, with 1 box this time. */
    struct yetty_ydraw_draw_list *buf2 = make_buf();
    struct yetty_ydraw_id_result m2 = yetty_ydraw_draw_list_begin_group(buf2, 42u);
    add_box(buf2, 50, 50, 5, 5, 0xff333333);
    yetty_ydraw_draw_list_end_group(buf2, m2.value);
    feed_grid(g, buf2);
    yetty_ydraw_draw_list_destroy(buf2);

    char *dump = dump_grid(g);
    /* The entity 42 should now own only 1 live prim. The two old prims
     * are tombstoned (still present in bytes/prims array, just dropped
     * from cells and entity prim_indices). */
    if (strstr(dump, "prim_count: 1\nprim_count_with_tombstones: 3\n") == NULL) {
        fprintf(stderr, "FAIL reopen_cmd_group_replaces_prims: prim counts wrong\n"
                        "--- got ---\n%s", dump);
        g_failures++;
    } else if (strstr(dump,
                      "  - slot: 1\n"
                      "    external_id: 42\n"
                      "    parent_slot: 0\n"
                      "    prim_count: 1\n") == NULL) {
        fprintf(stderr, "FAIL reopen_cmd_group_replaces_prims: entity 42 prim_count\n"
                        "--- got ---\n%s", dump);
        g_failures++;
    } else {
        fprintf(stderr, "ok   reopen_cmd_group_replaces_prims\n");
    }
    free(dump);
    destroy_grid(g);
}

/* Test 6: CMD_DELETE drops a previously-created entity (and its prims). */
static void test_cmd_delete_drops_entity(void)
{
    fprintf(stderr, "\n[test_cmd_delete_drops_entity]\n");
    g_tests++;
    struct yetty_ygrid_grid *g = make_headless_grid(100, 100);

    struct yetty_ydraw_draw_list *buf1 = make_buf();
    struct yetty_ydraw_id_result m1 = yetty_ydraw_draw_list_begin_group(buf1, 42u);
    add_box(buf1, 0, 0, 10, 10, 0xff111111);
    yetty_ydraw_draw_list_end_group(buf1, m1.value);
    feed_grid(g, buf1);
    yetty_ydraw_draw_list_destroy(buf1);

    struct yetty_ydraw_draw_list *buf2 = make_buf();
    yetty_ydraw_draw_list_add_cmd_delete(buf2, 42u);
    feed_grid(g, buf2);
    yetty_ydraw_draw_list_destroy(buf2);

    char *dump = dump_grid(g);
    /* Live prim count drops to 0; entity 42 must NOT appear in entities. */
    if (strstr(dump, "prim_count: 0\nprim_count_with_tombstones: 1\n") == NULL) {
        fprintf(stderr, "FAIL cmd_delete_drops_entity: prim counts wrong\n--- got ---\n%s", dump);
        g_failures++;
    } else if (strstr(dump, "external_id: 42") != NULL) {
        fprintf(stderr, "FAIL cmd_delete_drops_entity: entity 42 still present\n"
                        "--- got ---\n%s", dump);
        g_failures++;
    } else {
        fprintf(stderr, "ok   cmd_delete_drops_entity\n");
    }
    free(dump);
    destroy_grid(g);
}

/* Test 7: CMD_ZERO wipes the entire entity tree, leaving just the root. */
static void test_cmd_zero_clears(void)
{
    fprintf(stderr, "\n[test_cmd_zero_clears]\n");
    g_tests++;
    struct yetty_ygrid_grid *g = make_headless_grid(100, 100);

    struct yetty_ydraw_draw_list *buf1 = make_buf();
    struct yetty_ydraw_id_result m1 = yetty_ydraw_draw_list_begin_group(buf1, 42u);
    add_box(buf1, 0, 0, 10, 10, 0xffcafe00);
    yetty_ydraw_draw_list_end_group(buf1, m1.value);
    feed_grid(g, buf1);
    yetty_ydraw_draw_list_destroy(buf1);

    struct yetty_ydraw_draw_list *buf2 = make_buf();
    yetty_ydraw_draw_list_add_cmd_zero(buf2);
    feed_grid(g, buf2);
    yetty_ydraw_draw_list_destroy(buf2);

    char *dump = dump_grid(g);
    const char *expected =
        "kind: ygrid\n"
        "rect: [0.0, 0.0, 100.0, 100.0]\n"
        "dirty: 1\n"
        "grid_cols: 1\n"
        "grid_rows: 1\n"
        "prim_count: 0\n"
        "prim_count_with_tombstones: 0\n"
        "bytes_len: 0\n"
        "entity_high_water: 1\n"
        "entities:\n"
        "  - slot: 0\n"
        "    external_id: 0\n"
        "    parent_slot: ~\n"
        "    prim_count: 0\n"
        "    children: []\n";
    ASSERT_STR_EQ("cmd_zero_clears", dump, expected);
    free(dump);
    destroy_grid(g);
}

int main(void)
{
    test_empty_grid();
    test_add_one_box_at_root();
    test_cmd_group_one_entity();
    test_nested_cmd_group();
    test_reopen_cmd_group_replaces_prims();
    test_cmd_delete_drops_entity();
    test_cmd_zero_clears();

    fprintf(stderr, "\nygrid wire test: %d tests, %d failure%s\n", g_tests, g_failures,
            g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
