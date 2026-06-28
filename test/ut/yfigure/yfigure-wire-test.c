/*
 * yfigure-wire-test.c — drive bytes through yetty_yfigure_container's wire
 * record path (process_records) and assert the resulting figure-tree state
 * via the polymorphic ops->dump.
 *
 * Each test:
 *   1. Create a yfigure_container as the root.
 *   2. Build a wire record stream into a ydraw_drawable_list:
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

#include <pthread.h>
#include <sys/socket.h>

#include <yetty/yclass/rpc.h>
#include <yetty/yclass/transport-fd.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/container.h>
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
 * (YGRID = 2, YMGUI/YRDAWN single-digit, composite base 0x80000003+).
 *===========================================================================*/

#define TEST_LEAF_KIND 0x70000001u

struct test_leaf {
    struct yetty_yfigure_figure *base;
    size_t bytes_seen;   /* total bytes through process_bytes */
    uint32_t call_count; /* number of process_bytes calls */
};

static struct yetty_yclass_ptr_result test_leaf_class_get(void);
/* test_leaf's own data slice (after the figure base slice). */
static struct test_leaf *test_leaf_from_obj(struct yetty_yclass_object *obj)
{
    return (struct test_leaf *)yetty_yclass_object_data(obj, test_leaf_class_get().value).value;
}

/* test_leaf is a yclass figure (manually registered — test TUs aren't run
 * through codegen). Each slot impl takes the yclass object; the typed body
 * sits at obj + 1, base is its first member. */
static struct yetty_ycore_void_result test_leaf_destroy(struct yetty_yclass_object *obj)
{
    return yetty_yclass_object_free(obj);
}

static struct yetty_ycore_void_result test_leaf_render(struct yetty_yclass_object *obj,
                                                       struct yetty_ydraw_target *target)
{
    (void)obj;
    (void)target;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result test_leaf_process_bytes(struct yetty_yclass_object *obj,
                                                              const uint8_t *bytes,
                                                              size_t bytes_len)
{
    (void)bytes;
    struct test_leaf *l = test_leaf_from_obj(obj);
    l->bytes_seen += bytes_len;
    l->call_count++;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result test_leaf_reset_content(struct yetty_yclass_object *obj)
{
    struct test_leaf *l = test_leaf_from_obj(obj);
    l->bytes_seen = 0;
    l->call_count = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_char_ptr_result test_leaf_dump(struct yetty_yclass_object *obj,
                                                         int indent)
{
    const struct test_leaf *l = test_leaf_from_obj(obj);
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
             pad, pad, yetty_yfigure_figure_rect_get(obj).value.min.x,
             yetty_yfigure_figure_rect_get(obj).value.min.y,
             yetty_yfigure_figure_rect_get(obj).value.max.x,
             yetty_yfigure_figure_rect_get(obj).value.max.y, pad, l->bytes_seen, pad,
             l->call_count);
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
        .data_align = _Alignof(struct test_leaf),
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

static struct yetty_yfigure_figure_ptr_result test_leaf_factory(struct yetty_ycore_rectangle rect,
                                                                const struct yetty_context *ctx,
                                                                void *user)
{
    (void)user;
    struct yetty_yclass_ptr_result cls_r = test_leaf_class_get();
    if (YETTY_IS_ERR(cls_r)) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "test_leaf_factory: class", cls_r);
    }
    struct yetty_yclass_object_ptr_result obj_r = yetty_yclass_object_alloc(cls_r.value);
    if (YETTY_IS_ERR(obj_r)) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "test_leaf_factory: alloc", obj_r);
    }
    struct test_leaf *l = test_leaf_from_obj(obj_r.value);
    l->base = (struct yetty_yfigure_figure *)(obj_r.value + 1);
    yetty_yfigure_figure_rect_set(obj_r.value, rect);
    yetty_yfigure_figure_dirty_set(obj_r.value, 1);
    return YETTY_OK(yetty_yfigure_figure_ptr, l->base);
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

static struct yetty_yclass_object *make_root(struct yetty_yfigure_registry *registry)
{
    struct yetty_ycore_rectangle rect = {{0, 0}, {1000, 1000}};
    struct yetty_yclass_ctx yclass_ctx = {0};
    struct yetty_yclass_object_ptr_result obj_res = yetty_yfigure_container_create(&yclass_ctx);
    if (YETTY_IS_ERR(obj_res)) {
        fprintf(stderr, "container_create failed: %s\n", obj_res.error.msg);
        yetty_ycore_error_destroy(obj_res.error);
        exit(2);
    }
    struct yetty_yclass_object *root = obj_res.value;
    yetty_yfigure_container_set_registry(root, registry);
    yetty_yfigure_container_set_rect(root, rect);
    return root;
}

static struct yetty_ydraw_drawable_list *make_buf(void)
{
    struct yetty_ydraw_drawable_list_result r =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "buffer_create failed: %s\n", r.error.msg);
        yetty_ycore_error_destroy(r.error);
        exit(2);
    }
    return r.value;
}

static void feed(struct yetty_yclass_object *root, const struct yetty_ydraw_drawable_list *buf)
{
    const uint8_t *bytes = (const uint8_t *)yetty_ydraw_drawable_list_data(buf);
    size_t len = yetty_ydraw_drawable_list_size(buf);
    struct yetty_ycore_void_result r = yetty_yfigure_container_process_records(root, bytes, len);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "container_process_records failed: %s\n", r.error.msg);
        yetty_ycore_error_destroy(r.error);
        exit(3);
    }
}

static char *dump_root(struct yetty_yclass_object *root)
{
    struct yetty_yfigure_figure_ptr_result as_figure_result =
        yetty_yfigure_container_as_figure(root);
    if (YETTY_IS_ERR(as_figure_result)) {
        fprintf(stderr, "container_as_figure failed: %s\n", as_figure_result.error.msg);
        yetty_ycore_error_destroy(as_figure_result.error);
        exit(3);
    }
    struct yetty_ycore_char_ptr_result dump_result = yetty_yfigure_dump(as_figure_result.value, 0);
    if (YETTY_IS_ERR(dump_result)) {
        fprintf(stderr, "yfigure_dump failed: %s\n", dump_result.error.msg);
        yetty_ycore_error_destroy(dump_result.error);
        exit(3);
    }
    return dump_result.value;
}

/*===========================================================================
 * Test 1: empty container — just rect and zero children.
 *===========================================================================*/
static void test_empty_container(void)
{
    fprintf(stderr, "\n[test_empty_container]\n");
    g_tests++;
    struct yetty_yfigure_registry *reg = make_registry();
    struct yetty_yclass_object *root = make_root(reg);

    char *dump = dump_root(root);
    const char *expected = "kind: container\n"
                           "rect: [0.0, 0.0, 1000.0, 1000.0]\n"
                           "dirty: 0\n"
                           "viewport_offset: [0.0, 0.0]\n"
                           "children: {}\n";
    ASSERT_STR_EQ("empty_container", dump, expected);
    free(dump);

    yetty_yfigure_destroy(root);
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
    struct yetty_yclass_object *root = make_root(reg);

    struct yetty_ydraw_drawable_list *buf = make_buf();
    yetty_ydraw_drawable_list_add_admin_create_child(buf, /*child_id=*/1u, TEST_LEAF_KIND, 10.0f,
                                                     20.0f, 110.0f, 80.0f, NULL, 0);
    feed(root, buf);
    yetty_ydraw_drawable_list_destroy(buf);

    char *dump = dump_root(root);
    const char *expected = "kind: container\n"
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

    yetty_yfigure_destroy(root);
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
    struct yetty_yclass_object *root = make_root(reg);

    struct yetty_ydraw_drawable_list *buf = make_buf();
    yetty_ydraw_drawable_list_add_admin_create_child(buf, 1u, TEST_LEAF_KIND, 0, 0, 100, 100, NULL,
                                                     0);
    yetty_ydraw_drawable_list_add_admin_create_child(buf, 2u, TEST_LEAF_KIND, 100, 0, 200, 100,
                                                     NULL, 0);
    feed(root, buf);
    yetty_ydraw_drawable_list_destroy(buf);

    char *dump = dump_root(root);
    const char *expected = "kind: container\n"
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

    yetty_yfigure_destroy(root);
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
    struct yetty_yclass_object *root = make_root(reg);

    struct yetty_ydraw_drawable_list *buf = make_buf();
    yetty_ydraw_drawable_list_add_admin_create_child(buf, 1u, TEST_LEAF_KIND, 0, 0, 50, 50, NULL,
                                                     0);
    yetty_ydraw_drawable_list_add_admin_create_child(buf, 2u, TEST_LEAF_KIND, 0, 0, 50, 50, NULL,
                                                     0);
    yetty_ydraw_drawable_list_add_admin_delete_child(buf, 1u);
    feed(root, buf);
    yetty_ydraw_drawable_list_destroy(buf);

    char *dump = dump_root(root);
    const char *expected = "kind: container\n"
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

    yetty_yfigure_destroy(root);
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
    struct yetty_yclass_object *root = make_root(reg);

    /* Arbitrary 32-byte payload — the leaf just counts bytes. */
    uint8_t init[32];
    for (int i = 0; i < 32; i++) {
        init[i] = (uint8_t)i;
    }
    struct yetty_ydraw_drawable_list *buf = make_buf();
    yetty_ydraw_drawable_list_add_admin_create_child(buf, 7u, TEST_LEAF_KIND, 0, 0, 10, 10, init,
                                                     sizeof(init));
    feed(root, buf);
    yetty_ydraw_drawable_list_destroy(buf);

    char *dump = dump_root(root);
    const char *expected = "kind: container\n"
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

    yetty_yfigure_destroy(root);
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
    struct yetty_yclass_object *root = make_root(reg);

    struct yetty_ydraw_drawable_list *buf = make_buf();
    yetty_ydraw_drawable_list_add_admin_create_child(buf, 3u, TEST_LEAF_KIND, 0, 0, 1, 1, NULL, 0);

    /* Now a routed record (id=3) with a 16-byte body. */
    uint8_t body[16] = {0};
    yetty_ydraw_drawable_list_add_record(buf, /*id=*/3u, body, sizeof(body));
    feed(root, buf);
    yetty_ydraw_drawable_list_destroy(buf);

    char *dump = dump_root(root);
    const char *expected = "kind: container\n"
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

    yetty_yfigure_destroy(root);
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
    struct yetty_yclass_object *root = make_root(reg);

    struct yetty_ydraw_drawable_list *buf = make_buf();
    yetty_ydraw_drawable_list_add_admin_create_child(buf, 1u, TEST_LEAF_KIND, 0, 0, 1, 1, NULL, 0);
    yetty_ydraw_drawable_list_add_admin_create_child(buf, 2u, TEST_LEAF_KIND, 0, 0, 1, 1, NULL, 0);
    yetty_ydraw_drawable_list_add_admin_clear_all(buf);
    feed(root, buf);
    yetty_ydraw_drawable_list_destroy(buf);

    char *dump = dump_root(root);
    const char *expected = "kind: container\n"
                           "rect: [0.0, 0.0, 1000.0, 1000.0]\n"
                           "dirty: 1\n"
                           "viewport_offset: [0.0, 0.0]\n"
                           "children: {}\n";
    ASSERT_STR_EQ("clear_all", dump, expected);
    free(dump);

    yetty_yfigure_destroy(root);
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
    struct yetty_yclass_object *root = make_root(reg);

    struct yetty_ydraw_drawable_list *buf = make_buf();
    yetty_ydraw_drawable_list_add_admin_create_child(buf, 5u, TEST_LEAF_KIND, 0, 0, 10, 10, NULL,
                                                     0);
    yetty_ydraw_drawable_list_add_admin_delete_child(buf, 99u); /* never bound */
    feed(root, buf);
    yetty_ydraw_drawable_list_destroy(buf);

    char *dump = dump_root(root);
    const char *expected = "kind: container\n"
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

    yetty_yfigure_destroy(root);
    yetty_yfigure_registry_destroy(reg);
}

/*===========================================================================
 * Test: the typed yclass container slots (create_child / set_child_rect /
 * apply_child_body / delete_child) called DIRECTLY — the in-process path ygui
 * now uses (local dispatch, ctx.session == NULL) instead of the opaque
 * process_records envelope. Exercises the same figure-tree mutations end to
 * end through the generated stubs.
 *===========================================================================*/
static void test_typed_slots(void)
{
    fprintf(stderr, "\n[test_typed_slots]\n");
    g_tests++;
    struct yetty_yfigure_registry *reg = make_registry();
    struct yetty_yclass_object *root = make_root(reg);

    /* create_child: mint a TEST_LEAF at id 1. */
    struct yetty_ycore_rectangle rect = {{10, 20}, {110, 80}};
    struct yetty_ycore_buffer empty_init = {0};
    struct yetty_ycore_void_result cr =
        yetty_yfigure_create_child(root, TEST_LEAF_KIND, 1u, rect, empty_init);
    if (YETTY_IS_ERR(cr)) {
        FAIL("create_child: %s", cr.error.msg);
        yetty_ycore_error_destroy(cr.error);
    }
    /* set_child_rect: move it. */
    struct yetty_ycore_rectangle moved = {{5, 5}, {55, 55}};
    struct yetty_ycore_void_result sr = yetty_yfigure_set_child_rect(root, 1u, moved);
    if (YETTY_IS_ERR(sr)) {
        FAIL("set_child_rect: %s", sr.error.msg);
        yetty_ycore_error_destroy(sr.error);
    }
    /* apply_child_body: forward 3 bytes to the child's process_bytes. */
    struct yetty_ycore_buffer body = {.data = (uint8_t *)"abc", .capacity = 0, .size = 3};
    struct yetty_ycore_void_result ar = yetty_yfigure_apply_child_body(root, 1u, body);
    if (YETTY_IS_ERR(ar)) {
        FAIL("apply_child_body: %s", ar.error.msg);
        yetty_ycore_error_destroy(ar.error);
    }

    char *dump = dump_root(root);
    const char *expected = "kind: container\n"
                           "rect: [0.0, 0.0, 1000.0, 1000.0]\n"
                           "dirty: 1\n"
                           "viewport_offset: [0.0, 0.0]\n"
                           "children:\n"
                           "  '1':\n"
                           "    kind: test_leaf\n"
                           "    rect: [5.0, 5.0, 55.0, 55.0]\n"
                           "    bytes_seen: 3\n"
                           "    call_count: 1\n";
    ASSERT_STR_EQ("typed_slots_after_mutations", dump, expected);
    free(dump);

    /* delete_child: drop it. */
    struct yetty_ycore_void_result dr = yetty_yfigure_delete_child(root, 1u);
    if (YETTY_IS_ERR(dr)) {
        FAIL("delete_child: %s", dr.error.msg);
        yetty_ycore_error_destroy(dr.error);
    }
    char *dump2 = dump_root(root);
    const char *expected2 = "kind: container\n"
                            "rect: [0.0, 0.0, 1000.0, 1000.0]\n"
                            "dirty: 1\n"
                            "viewport_offset: [0.0, 0.0]\n"
                            "children: {}\n";
    ASSERT_STR_EQ("typed_slots_after_delete", dump2, expected2);
    free(dump2);

    yetty_yfigure_destroy(root);
    yetty_yfigure_registry_destroy(reg);
}

/*===========================================================================
 * Test: the typed container slots over a REAL yclass RPC session (fd-transport
 * socketpair), proving the same callsite that dispatches locally also marshals
 * remote. Single process, two threads: a server thread runs
 * yetty_yclass_rpc_server_run over one socketpair end against a real container
 * (with a registry); the main thread is the client — it calls the generated
 * stubs through a proxy wrapping the server-side handle. The remote calls must
 * mutate the server container, and the result must be byte-identical to doing
 * the same sequence locally (same expected dump as test_typed_slots).
 *
 * Exercises end to end: RPC_OP_GET_CLASS (translate_class), RPC_OP_RESOLVE_SLOT
 * (the first call on each slot, via ensure_remote_id), and RPC_OP_CALL with
 * scalar + rectangle + buffer args.
 *===========================================================================*/

struct rpc_server_arg {
    struct yetty_yclass_transport *transport;
};

static void *rpc_server_thread(void *arg)
{
    struct rpc_server_arg *server_arg = (struct rpc_server_arg *)arg;
    struct yetty_ycore_void_result run_r = yetty_yclass_rpc_server_run(server_arg->transport);
    /* Clean disconnect (client closed its end) returns OK; absorb any error at
     * this thread boundary since there's nowhere to propagate it. */
    if (YETTY_IS_ERR(run_r)) {
        yetty_ycore_error_destroy(run_r.error);
    }
    return NULL;
}

static void test_remote_typed_slots(void)
{
    fprintf(stderr, "\n[test_remote_typed_slots]\n");
    g_tests++;

    /* Server-side yclass registration: skel lookup (CALL dispatch) + handle
     * table init. The container's slots register lazily via make_root's
     * container_create → class accessor. */
    struct yetty_ycore_void_result init_r = yetty_yclass_rpc_init();
    if (YETTY_IS_ERR(init_r)) {
        FAIL("rpc_init: %s", init_r.error.msg);
        yetty_ycore_error_destroy(init_r.error);
        return;
    }
    struct yetty_ycore_void_result reg_r = yetty_yfigure_register();
    if (YETTY_IS_ERR(reg_r)) {
        FAIL("yfigure_register: %s", reg_r.error.msg);
        yetty_ycore_error_destroy(reg_r.error);
        return;
    }

    /* Server-side real container with a registry that knows TEST_LEAF_KIND,
     * designated as the RPC root (the terminal does the same with its root
     * container) so the client can discover it via RPC_OP_GET_ROOT. */
    struct yetty_yfigure_registry *reg = make_registry();
    struct yetty_yclass_object *server_container = make_root(reg);
    struct yetty_yclass_handle_result root_set_r = yetty_yclass_rpc_set_root(server_container);
    if (YETTY_IS_ERR(root_set_r)) {
        FAIL("set_root: %s", root_set_r.error.msg);
        yetty_ycore_error_destroy(root_set_r.error);
        return;
    }

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        FAIL("socketpair");
        return;
    }

    struct yetty_yclass_transport_ptr_result server_transport_r =
        yetty_yclass_transport_fd_create(fds[1]);
    if (YETTY_IS_ERR(server_transport_r)) {
        FAIL("server transport: %s", server_transport_r.error.msg);
        yetty_ycore_error_destroy(server_transport_r.error);
        return;
    }
    struct rpc_server_arg server_arg = {.transport = server_transport_r.value};
    pthread_t server_th;
    pthread_create(&server_th, NULL, rpc_server_thread, &server_arg);

    struct yetty_yclass_transport_ptr_result client_transport_r =
        yetty_yclass_transport_fd_create(fds[0]);
    if (YETTY_IS_ERR(client_transport_r)) {
        FAIL("client transport: %s", client_transport_r.error.msg);
        yetty_ycore_error_destroy(client_transport_r.error);
        return;
    }
    struct yetty_yclass_rpc_session_ptr_result session_r =
        yetty_yclass_rpc_session_create(client_transport_r.value);
    if (YETTY_IS_ERR(session_r)) {
        FAIL("session_create: %s", session_r.error.msg);
        yetty_ycore_error_destroy(session_r.error);
        return;
    }
    struct yetty_yclass_rpc_session *session = session_r.value;

    /* GET_CLASS round-trip (batched slot translation). Degraded-but-OK on
     * failure — the per-slot RESOLVE_SLOT fallback still resolves on demand. */
    struct yetty_ycore_void_result translate_r =
        yetty_yclass_rpc_session_translate_class(session, "yetty_yfigure_container");
    if (YETTY_IS_ERR(translate_r)) {
        yetty_ycore_error_destroy(translate_r.error);
    }

    /* Discover the server root via RPC_OP_GET_ROOT and wrap it in a client
     * proxy — exactly how an out-of-process producer attaches to the host. */
    struct yetty_yclass_handle_result root_r = yetty_yclass_rpc_session_get_root(session);
    if (YETTY_IS_ERR(root_r)) {
        FAIL("get_root: %s", root_r.error.msg);
        yetty_ycore_error_destroy(root_r.error);
        return;
    }
    if (root_r.value == 0) {
        FAIL("get_root returned handle 0");
        return;
    }
    struct yetty_yclass_object_ptr_result proxy_r =
        yetty_yclass_object_proxy_create(session, root_r.value, NULL);
    if (YETTY_IS_ERR(proxy_r)) {
        FAIL("proxy_create: %s", proxy_r.error.msg);
        yetty_ycore_error_destroy(proxy_r.error);
        return;
    }
    struct yetty_yclass_object *cobj = proxy_r.value;

    /* Remote typed-slot calls — marshal over the socketpair, dispatch on the
     * server-side container. */
    struct yetty_ycore_rectangle rect = {{10, 20}, {110, 80}};
    struct yetty_ycore_buffer empty_init = {0};
    struct yetty_ycore_void_result cr =
        yetty_yfigure_create_child(cobj, TEST_LEAF_KIND, 1u, rect, empty_init);
    if (YETTY_IS_ERR(cr)) {
        FAIL("remote create_child: %s", cr.error.msg);
        yetty_ycore_error_destroy(cr.error);
    }
    struct yetty_ycore_rectangle moved = {{5, 5}, {55, 55}};
    struct yetty_ycore_void_result sr = yetty_yfigure_set_child_rect(cobj, 1u, moved);
    if (YETTY_IS_ERR(sr)) {
        FAIL("remote set_child_rect: %s", sr.error.msg);
        yetty_ycore_error_destroy(sr.error);
    }
    struct yetty_ycore_buffer body = {.data = (uint8_t *)"abc", .capacity = 0, .size = 3};
    struct yetty_ycore_void_result ar = yetty_yfigure_apply_child_body(cobj, 1u, body);
    if (YETTY_IS_ERR(ar)) {
        FAIL("remote apply_child_body: %s", ar.error.msg);
        yetty_ycore_error_destroy(ar.error);
    }

    /* Disconnect the client → the server's recv returns 0 → server loop exits. */
    struct yetty_ycore_void_result session_destroy_r = yetty_yclass_rpc_session_destroy(session);
    if (YETTY_IS_ERR(session_destroy_r)) {
        yetty_ycore_error_destroy(session_destroy_r.error);
    }
    pthread_join(server_th, NULL);
    struct yetty_ycore_void_result server_transport_destroy_r =
        server_arg.transport->ops->destroy(server_arg.transport);
    if (YETTY_IS_ERR(server_transport_destroy_r)) {
        yetty_ycore_error_destroy(server_transport_destroy_r.error);
    }

    /* The remote calls must have mutated the server container, and the result
     * must match doing the same sequence locally (identical to test_typed_slots
     * after mutations). */
    char *dump = dump_root(server_container);
    const char *expected = "kind: container\n"
                           "rect: [0.0, 0.0, 1000.0, 1000.0]\n"
                           "dirty: 1\n"
                           "viewport_offset: [0.0, 0.0]\n"
                           "children:\n"
                           "  '1':\n"
                           "    kind: test_leaf\n"
                           "    rect: [5.0, 5.0, 55.0, 55.0]\n"
                           "    bytes_seen: 3\n"
                           "    call_count: 1\n";
    ASSERT_STR_EQ("remote_typed_slots == local", dump, expected);
    free(dump);

    free(cobj); /* heap proxy from yetty_yclass_object_proxy_create */
    yetty_yfigure_destroy(server_container);
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
    test_typed_slots();
    test_remote_typed_slots();

    fprintf(stderr, "\nyfigure wire test: %d tests, %d failure%s\n", g_tests, g_failures,
            g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
