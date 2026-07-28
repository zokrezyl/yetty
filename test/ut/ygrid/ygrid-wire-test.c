/*
 * ygrid-wire-test.c — feed ydraw command streams (SDF prims, CMD_GROUP,
 * CMD_DELETE, CMD_ZERO) into ygrid->ops->process_bytes and assert entity
 * tree + prim counts via the polymorphic dump op.
 *
 * The ygrid figure is created in "headless" mode: passing a NULL
 * `yetty_context` makes yetty_ygrid_create skip every GPU-touching init
 * step (no shader load, no binder). The entity tree, the drawable-list entry
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
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ydraw-factory/composite-factory.h>
#include <yetty/api/yfigure/figure.h>
#include <yetty/api/yfigure/container.h>
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

static struct yetty_ydraw_drawable_list *make_buf(void)
{
    struct yetty_ydraw_drawable_list_result r =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "buffer_create failed\n");
        yetty_ycore_error_destroy(r.error);
        exit(2);
    }
    return r.value;
}

static void feed_grid(struct yetty_ygrid_grid *grid, const struct yetty_ydraw_drawable_list *buf)
{
    struct yetty_yfigure_figure *fig = yetty_ygrid_as_figure(grid);
    const uint8_t *bytes = (const uint8_t *)yetty_ydraw_drawable_list_data(buf);
    size_t len = yetty_ydraw_drawable_list_size(buf);
    struct yetty_ycore_void_result r =
        yetty_yfigure_process_bytes(((struct yetty_yclass_object *)(fig)-1), bytes, len);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "ygrid process_bytes failed: %s\n", r.error.msg);
        yetty_ycore_error_destroy(r.error);
        exit(3);
    }
}

static char *dump_grid(struct yetty_ygrid_grid *grid)
{
    struct yetty_ycore_char_ptr_result dump_result =
        yetty_yfigure_dump(yetty_ygrid_as_figure(grid), 0);
    if (YETTY_IS_ERR(dump_result)) {
        fprintf(stderr, "yfigure_dump failed: %s\n", dump_result.error.msg);
        yetty_ycore_error_destroy(dump_result.error);
        exit(3);
    }
    return dump_result.value;
}

static void destroy_grid(struct yetty_ygrid_grid *grid)
{
    struct yetty_yfigure_figure *fig = yetty_ygrid_as_figure(grid);
    yetty_yfigure_destroy((struct yetty_yclass_object *)fig - 1);
}

/* Append a small SDF box record to buf. Coordinates are figure-local. */
static void add_box(struct yetty_ydraw_drawable_list *buf, float x, float y, float w, float h,
                    uint32_t color)
{
    struct yetty_ysdf_box geom = {
        .center_x = x + w * 0.5f,
        .center_y = y + h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
        .corner_radius = 0.0f,
    };
    yetty_ydraw_drawable_list_add_cmd_add_box(buf, /*id=*/0, /*z_order=*/0, color, /*stroke=*/0,
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
    const char *expected = "kind: ygrid\n"
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

    struct yetty_ydraw_drawable_list *buf = make_buf();
    add_box(buf, 10, 10, 20, 20, 0xff0000ffu);
    feed_grid(g, buf);
    yetty_ydraw_drawable_list_destroy(buf);

    char *dump = dump_grid(g);
    /* One live prim attached to the root entity (slot 0). */
    const char *expected_head = "kind: ygrid\n"
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
        if (strstr(dump, "entities:\n"
                         "  - slot: 0\n"
                         "    external_id: 0\n"
                         "    parent_slot: ~\n"
                         "    prim_count: 1\n"
                         "    children: []\n") == NULL) {
            fprintf(stderr,
                    "FAIL add_one_box_at_root: root entity prim_count wrong\n"
                    "--- got ---\n%s",
                    dump);
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

    struct yetty_ydraw_drawable_list *buf = make_buf();
    struct yetty_ydraw_id_result m = yetty_ydraw_drawable_list_begin_group(buf, /*group_id=*/42u);
    if (YETTY_IS_ERR(m)) {
        fprintf(stderr, "begin_group failed\n");
        yetty_ycore_error_destroy(m.error);
        exit(3);
    }
    add_box(buf, 5, 5, 10, 10, 0xffabcdef);
    yetty_ydraw_drawable_list_end_group(buf, m.value);

    feed_grid(g, buf);
    yetty_ydraw_drawable_list_destroy(buf);

    char *dump = dump_grid(g);
    const char *expected = "kind: ygrid\n"
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

    struct yetty_ydraw_drawable_list *buf = make_buf();
    struct yetty_ydraw_id_result outer = yetty_ydraw_drawable_list_begin_group(buf, 42u);
    add_box(buf, 0, 0, 10, 10, 0xff000000); /* in outer scope */
    struct yetty_ydraw_id_result inner = yetty_ydraw_drawable_list_begin_group(buf, 100u);
    add_box(buf, 5, 5, 5, 5, 0xff111111); /* in inner scope */
    add_box(buf, 6, 6, 1, 1, 0xff222222); /* second prim in inner */
    yetty_ydraw_drawable_list_end_group(buf, inner.value);
    yetty_ydraw_drawable_list_end_group(buf, outer.value);

    feed_grid(g, buf);
    yetty_ydraw_drawable_list_destroy(buf);

    char *dump = dump_grid(g);
    /* Three live prims: 1 in outer, 2 in inner. Two entities: outer
     * under root, inner under outer. */
    const char *expected = "kind: ygrid\n"
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
    struct yetty_ydraw_drawable_list *buf1 = make_buf();
    struct yetty_ydraw_id_result m1 = yetty_ydraw_drawable_list_begin_group(buf1, 42u);
    add_box(buf1, 0, 0, 10, 10, 0xff111111);
    add_box(buf1, 10, 0, 10, 10, 0xff222222);
    yetty_ydraw_drawable_list_end_group(buf1, m1.value);
    feed_grid(g, buf1);
    yetty_ydraw_drawable_list_destroy(buf1);

    /* Second emission: CMD_GROUP(42) again, with 1 box this time. */
    struct yetty_ydraw_drawable_list *buf2 = make_buf();
    struct yetty_ydraw_id_result m2 = yetty_ydraw_drawable_list_begin_group(buf2, 42u);
    add_box(buf2, 50, 50, 5, 5, 0xff333333);
    yetty_ydraw_drawable_list_end_group(buf2, m2.value);
    feed_grid(g, buf2);
    yetty_ydraw_drawable_list_destroy(buf2);

    char *dump = dump_grid(g);
    /* The entity 42 should now own only 1 live prim. The two old prims
     * are tombstoned (still present in bytes/prims array, just dropped
     * from cells and entity prim_indices). */
    if (strstr(dump, "prim_count: 1\nprim_count_with_tombstones: 3\n") == NULL) {
        fprintf(stderr,
                "FAIL reopen_cmd_group_replaces_prims: prim counts wrong\n"
                "--- got ---\n%s",
                dump);
        g_failures++;
    } else if (strstr(dump, "  - slot: 1\n"
                            "    external_id: 42\n"
                            "    parent_slot: 0\n"
                            "    prim_count: 1\n") == NULL) {
        fprintf(stderr,
                "FAIL reopen_cmd_group_replaces_prims: entity 42 prim_count\n"
                "--- got ---\n%s",
                dump);
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

    struct yetty_ydraw_drawable_list *buf1 = make_buf();
    struct yetty_ydraw_id_result m1 = yetty_ydraw_drawable_list_begin_group(buf1, 42u);
    add_box(buf1, 0, 0, 10, 10, 0xff111111);
    yetty_ydraw_drawable_list_end_group(buf1, m1.value);
    feed_grid(g, buf1);
    yetty_ydraw_drawable_list_destroy(buf1);

    struct yetty_ydraw_drawable_list *buf2 = make_buf();
    yetty_ydraw_drawable_list_add_cmd_delete(buf2, 42u);
    feed_grid(g, buf2);
    yetty_ydraw_drawable_list_destroy(buf2);

    char *dump = dump_grid(g);
    /* Live prim count drops to 0; entity 42 must NOT appear in entities. */
    if (strstr(dump, "prim_count: 0\nprim_count_with_tombstones: 1\n") == NULL) {
        fprintf(stderr, "FAIL cmd_delete_drops_entity: prim counts wrong\n--- got ---\n%s", dump);
        g_failures++;
    } else if (strstr(dump, "external_id: 42") != NULL) {
        fprintf(stderr,
                "FAIL cmd_delete_drops_entity: entity 42 still present\n"
                "--- got ---\n%s",
                dump);
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

    struct yetty_ydraw_drawable_list *buf1 = make_buf();
    struct yetty_ydraw_id_result m1 = yetty_ydraw_drawable_list_begin_group(buf1, 42u);
    add_box(buf1, 0, 0, 10, 10, 0xffcafe00);
    yetty_ydraw_drawable_list_end_group(buf1, m1.value);
    feed_grid(g, buf1);
    yetty_ydraw_drawable_list_destroy(buf1);

    struct yetty_ydraw_drawable_list *buf2 = make_buf();
    yetty_ydraw_drawable_list_add_cmd_zero(buf2);
    feed_grid(g, buf2);
    yetty_ydraw_drawable_list_destroy(buf2);

    char *dump = dump_grid(g);
    const char *expected = "kind: ygrid\n"
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

/* Test 8: the ybrowser #507 pattern — a full body mixing bare (group-less)
 * prims with per-image CMD_GROUPs, then a DELTA body that re-opens just one
 * image group WITHOUT a leading CMD_ZERO. The re-opened image's prim is
 * replaced; the other image group AND the bare root prims are untouched. This
 * is exactly what a landed streaming image ships. */
static void test_browser_image_delta_pattern(void)
{
    fprintf(stderr, "\n[test_browser_image_delta_pattern]\n");
    g_tests++;
    struct yetty_ygrid_grid *g = make_headless_grid(200, 200);

    /* Full page: a bare background box at root, then two image groups. */
    struct yetty_ydraw_drawable_list *full = make_buf();
    add_box(full, 0, 0, 200, 200, 0xff202020); /* page background — root scope */
    struct yetty_ydraw_id_result img1 = yetty_ydraw_drawable_list_begin_group(full, 101u);
    add_box(full, 10, 10, 40, 40, 0xffaaaaaa); /* image 1 placeholder */
    yetty_ydraw_drawable_list_end_group(full, img1.value);
    struct yetty_ydraw_id_result img2 = yetty_ydraw_drawable_list_begin_group(full, 102u);
    add_box(full, 60, 10, 40, 40, 0xffbbbbbb); /* image 2 placeholder */
    yetty_ydraw_drawable_list_end_group(full, img2.value);
    feed_grid(g, full);
    yetty_ydraw_drawable_list_destroy(full);

    /* Delta: image 1 landed — re-emit ONLY its group, no CMD_ZERO. */
    struct yetty_ydraw_drawable_list *delta = make_buf();
    struct yetty_ydraw_id_result img1b = yetty_ydraw_drawable_list_begin_group(delta, 101u);
    add_box(delta, 10, 10, 40, 40, 0xff00ff00); /* image 1 loaded pixels */
    yetty_ydraw_drawable_list_end_group(delta, img1b.value);
    feed_grid(g, delta);
    yetty_ydraw_drawable_list_destroy(delta);

    char *dump = dump_grid(g);
    /* Live prims: bg (root) + image2 + reloaded image1 = 3; image1's old
     * placeholder is tombstoned (4 total). Both image entities and the root
     * bg survive; only image1's prim was swapped. */
    int ok = 1;
    if (strstr(dump, "prim_count: 3\nprim_count_with_tombstones: 4\n") == NULL) {
        ok = 0;
    }
    /* Root still owns its 1 bare prim (the background). */
    if (strstr(dump, "  - slot: 0\n"
                     "    external_id: 0\n"
                     "    parent_slot: ~\n"
                     "    prim_count: 1\n") == NULL) {
        ok = 0;
    }
    /* image1 (id 101) replaced → still exactly 1 live prim. */
    if (strstr(dump, "    external_id: 101\n"
                     "    parent_slot: 0\n"
                     "    prim_count: 1\n") == NULL) {
        ok = 0;
    }
    /* image2 (id 102) untouched → still 1 live prim. */
    if (strstr(dump, "    external_id: 102\n"
                     "    parent_slot: 0\n"
                     "    prim_count: 1\n") == NULL) {
        ok = 0;
    }
    if (ok) {
        fprintf(stderr, "ok   browser_image_delta_pattern\n");
    } else {
        fprintf(stderr, "FAIL browser_image_delta_pattern\n--- got ---\n%s", dump);
        g_failures++;
    }
    free(dump);
    destroy_grid(g);
}

/*===========================================================================
 * Stub composite factory — a headless concrete factory whose instances are
 * plain calloc'd structs (no GPU, no pipeline). Counters live on the factory
 * wrapper and are reached through the instance's factory back-pointer, so
 * the test needs no file-scope state. Exercises the #685 composite-entity
 * paths: mint-under-entity, CMD_DELETE destroy, CMD_GROUP re-open replace,
 * CMD_UPDATE routing.
 *===========================================================================*/

#define STUB_COMPOSITE_TYPE_ID 0x80000042u

struct stub_composite_factory {
    struct yetty_ydraw_concrete_factory base; /* must stay first: cast target */
    int instances_destroyed;
    int updates_received;
    uint32_t last_update_field;
    uint32_t last_update_body_len;
};

static void stub_instance_destroy(struct yetty_ydraw_composite *self)
{
    struct stub_composite_factory *wrapper = (struct stub_composite_factory *)self->factory;
    wrapper->instances_destroyed++;
    free(self->buffer_data);
    free(self);
}

static struct yetty_ycore_void_result stub_instance_update(struct yetty_ydraw_composite *self,
                                                           uint32_t target_field, const void *body,
                                                           size_t body_size)
{
    struct stub_composite_factory *wrapper = (struct stub_composite_factory *)self->factory;
    (void)body;
    wrapper->updates_received++;
    wrapper->last_update_field = target_field;
    wrapper->last_update_body_len = (uint32_t)body_size;
    return YETTY_OK_VOID();
}

static const struct yetty_ydraw_composite_ops *stub_instance_ops(void)
{
    static const struct yetty_ydraw_composite_ops ops = {
        .destroy = stub_instance_destroy,
        .update = stub_instance_update,
    };
    return &ops;
}

static struct yetty_ycore_void_result stub_compile_pipeline(
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

static struct yetty_ydraw_composite_ptr_result stub_create_instance(
    struct yetty_ydraw_concrete_factory *self, const void *buffer_data, size_t size,
    uint32_t rolling_row)
{
    struct yetty_ydraw_composite *instance = calloc(1, sizeof(struct yetty_ydraw_composite));
    if (!instance) {
        return YETTY_ERR(yetty_ydraw_composite_ptr, "stub_create_instance: oom");
    }
    instance->ops = stub_instance_ops();
    instance->type = STUB_COMPOSITE_TYPE_ID;
    instance->factory = self;
    instance->rolling_row = rolling_row;
    instance->buffer_data = malloc(size);
    if (!instance->buffer_data) {
        free(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr, "stub_create_instance: buffer oom");
    }
    memcpy(instance->buffer_data, buffer_data, size);
    instance->buffer_size = size;
    /* Payload bounds header: [x][y][w][h] floats at payload offset 0. */
    float bounds[4];
    memcpy(bounds, (const uint8_t *)buffer_data + 8, sizeof(bounds));
    instance->bounds.min.x = bounds[0];
    instance->bounds.min.y = bounds[1];
    instance->bounds.max.x = bounds[0] + bounds[2];
    instance->bounds.max.y = bounds[1] + bounds[3];
    return YETTY_OK(yetty_ydraw_composite_ptr, instance);
}

static void stub_factory_destroy(struct yetty_ydraw_concrete_factory *self)
{
    free(self);
}

/* Abstract factory with the stub registered. The wrapper is owned by the
 * abstract factory (registration transfers ownership); read the counters
 * BEFORE yetty_ydraw_composite_factory_destroy. */
static struct yetty_ydraw_composite_factory *make_stub_composite_factory(
    struct stub_composite_factory **out_wrapper)
{
    struct yetty_ydraw_composite_factory_ptr_result factory_res =
        yetty_ydraw_composite_factory_create(NULL, NULL, (WGPUTextureFormat)0, NULL, NULL);
    if (YETTY_IS_ERR(factory_res)) {
        fprintf(stderr, "composite_factory_create failed\n");
        yetty_ycore_error_destroy(factory_res.error);
        exit(2);
    }
    struct stub_composite_factory *wrapper = calloc(1, sizeof(struct stub_composite_factory));
    if (!wrapper) {
        fprintf(stderr, "stub wrapper oom\n");
        exit(2);
    }
    wrapper->base.type_id = STUB_COMPOSITE_TYPE_ID;
    wrapper->base.destroy = stub_factory_destroy;
    wrapper->base.compile_pipeline = stub_compile_pipeline;
    wrapper->base.create_instance = stub_create_instance;
    struct yetty_ycore_void_result reg_res =
        yetty_ydraw_composite_factory_register(factory_res.value, &wrapper->base);
    if (YETTY_IS_ERR(reg_res)) {
        fprintf(stderr, "composite_factory_register failed: %s\n", reg_res.error.msg);
        yetty_ycore_error_destroy(reg_res.error);
        exit(2);
    }
    *out_wrapper = wrapper;
    return factory_res.value;
}

/* Append a stub composite record: [type][payload_size=16][x][y][w][h]. */
static void add_stub_composite(struct yetty_ydraw_drawable_list *buf, float x, float y, float w,
                               float h)
{
    uint32_t record[6];
    record[0] = STUB_COMPOSITE_TYPE_ID;
    record[1] = 16u;
    memcpy(&record[2], &x, sizeof(float));
    memcpy(&record[3], &y, sizeof(float));
    memcpy(&record[4], &w, sizeof(float));
    memcpy(&record[5], &h, sizeof(float));
    struct yetty_ydraw_id_result add_res =
        yetty_ydraw_drawable_list_add_prim(buf, record, sizeof(record));
    if (YETTY_IS_ERR(add_res)) {
        fprintf(stderr, "add_stub_composite failed\n");
        yetty_ycore_error_destroy(add_res.error);
        exit(2);
    }
}

/* Test 9: a composite minted inside CMD_GROUP is owned by that entity;
 * CMD_DELETE of the entity destroys the instance (no ghost, no leak).
 * Also pins the reclaimed byte buffer (bytes_len: 0 — the instance keeps
 * the only copy of the wire record) and the composites dump section. */
static void test_composite_delete_destroys_instance(void)
{
    fprintf(stderr, "\n[test_composite_delete_destroys_instance]\n");
    g_tests++;
    struct stub_composite_factory *stub = NULL;
    struct yetty_ydraw_composite_factory *factory = make_stub_composite_factory(&stub);
    struct yetty_ygrid_grid *g = make_headless_grid(100, 100);
    yetty_ygrid_set_composite_factory(g, factory);

    struct yetty_ydraw_drawable_list *buf1 = make_buf();
    struct yetty_ydraw_id_result m1 = yetty_ydraw_drawable_list_begin_group(buf1, 7u);
    add_stub_composite(buf1, 20, 30, 32, 16);
    yetty_ydraw_drawable_list_end_group(buf1, m1.value);
    feed_grid(g, buf1);
    yetty_ydraw_drawable_list_destroy(buf1);

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
        "entity_high_water: 2\n"
        "composite_count: 1\n"
        "composites:\n"
        "  - type: 0x80000042 entity: 7 stream_id: 1 bounds: [20.0, 30.0, 52.0, 46.0]\n"
        "entities:\n"
        "  - slot: 0\n"
        "    external_id: 0\n"
        "    parent_slot: ~\n"
        "    prim_count: 0\n"
        "    children: [1]\n"
        "  - slot: 1\n"
        "    external_id: 7\n"
        "    parent_slot: 0\n"
        "    prim_count: 0\n"
        "    children: []\n";
    ASSERT_STR_EQ("composite_minted_under_entity", dump, expected);
    free(dump);

    struct yetty_ydraw_drawable_list *buf2 = make_buf();
    yetty_ydraw_drawable_list_add_cmd_delete(buf2, 7u);
    feed_grid(g, buf2);
    yetty_ydraw_drawable_list_destroy(buf2);

    dump = dump_grid(g);
    int ok = 1;
    if (strstr(dump, "composite_count") != NULL) {
        fprintf(stderr, "FAIL composite_delete: instance still dumped\n--- got ---\n%s", dump);
        ok = 0;
    }
    if (stub->instances_destroyed != 1) {
        fprintf(stderr, "FAIL composite_delete: destroyed=%d want 1\n", stub->instances_destroyed);
        ok = 0;
    }
    if (ok) {
        fprintf(stderr, "ok   composite_delete_destroys_instance\n");
    } else {
        g_failures++;
    }
    free(dump);
    destroy_grid(g);
    yetty_ydraw_composite_factory_destroy(factory);
}

/* Test 10: re-opening the CMD_GROUP of an entity that owns a composite
 * replaces the instance instead of stacking a duplicate. */
static void test_composite_reopen_no_duplicate(void)
{
    fprintf(stderr, "\n[test_composite_reopen_no_duplicate]\n");
    g_tests++;
    struct stub_composite_factory *stub = NULL;
    struct yetty_ydraw_composite_factory *factory = make_stub_composite_factory(&stub);
    struct yetty_ygrid_grid *g = make_headless_grid(100, 100);
    yetty_ygrid_set_composite_factory(g, factory);

    for (int round = 0; round < 2; round++) {
        struct yetty_ydraw_drawable_list *buf = make_buf();
        struct yetty_ydraw_id_result m = yetty_ydraw_drawable_list_begin_group(buf, 5u);
        add_stub_composite(buf, 0, 0, 10, 10);
        yetty_ydraw_drawable_list_end_group(buf, m.value);
        feed_grid(g, buf);
        yetty_ydraw_drawable_list_destroy(buf);
    }

    char *dump = dump_grid(g);
    int ok = 1;
    if (strstr(dump, "composite_count: 1\n") == NULL) {
        fprintf(stderr, "FAIL composite_reopen: want exactly 1 instance\n--- got ---\n%s", dump);
        ok = 0;
    }
    if (stub->instances_destroyed != 1) {
        fprintf(stderr, "FAIL composite_reopen: destroyed=%d want 1 (old instance)\n",
                stub->instances_destroyed);
        ok = 0;
    }
    if (ok) {
        fprintf(stderr, "ok   composite_reopen_no_duplicate\n");
    } else {
        g_failures++;
    }
    free(dump);
    destroy_grid(g);
    yetty_ydraw_composite_factory_destroy(factory);
}

/* Test 11: CMD_UPDATE in a later body routes to the live composite through
 * the grid's stream registry (ordinal 1 within the minting body), carrying
 * target_field + body, mirroring the terminal scrollback path. */
static void test_composite_cmd_update_routes(void)
{
    fprintf(stderr, "\n[test_composite_cmd_update_routes]\n");
    g_tests++;
    struct stub_composite_factory *stub = NULL;
    struct yetty_ydraw_composite_factory *factory = make_stub_composite_factory(&stub);
    struct yetty_ygrid_grid *g = make_headless_grid(100, 100);
    yetty_ygrid_set_composite_factory(g, factory);

    struct yetty_ydraw_drawable_list *buf1 = make_buf();
    add_stub_composite(buf1, 0, 0, 10, 10); /* root scope, stream ordinal 1 */
    feed_grid(g, buf1);
    yetty_ydraw_drawable_list_destroy(buf1);

    uint32_t payload[3] = {3u, 0xAABBCCDDu, 0x11223344u}; /* field=3 + 8-byte body */
    struct yetty_ydraw_drawable_list *buf2 = make_buf();
    struct yetty_ycore_void_result update_res =
        yetty_ydraw_drawable_list_add_cmd_update(buf2, /*target_id=*/1u, payload, sizeof(payload));
    if (YETTY_IS_ERR(update_res)) {
        fprintf(stderr, "add_cmd_update failed\n");
        yetty_ycore_error_destroy(update_res.error);
        exit(2);
    }
    feed_grid(g, buf2);
    yetty_ydraw_drawable_list_destroy(buf2);

    int ok = 1;
    if (stub->updates_received != 1) {
        fprintf(stderr, "FAIL cmd_update_routes: updates=%d want 1\n", stub->updates_received);
        ok = 0;
    }
    if (stub->last_update_field != 3u) {
        fprintf(stderr, "FAIL cmd_update_routes: field=%u want 3\n", stub->last_update_field);
        ok = 0;
    }
    if (stub->last_update_body_len != 8u) {
        fprintf(stderr, "FAIL cmd_update_routes: body_len=%u want 8\n", stub->last_update_body_len);
        ok = 0;
    }
    if (ok) {
        fprintf(stderr, "ok   composite_cmd_update_routes\n");
    } else {
        g_failures++;
    }
    destroy_grid(g);
    yetty_ydraw_composite_factory_destroy(factory);
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
    test_browser_image_delta_pattern();
    test_composite_delete_destroys_instance();
    test_composite_reopen_no_duplicate();
    test_composite_cmd_update_routes();

    fprintf(stderr, "\nygrid wire test: %d tests, %d failure%s\n", g_tests, g_failures,
            g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
