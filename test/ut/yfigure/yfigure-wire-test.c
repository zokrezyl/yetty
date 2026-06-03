/*
 * yfigure-wire-test.c — drive bytes through yetty_yfigure_container's wire
 * record path (process_records) and assert the resulting figure-tree state
 * via the polymorphic ops->dump.
 *
 * Each test:
 *   1. Create a yfigure_container as the root.
 *   2. Build a wire record stream into a ydraw_draw_list:
 *        - admin CREATE_CHILD records to mint children,
 *        - admin DELETE_CHILD records to remove them,
 *        - routed records to forward bytes to a specific child,
 *        - admin CLEAR_ALL to wipe everything.
 *   3. Feed the stream to container_process_records.
 *   4. Dump the container.
 *   5. Compare against an expected YAML string.
 *
 * The tests register a TEST_LEAF figure kind (kind code 0x70000001) whose
 * process_bytes appends to a small in-memory log and whose dump emits a
 * single-line summary. This keeps the tests purely receiver-side and free
 * of any GPU / shader dependencies, while still exercising the real
 * dispatch path through yfigure_container.
 *
 * Returns 0 on success, non-zero on first failed assertion.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/container.h>
#include <yetty/yfigure/rpc.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yfigure/wire.h>

static int g_failures = 0;
static int g_tests = 0;

#define FAIL(...)                                                                                  \
    do {                                                                                           \
        fprintf(stderr, "FAIL: " __VA_ARGS__);                                                     \
        fprintf(stderr, "\n");                                                                     \
        g_failures++;                                                                              \
    } while (0)

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
 * Test stub figure: TEST_LEAF
 *
 * Minimal yetty_yfigure_figure subclass that records every process_bytes
 * call into an in-memory log and whose dump emits a one-line summary
 * "kind: test_leaf | bytes_seen: N".
 *
 * Kind code 0x70000001 is outside every range the production code uses
 * (YGRID = 2, YMGUI/YRDAWN single-digit, complex-prim base 0x80000003+).
 *===========================================================================*/

#define TEST_LEAF_KIND 0x70000001u

struct test_leaf {
    struct yetty_yfigure_figure *base;
    size_t bytes_seen;       /* total bytes through process_bytes */
    uint32_t call_count;     /* number of process_bytes calls */
};

static struct yetty_yclass_ptr_result test_leaf_class_get(void);
/* test_leaf's own data slice (after the figure base slice). */
static struct test_leaf *test_leaf_from_obj(struct yetty_yclass_object *obj)
{
    return (struct test_leaf *)yetty_yclass_object_data(
               obj, test_leaf_class_get().value)
        .value;
}

/* test_leaf is a yclass figure (manually registered — test TUs aren't run
 * through codegen). Each slot impl takes the yclass object; the typed body
 * sits at obj + 1, base is its first member. */
static struct yetty_ycore_void_result test_leaf_destroy(struct yetty_yclass_ctx *ctx,
                                                        struct yetty_yclass_object *obj)
{
    (void)ctx;
    return yetty_yclass_object_free(obj);
}

static struct yetty_ycore_void_result test_leaf_render(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       struct yetty_ydraw_target *target)
{
    (void)ctx;
    (void)obj;
    (void)target;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result test_leaf_process_bytes(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              const uint8_t *bytes, size_t bytes_len)
{
    (void)ctx;
    (void)bytes;
    struct test_leaf *l = test_leaf_from_obj(obj);
    l->bytes_seen += bytes_len;
    l->call_count++;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result test_leaf_reset_content(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct test_leaf *l = test_leaf_from_obj(obj);
    l->bytes_seen = 0;
    l->call_count = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_char_ptr_result test_leaf_dump(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj, int indent)
{
    (void)ctx;
    const struct test_leaf *l = test_leaf_from_obj(obj);
    const struct yetty_yfigure_figure *self = l->base;
    char pad[64];
    int n = indent < 0 ? 0 : indent;
    if ((size_t)n + 1 > sizeof(pad)) {
        n = (int)sizeof(pad) - 1;
    }
    for (int i = 0; i < n; i++) {
        pad[i] = ' ';
    }
    pad[n] = '\0';
    char *buf = (char *)malloc(512);
    if (!buf) {
        return YETTY_OK(yetty_ycore_char_ptr, NULL);
    }
    snprintf(buf, 512,
             "%skind: test_leaf\n"
             "%srect: [%.1f, %.1f, %.1f, %.1f]\n"
             "%sbytes_seen: %zu\n"
             "%scall_count: %u\n",
             pad, pad, yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self) - 1).value.min.x, yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self) - 1).value.min.y, yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self) - 1).value.max.x, yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self) - 1).value.max.y,
             pad, l->bytes_seen, pad, l->call_count);
    return YETTY_OK(yetty_ycore_char_ptr, buf);
}

static struct yetty_yclass_ptr_result test_leaf_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    static const struct yetty_yclass_descriptor desc = {
        .name = "test_leaf",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct test_leaf),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render,
         (yetty_yclass_impl_t)test_leaf_render},
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy,
         (yetty_yclass_impl_t)test_leaf_destroy},
        {"yetty_yfigure", "process_bytes", (yetty_yclass_method_id_t)yetty_yfigure_process_bytes,
         (yetty_yclass_impl_t)test_leaf_process_bytes},
        {"yetty_yfigure", "reset_content", (yetty_yclass_method_id_t)yetty_yfigure_reset_content,
         (yetty_yclass_impl_t)test_leaf_reset_content},
        {"yetty_yfigure", "dump_state", (yetty_yclass_method_id_t)yetty_yfigure_dump_state,
         (yetty_yclass_impl_t)test_leaf_dump},
    };
    struct yetty_yclass_ptr_result parent_r = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(parent_r)) {
        return parent_r;
    }
    struct yetty_yclass_ptr_result r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), parent_r.value, NULL, 0);
    if (YETTY_IS_OK(r)) {
        cls = r.value;
    }
    return r;
}

static struct yetty_yfigure_figure_data_ptr_result test_leaf_factory(struct yetty_ycore_rectangle rect,
                                                                 const struct yetty_context *ctx,
                                                                 void *user)
{
    (void)ctx;
    (void)user;
    struct yetty_yclass_ptr_result cls_r = test_leaf_class_get();
    if (YETTY_IS_ERR(cls_r)) {
        return YETTY_ERR(yetty_yfigure_figure_data_ptr, "test_leaf_factory: class", cls_r);
    }
    struct yetty_yclass_object_ptr_result obj_r = yetty_yclass_object_alloc(cls_r.value);
    if (YETTY_IS_ERR(obj_r)) {
        return YETTY_ERR(yetty_yfigure_figure_data_ptr, "test_leaf_factory: alloc", obj_r);
    }
    struct test_leaf *l = test_leaf_from_obj(obj_r.value);
    l->base = (struct yetty_yfigure_figure *)(obj_r.value + 1);
    yetty_yfigure_figure_rect_set((struct yetty_yclass_object *)(l->base) - 1, rect);
    yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(l->base) - 1, 1);
    return YETTY_OK(yetty_yfigure_figure_data_ptr, l->base);
}

/*===========================================================================
 * Test helpers
 *===========================================================================*/

/* Build a registry that knows TEST_LEAF_KIND. Caller frees. */
static struct yetty_yfigure_registry *make_registry(void)
{
    struct yetty_yfigure_registry_ptr_result r = yetty_yfigure_registry_create();
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "registry_create failed: %s\n", r.error.msg);
        yetty_ycore_error_destroy(r.error);
        exit(2);
    }
    struct yetty_ycore_void_result rr =
        yetty_yfigure_registry_register(r.value, TEST_LEAF_KIND, test_leaf_factory, NULL);
    if (YETTY_IS_ERR(rr)) {
        fprintf(stderr, "registry_register failed: %s\n", rr.error.msg);
        yetty_ycore_error_destroy(rr.error);
        exit(2);
    }
    return r.value;
}

static struct yetty_yfigure_container *make_root(struct yetty_yfigure_registry *registry)
{
    struct yetty_ycore_rectangle rect = {{0, 0}, {1000, 1000}};
    struct yetty_yclass_ctx yclass_ctx = {0};
    struct yetty_yclass_object_ptr_result obj_res =
        yetty_yfigure_container_create(&yclass_ctx);
    if (YETTY_IS_ERR(obj_res)) {
        fprintf(stderr, "container_create failed: %s\n", obj_res.error.msg);
        yetty_ycore_error_destroy(obj_res.error);
        exit(2);
    }
    struct yetty_yfigure_container *root = yetty_yfigure_container_from(obj_res.value);
    yetty_yfigure_container_set_registry(root, registry);
    yetty_yfigure_container_set_rect(root, rect);
    return root;
}

static struct yetty_ydraw_draw_list *make_buf(void)
{
    struct yetty_ydraw_draw_list_result r = yetty_ydraw_draw_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "buffer_create failed: %s\n", r.error.msg);
        yetty_ycore_error_destroy(r.error);
        exit(2);
    }
    return r.value;
}

static void feed(struct yetty_yfigure_container *root, const struct yetty_ydraw_draw_list *buf)
{
    const uint8_t *bytes = (const uint8_t *)yetty_ydraw_draw_list_data(buf);
    size_t len = yetty_ydraw_draw_list_size(buf);
    struct yetty_ycore_void_result r = yetty_yfigure_container_process_records(root, bytes, len);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "container_process_records failed: %s\n", r.error.msg);
        yetty_ycore_error_destroy(r.error);
        exit(3);
    }
}

static char *dump_root(struct yetty_yfigure_container *root)
{
    return yetty_yfigure_dump(yetty_yfigure_container_as_figure(root), 0);
}

/*===========================================================================
 * Test 1: empty container — just rect and zero children.
 *===========================================================================*/
static void test_empty_container(void)
{
    fprintf(stderr, "\n[test_empty_container]\n");
    g_tests++;
    struct yetty_yfigure_registry *reg = make_registry();
    struct yetty_yfigure_container *root = make_root(reg);

    char *dump = dump_root(root);
    const char *expected =
        "kind: container\n"
        "rect: [0.0, 0.0, 1000.0, 1000.0]\n"
        "dirty: 0\n"
        "viewport_offset: [0.0, 0.0]\n"
        "children: {}\n";
    ASSERT_STR_EQ("empty_container", dump, expected);
    free(dump);

    struct yetty_yfigure_figure *fig = yetty_yfigure_container_as_figure(root);
    yetty_yfigure_destroy(NULL, (struct yetty_yclass_object *)fig - 1);
    yetty_yfigure_registry_destroy(reg);
}

/*===========================================================================
 * Test 2: one CREATE_CHILD mints one TEST_LEAF child.
 *===========================================================================*/
static void test_one_create_child(void)
{
    fprintf(stderr, "\n[test_one_create_child]\n");
    g_tests++;
    struct yetty_yfigure_registry *reg = make_registry();
    struct yetty_yfigure_container *root = make_root(reg);

    struct yetty_ydraw_draw_list *buf = make_buf();
    yetty_ydraw_draw_list_add_admin_create_child(buf, /*child_id=*/1u, TEST_LEAF_KIND,
                                                  10.0f, 20.0f, 110.0f, 80.0f, NULL, 0);
    feed(root, buf);
    yetty_ydraw_draw_list_destroy(buf);

    char *dump = dump_root(root);
    const char *expected =
        "kind: container\n"
        "rect: [0.0, 0.0, 1000.0, 1000.0]\n"
        "dirty: 1\n"
        "viewport_offset: [0.0, 0.0]\n"
        "children:\n"
        "  '1':\n"
        "    kind: test_leaf\n"
        "    rect: [10.0, 20.0, 110.0, 80.0]\n"
        "    bytes_seen: 0\n"
        "    call_count: 0\n";
    ASSERT_STR_EQ("one_create_child", dump, expected);
    free(dump);

    struct yetty_yfigure_figure *fig = yetty_yfigure_container_as_figure(root);
    yetty_yfigure_destroy(NULL, (struct yetty_yclass_object *)fig - 1);
    yetty_yfigure_registry_destroy(reg);
}

/*===========================================================================
 * Test 3: two CREATE_CHILD records, then assert both children are in
 * insertion order (= z-order, back-to-front).
 *===========================================================================*/
static void test_two_create_child(void)
{
    fprintf(stderr, "\n[test_two_create_child]\n");
    g_tests++;
    struct yetty_yfigure_registry *reg = make_registry();
    struct yetty_yfigure_container *root = make_root(reg);

    struct yetty_ydraw_draw_list *buf = make_buf();
    yetty_ydraw_draw_list_add_admin_create_child(buf, 1u, TEST_LEAF_KIND, 0, 0, 100, 100, NULL, 0);
    yetty_ydraw_draw_list_add_admin_create_child(buf, 2u, TEST_LEAF_KIND, 100, 0, 200, 100, NULL, 0);
    feed(root, buf);
    yetty_ydraw_draw_list_destroy(buf);

    char *dump = dump_root(root);
    const char *expected =
        "kind: container\n"
        "rect: [0.0, 0.0, 1000.0, 1000.0]\n"
        "dirty: 1\n"
        "viewport_offset: [0.0, 0.0]\n"
        "children:\n"
        "  '1':\n"
        "    kind: test_leaf\n"
        "    rect: [0.0, 0.0, 100.0, 100.0]\n"
        "    bytes_seen: 0\n"
        "    call_count: 0\n"
        "  '2':\n"
        "    kind: test_leaf\n"
        "    rect: [100.0, 0.0, 200.0, 100.0]\n"
        "    bytes_seen: 0\n"
        "    call_count: 0\n";
    ASSERT_STR_EQ("two_create_child", dump, expected);
    free(dump);

    struct yetty_yfigure_figure *fig = yetty_yfigure_container_as_figure(root);
    yetty_yfigure_destroy(NULL, (struct yetty_yclass_object *)fig - 1);
    yetty_yfigure_registry_destroy(reg);
}

/*===========================================================================
 * Test 4: CREATE_CHILD then DELETE_CHILD — removed child is gone.
 *===========================================================================*/
static void test_create_then_delete(void)
{
    fprintf(stderr, "\n[test_create_then_delete]\n");
    g_tests++;
    struct yetty_yfigure_registry *reg = make_registry();
    struct yetty_yfigure_container *root = make_root(reg);

    struct yetty_ydraw_draw_list *buf = make_buf();
    yetty_ydraw_draw_list_add_admin_create_child(buf, 1u, TEST_LEAF_KIND, 0, 0, 50, 50, NULL, 0);
    yetty_ydraw_draw_list_add_admin_create_child(buf, 2u, TEST_LEAF_KIND, 0, 0, 50, 50, NULL, 0);
    yetty_ydraw_draw_list_add_admin_delete_child(buf, 1u);
    feed(root, buf);
    yetty_ydraw_draw_list_destroy(buf);

    char *dump = dump_root(root);
    const char *expected =
        "kind: container\n"
        "rect: [0.0, 0.0, 1000.0, 1000.0]\n"
        "dirty: 1\n"
        "viewport_offset: [0.0, 0.0]\n"
        "children:\n"
        "  '2':\n"
        "    kind: test_leaf\n"
        "    rect: [0.0, 0.0, 50.0, 50.0]\n"
        "    bytes_seen: 0\n"
        "    call_count: 0\n";
    ASSERT_STR_EQ("create_then_delete", dump, expected);
    free(dump);

    struct yetty_yfigure_figure *fig = yetty_yfigure_container_as_figure(root);
    yetty_yfigure_destroy(NULL, (struct yetty_yclass_object *)fig - 1);
    yetty_yfigure_registry_destroy(reg);
}

/*===========================================================================
 * Test 5: CREATE_CHILD with init payload — child gets process_bytes called
 * once with the init payload bytes.
 *===========================================================================*/
static void test_create_with_init_payload(void)
{
    fprintf(stderr, "\n[test_create_with_init_payload]\n");
    g_tests++;
    struct yetty_yfigure_registry *reg = make_registry();
    struct yetty_yfigure_container *root = make_root(reg);

    /* Arbitrary 32-byte payload — the leaf just counts bytes. */
    uint8_t init[32];
    for (int i = 0; i < 32; i++) {
        init[i] = (uint8_t)i;
    }
    struct yetty_ydraw_draw_list *buf = make_buf();
    yetty_ydraw_draw_list_add_admin_create_child(buf, 7u, TEST_LEAF_KIND, 0, 0, 10, 10, init,
                                                  sizeof(init));
    feed(root, buf);
    yetty_ydraw_draw_list_destroy(buf);

    char *dump = dump_root(root);
    const char *expected =
        "kind: container\n"
        "rect: [0.0, 0.0, 1000.0, 1000.0]\n"
        "dirty: 1\n"
        "viewport_offset: [0.0, 0.0]\n"
        "children:\n"
        "  '7':\n"
        "    kind: test_leaf\n"
        "    rect: [0.0, 0.0, 10.0, 10.0]\n"
        "    bytes_seen: 32\n"
        "    call_count: 1\n";
    ASSERT_STR_EQ("create_with_init_payload", dump, expected);
    free(dump);

    struct yetty_yfigure_figure *fig = yetty_yfigure_container_as_figure(root);
    yetty_yfigure_destroy(NULL, (struct yetty_yclass_object *)fig - 1);
    yetty_yfigure_registry_destroy(reg);
}

/*===========================================================================
 * Test 6: CREATE_CHILD then a routed record targeting that id — the
 * child's process_bytes runs again with the routed payload.
 *===========================================================================*/
static void test_routed_to_child(void)
{
    fprintf(stderr, "\n[test_routed_to_child]\n");
    g_tests++;
    struct yetty_yfigure_registry *reg = make_registry();
    struct yetty_yfigure_container *root = make_root(reg);

    struct yetty_ydraw_draw_list *buf = make_buf();
    yetty_ydraw_draw_list_add_admin_create_child(buf, 3u, TEST_LEAF_KIND, 0, 0, 1, 1, NULL, 0);

    /* Now a routed record (id=3) with a 16-byte body. */
    uint8_t body[16] = {0};
    yetty_ydraw_draw_list_add_record(buf, /*id=*/3u, body, sizeof(body));
    feed(root, buf);
    yetty_ydraw_draw_list_destroy(buf);

    char *dump = dump_root(root);
    const char *expected =
        "kind: container\n"
        "rect: [0.0, 0.0, 1000.0, 1000.0]\n"
        "dirty: 1\n"
        "viewport_offset: [0.0, 0.0]\n"
        "children:\n"
        "  '3':\n"
        "    kind: test_leaf\n"
        "    rect: [0.0, 0.0, 1.0, 1.0]\n"
        "    bytes_seen: 16\n"
        "    call_count: 1\n";
    ASSERT_STR_EQ("routed_to_child", dump, expected);
    free(dump);

    struct yetty_yfigure_figure *fig = yetty_yfigure_container_as_figure(root);
    yetty_yfigure_destroy(NULL, (struct yetty_yclass_object *)fig - 1);
    yetty_yfigure_registry_destroy(reg);
}

/*===========================================================================
 * Test 7: CLEAR_ALL after some children exist — every child is removed.
 *===========================================================================*/
static void test_clear_all(void)
{
    fprintf(stderr, "\n[test_clear_all]\n");
    g_tests++;
    struct yetty_yfigure_registry *reg = make_registry();
    struct yetty_yfigure_container *root = make_root(reg);

    struct yetty_ydraw_draw_list *buf = make_buf();
    yetty_ydraw_draw_list_add_admin_create_child(buf, 1u, TEST_LEAF_KIND, 0, 0, 1, 1, NULL, 0);
    yetty_ydraw_draw_list_add_admin_create_child(buf, 2u, TEST_LEAF_KIND, 0, 0, 1, 1, NULL, 0);
    yetty_ydraw_draw_list_add_admin_clear_all(buf);
    feed(root, buf);
    yetty_ydraw_draw_list_destroy(buf);

    char *dump = dump_root(root);
    const char *expected =
        "kind: container\n"
        "rect: [0.0, 0.0, 1000.0, 1000.0]\n"
        "dirty: 1\n"
        "viewport_offset: [0.0, 0.0]\n"
        "children: {}\n";
    ASSERT_STR_EQ("clear_all", dump, expected);
    free(dump);

    struct yetty_yfigure_figure *fig = yetty_yfigure_container_as_figure(root);
    yetty_yfigure_destroy(NULL, (struct yetty_yclass_object *)fig - 1);
    yetty_yfigure_registry_destroy(reg);
}

/*===========================================================================
 * Test 8: stale DELETE_CHILD on an unknown id — benign no-op, the
 * existing child set is unchanged. Matches the existing wire contract
 * (replays / out-of-order traffic must not crash).
 *===========================================================================*/
static void test_stale_delete_is_noop(void)
{
    fprintf(stderr, "\n[test_stale_delete_is_noop]\n");
    g_tests++;
    struct yetty_yfigure_registry *reg = make_registry();
    struct yetty_yfigure_container *root = make_root(reg);

    struct yetty_ydraw_draw_list *buf = make_buf();
    yetty_ydraw_draw_list_add_admin_create_child(buf, 5u, TEST_LEAF_KIND, 0, 0, 10, 10, NULL, 0);
    yetty_ydraw_draw_list_add_admin_delete_child(buf, 99u); /* never bound */
    feed(root, buf);
    yetty_ydraw_draw_list_destroy(buf);

    char *dump = dump_root(root);
    const char *expected =
        "kind: container\n"
        "rect: [0.0, 0.0, 1000.0, 1000.0]\n"
        "dirty: 1\n"
        "viewport_offset: [0.0, 0.0]\n"
        "children:\n"
        "  '5':\n"
        "    kind: test_leaf\n"
        "    rect: [0.0, 0.0, 10.0, 10.0]\n"
        "    bytes_seen: 0\n"
        "    call_count: 0\n";
    ASSERT_STR_EQ("stale_delete_is_noop", dump, expected);
    free(dump);

    struct yetty_yfigure_figure *fig = yetty_yfigure_container_as_figure(root);
    yetty_yfigure_destroy(NULL, (struct yetty_yclass_object *)fig - 1);
    yetty_yfigure_registry_destroy(reg);
}

int main(void)
{
    test_empty_container();
    test_one_create_child();
    test_two_create_child();
    test_create_then_delete();
    test_create_with_init_payload();
    test_routed_to_child();
    test_clear_all();
    test_stale_delete_is_noop();

    fprintf(stderr, "\nyfigure wire test: %d tests, %d failure%s\n", g_tests, g_failures,
            g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
