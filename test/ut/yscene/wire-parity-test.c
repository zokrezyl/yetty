/*
 * wire-parity-test.c — receiver coverage ported from the retired ygrid
 * suite onto the yscene figure: nested CMD_GROUP trees, the multi-group
 * delta pattern (the browser progressive-image shape), the figure
 * dirty / reset_content lifecycle, malformed-body recovery + dump
 * stability, and the complex-instance lifecycle (mint under a node,
 * CMD_DELETE destroys, re-open replaces without duplicating, CMD_UPDATE
 * routes to the live instance).
 *
 * Headless: a NULL yetty_context skips every GPU-touching init step.
 * The dom, the wire adapter, derive, hit-test, staging (CPU) and the
 * complex mint/sweep paths all still run — that is the surface here.
 * Where ygrid asserted entity-dump goldens, this suite asserts through
 * the scene's own contract: leaf counts, dump_state ordering, hit ids,
 * and stub-factory counters. One deliberate semantic difference is
 * pinned: a re-emitted group KEEPS its paint slot (seq-preserving
 * replace) instead of ygrid's rebucket-to-back.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/api/yfigure/figure.h>
#include <yetty/api/yscene/scene.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-list/cmds.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ydraw-factory/complex-factory.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include "ytest.h"

/*===========================================================================
 * Helpers
 *=========================================================================*/

static struct yetty_yclass_object *make_scene(struct ytest *test)
{
    struct yetty_ycore_rectangle rect = {{0, 0}, {200, 200}};
    struct yetty_yscene_scene_ptr_result scene_res = yetty_yscene_create(rect, NULL);
    YTEST_REQUIRE_OK(test, scene_res);
    struct yetty_yclass_object_ptr_result object_res = yetty_yscene_scene_to(scene_res.value);
    YTEST_REQUIRE_OK(test, object_res);
    return object_res.value;
}

static void destroy_scene(struct yetty_yclass_object *obj)
{
    struct yetty_ycore_void_result destroy_res = yetty_yfigure_destroy(obj);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
}

static struct yetty_ydraw_drawable_list *make_list(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, list_res);
    return list_res.value;
}

static struct yetty_ycore_void_result feed_bytes(struct yetty_yclass_object *obj,
                                                 const uint8_t *bytes, size_t len)
{
    return yetty_yfigure_process_bytes(obj, bytes, len);
}

static void feed(struct ytest *test, struct yetty_yclass_object *obj,
                 const struct yetty_ydraw_drawable_list *list)
{
    YTEST_REQUIRE_OK(test, feed_bytes(obj, yetty_ydraw_drawable_list_data(list),
                                      yetty_ydraw_drawable_list_size(list)));
}

static void add_box(struct ytest *test, struct yetty_ydraw_drawable_list *list, float x, float y,
                    float w, float h)
{
    struct yetty_ysdf_box geometry = {
        .center_x = x + w * 0.5f,
        .center_y = y + h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
        .corner_radius = 0.0f,
    };
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_add_box(
                               list, /*id=*/0, /*z_order=*/0, /*fill=*/0xff00ff00u, /*stroke=*/0,
                               /*stroke_w=*/0.0f, &geometry));
}

static uint32_t leaf_count(struct ytest *test, struct yetty_yclass_object *obj)
{
    YTEST_REQUIRE_OK(test, yetty_yscene_derive(obj));
    struct yetty_ycore_uint32_result count_res = yetty_yscene_leaf_count(obj);
    YTEST_REQUIRE_OK(test, count_res);
    return count_res.value;
}

static char *dump_scene(struct ytest *test, struct yetty_yclass_object *obj)
{
    YTEST_REQUIRE_OK(test, yetty_yscene_derive(obj));
    struct yetty_ycore_char_ptr_result dump_res = yetty_yfigure_dump_state(obj, 0);
    YTEST_REQUIRE_OK(test, dump_res);
    YTEST_REQUIRE_NOT_NULL(test, dump_res.value);
    return dump_res.value;
}

static uint64_t hit(struct ytest *test, struct yetty_yclass_object *obj, float x, float y)
{
    struct yetty_ycore_uint64_result hit_res = yetty_yscene_hit_test(obj, x, y);
    YTEST_REQUIRE_OK(test, hit_res);
    return hit_res.value;
}

/* Build staging (pure CPU headless) — this is where instances whose
 * source span left the committed scene are swept. Caller frees. */
static void build_staging(struct ytest *test, struct yetty_yclass_object *obj)
{
    struct yetty_ycore_char_ptr_result plan_res = yetty_yscene_render_plan(obj);
    YTEST_REQUIRE_OK(test, plan_res);
    free(plan_res.value);
}

/*===========================================================================
 * Nested CMD_GROUP: leaves land under the innermost node; hit-test
 * routes to the owning node's external id.
 *=========================================================================*/
static void test_nested_group_and_hit(struct ytest *test)
{
    struct yetty_yclass_object *obj = make_scene(test);

    struct yetty_ydraw_drawable_list *list = make_list(test);
    struct yetty_ydraw_id_result outer = yetty_ydraw_drawable_list_begin_group(list, 42u);
    YTEST_REQUIRE_OK(test, outer);
    add_box(test, list, 10, 10, 20, 20); /* outer scope */
    struct yetty_ydraw_id_result inner = yetty_ydraw_drawable_list_begin_group(list, 100u);
    YTEST_REQUIRE_OK(test, inner);
    add_box(test, list, 40, 40, 10, 10); /* inner scope */
    add_box(test, list, 55, 40, 5, 5);   /* second prim in inner */
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_end_group(list, inner.value));
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_end_group(list, outer.value));
    feed(test, obj, list);
    yetty_ydraw_drawable_list_destroy(list);

    YTEST_CHECK(test, leaf_count(test, obj) == 3);
    char *dump = dump_scene(test, obj);
    YTEST_CHECK(test, strstr(dump, "node id=42") != NULL);
    YTEST_CHECK(test, strstr(dump, "node id=100") != NULL);
    free(dump);

    YTEST_CHECK(test, hit(test, obj, 15, 15) == 42u);
    YTEST_CHECK(test, hit(test, obj, 45, 45) == 100u);
    YTEST_CHECK(test, hit(test, obj, 150, 150) == 0u);

    destroy_scene(obj);
}

/*===========================================================================
 * Multi-group delta (the browser progressive-image shape): a full body
 * ships bg + two image groups; a later delta re-emits ONLY one group.
 * The re-emitted group keeps its paint slot between the bg and the
 * untouched group; nothing else changes.
 *=========================================================================*/
static void test_multi_group_delta(struct ytest *test)
{
    struct yetty_yclass_object *obj = make_scene(test);

    struct yetty_ydraw_drawable_list *full = make_list(test);
    add_box(test, full, 0, 0, 200, 200); /* page background — root scope */
    struct yetty_ydraw_id_result image_one = yetty_ydraw_drawable_list_begin_group(full, 101u);
    YTEST_REQUIRE_OK(test, image_one);
    add_box(test, full, 10, 10, 40, 40); /* image 1 placeholder */
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_end_group(full, image_one.value));
    struct yetty_ydraw_id_result image_two = yetty_ydraw_drawable_list_begin_group(full, 102u);
    YTEST_REQUIRE_OK(test, image_two);
    add_box(test, full, 60, 10, 40, 40); /* image 2 placeholder */
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_end_group(full, image_two.value));
    feed(test, obj, full);
    yetty_ydraw_drawable_list_destroy(full);
    YTEST_CHECK(test, leaf_count(test, obj) == 3);

    /* Delta: image 1 landed — re-emit ONLY its group, no CMD_ZERO. */
    struct yetty_ydraw_drawable_list *delta = make_list(test);
    struct yetty_ydraw_id_result image_one_again =
        yetty_ydraw_drawable_list_begin_group(delta, 101u);
    YTEST_REQUIRE_OK(test, image_one_again);
    add_box(test, delta, 12, 12, 36, 36); /* image 1 loaded pixels */
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_end_group(delta, image_one_again.value));
    feed(test, obj, delta);
    yetty_ydraw_drawable_list_destroy(delta);

    YTEST_CHECK(test, leaf_count(test, obj) == 3);
    char *dump = dump_scene(test, obj);
    const char *background = strstr(dump, "aabb=(0.0,0.0,200.0,200.0)");
    const char *image_one_new = strstr(dump, "aabb=(12.0,12.0,48.0,48.0)");
    const char *image_two_kept = strstr(dump, "aabb=(60.0,10.0,100.0,50.0)");
    YTEST_CHECK_NOT_NULL(test, (void *)background);
    YTEST_CHECK_NOT_NULL(test, (void *)image_one_new);
    YTEST_CHECK_NOT_NULL(test, (void *)image_two_kept);
    /* The old placeholder is gone. */
    YTEST_CHECK(test, strstr(dump, "aabb=(10.0,10.0,50.0,50.0)") == NULL);
    /* Seq-preserving replace: image 1 still paints between bg and image 2. */
    YTEST_CHECK(test, background && image_one_new && image_two_kept && background < image_one_new &&
                          image_one_new < image_two_kept);
    free(dump);

    destroy_scene(obj);
}

/*===========================================================================
 * Figure dirty lifecycle: dirty at birth, a processed body re-dirties a
 * cleaned figure, reset_content empties the model and re-dirties.
 *=========================================================================*/
static void test_dirty_lifecycle(struct ytest *test)
{
    struct yetty_yclass_object *obj = make_scene(test);

    struct yetty_ycore_int_result dirty_res = yetty_yfigure_figure_dirty_get(obj);
    YTEST_REQUIRE_OK(test, dirty_res);
    YTEST_CHECK(test, dirty_res.value); /* dirty at birth */

    YTEST_REQUIRE_OK(test, yetty_yfigure_figure_dirty_set(obj, 0));

    struct yetty_ydraw_drawable_list *list = make_list(test);
    add_box(test, list, 20, 20, 10, 10);
    feed(test, obj, list);
    yetty_ydraw_drawable_list_destroy(list);

    dirty_res = yetty_yfigure_figure_dirty_get(obj);
    YTEST_REQUIRE_OK(test, dirty_res);
    YTEST_CHECK(test, dirty_res.value); /* a processed body re-dirties */
    YTEST_CHECK(test, leaf_count(test, obj) == 1);

    YTEST_REQUIRE_OK(test, yetty_yfigure_figure_dirty_set(obj, 0));
    YTEST_REQUIRE_OK(test, yetty_yfigure_reset_content(obj));
    dirty_res = yetty_yfigure_figure_dirty_get(obj);
    YTEST_REQUIRE_OK(test, dirty_res);
    YTEST_CHECK(test, dirty_res.value); /* reset re-dirties */
    YTEST_CHECK(test, leaf_count(test, obj) == 0);

    destroy_scene(obj);
}

/*===========================================================================
 * Malformed bodies: rejected atomically (validate-before-mutate — no
 * partial state, no poison), and a valid body afterwards still lands.
 *=========================================================================*/
static void test_malformed_then_recover(struct ytest *test)
{
    struct yetty_yclass_object *obj = make_scene(test);

    /* 1) Unaligned garbage (not even a whole 4-byte word). */
    const uint8_t junk[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03};
    struct yetty_ycore_void_result junk_res = feed_bytes(obj, junk, sizeof(junk));
    YTEST_CHECK(test, YETTY_IS_ERR(junk_res));
    if (YETTY_IS_ERR(junk_res)) {
        yetty_ycore_error_destroy(junk_res.error);
    }

    /* 2) A record whose type word claims a box but supplies no fields
     * (truncated mid-record). Exact-size alloc so an over-read trips ASAN. */
    uint32_t *truncated = malloc(sizeof(uint32_t));
    YTEST_REQUIRE_NOT_NULL(test, truncated);
    truncated[0] = YETTY_YSDF_BOX;
    struct yetty_ycore_void_result truncated_res =
        feed_bytes(obj, (const uint8_t *)truncated, sizeof(uint32_t));
    YTEST_CHECK(test, YETTY_IS_ERR(truncated_res));
    if (YETTY_IS_ERR(truncated_res)) {
        yetty_ycore_error_destroy(truncated_res.error);
    }
    free(truncated);

    /* 3) Recovery: rejection was pre-mutation, so a valid body still
     * processes and the scene holds exactly its content. */
    struct yetty_ydraw_drawable_list *list = make_list(test);
    add_box(test, list, 20, 20, 10, 10);
    feed(test, obj, list);
    yetty_ydraw_drawable_list_destroy(list);
    YTEST_CHECK(test, leaf_count(test, obj) == 1);
    char *dump = dump_scene(test, obj);
    YTEST_CHECK_NOT_NULL(test, dump);
    free(dump);

    destroy_scene(obj);
}

/*===========================================================================
 * Dump stability: identical inputs → identical dumps, across instances
 * and across repeated dumps of one instance.
 *=========================================================================*/
static void test_dump_stable(struct ytest *test)
{
    struct yetty_yclass_object *first = make_scene(test);
    struct yetty_yclass_object *second = make_scene(test);

    struct yetty_ydraw_drawable_list *list = make_list(test);
    add_box(test, list, 10, 10, 20, 20);
    add_box(test, list, 30, 40, 20, 20);
    feed(test, first, list);
    feed(test, second, list);
    yetty_ydraw_drawable_list_destroy(list);

    char *dump_first = dump_scene(test, first);
    char *dump_second = dump_scene(test, second);
    YTEST_CHECK_STR_EQ(test, dump_first, dump_second);
    free(dump_first);
    free(dump_second);

    char *repeat_first = dump_scene(test, first);
    char *repeat_second = dump_scene(test, first);
    YTEST_CHECK_STR_EQ(test, repeat_first, repeat_second);
    free(repeat_first);
    free(repeat_second);

    destroy_scene(first);
    destroy_scene(second);
}

/*===========================================================================
 * Stub complex factory — a headless concrete factory whose instances
 * are plain calloc'd structs (no GPU, no pipeline). Counters live on the
 * factory wrapper, reached through the instance's factory back-pointer.
 *=========================================================================*/

#define STUB_COMPLEX_TYPE_ID 0x80000042u

struct stub_complex_factory {
    struct yetty_ydraw_concrete_factory base; /* must stay first: cast target */
    int instances_destroyed;
    int updates_received;
    uint32_t last_update_field;
    uint32_t last_update_body_len;
};

static void stub_instance_destroy(struct yetty_ydraw_complex *self)
{
    struct stub_complex_factory *wrapper = (struct stub_complex_factory *)self->factory;
    wrapper->instances_destroyed++;
    free(self->buffer_data);
    free(self);
}

static struct yetty_ycore_void_result stub_instance_update(struct yetty_ydraw_complex *self,
                                                           uint32_t target_field, const void *body,
                                                           size_t body_size)
{
    struct stub_complex_factory *wrapper = (struct stub_complex_factory *)self->factory;
    (void)body;
    wrapper->updates_received++;
    wrapper->last_update_field = target_field;
    wrapper->last_update_body_len = (uint32_t)body_size;
    return YETTY_OK_VOID();
}

static const struct yetty_ydraw_complex_ops *stub_instance_ops(void)
{
    static const struct yetty_ydraw_complex_ops ops = {
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

static struct yetty_ydraw_complex_ptr_result stub_create_instance(
    struct yetty_ydraw_concrete_factory *self, const void *buffer_data, size_t size,
    uint32_t rolling_row)
{
    struct yetty_ydraw_complex *instance = calloc(1, sizeof(struct yetty_ydraw_complex));
    if (!instance) {
        return YETTY_ERR(yetty_ydraw_complex_ptr, "stub_create_instance: oom");
    }
    instance->ops = stub_instance_ops();
    instance->type = STUB_COMPLEX_TYPE_ID;
    instance->factory = self;
    instance->rolling_row = rolling_row;
    instance->buffer_data = malloc(size);
    if (!instance->buffer_data) {
        free(instance);
        return YETTY_ERR(yetty_ydraw_complex_ptr, "stub_create_instance: buffer oom");
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
    return YETTY_OK(yetty_ydraw_complex_ptr, instance);
}

static void stub_factory_destroy(struct yetty_ydraw_concrete_factory *self)
{
    free(self);
}

/* Abstract factory with the stub registered. The wrapper is owned by the
 * abstract factory (registration transfers ownership); read the counters
 * BEFORE yetty_ydraw_complex_factory_destroy. */
static struct yetty_ydraw_complex_factory *make_stub_complex_factory(
    struct ytest *test, struct stub_complex_factory **out_wrapper)
{
    struct yetty_ydraw_complex_factory_ptr_result factory_res =
        yetty_ydraw_complex_factory_create(NULL, NULL, (WGPUTextureFormat)0, NULL, NULL);
    YTEST_REQUIRE_OK(test, factory_res);
    struct stub_complex_factory *wrapper = calloc(1, sizeof(struct stub_complex_factory));
    YTEST_REQUIRE_NOT_NULL(test, wrapper);
    wrapper->base.type_id = STUB_COMPLEX_TYPE_ID;
    wrapper->base.destroy = stub_factory_destroy;
    wrapper->base.compile_pipeline = stub_compile_pipeline;
    wrapper->base.create_instance = stub_create_instance;
    YTEST_REQUIRE_OK(test,
                     yetty_ydraw_complex_factory_register(factory_res.value, &wrapper->base));
    *out_wrapper = wrapper;
    return factory_res.value;
}

/* Append a stub complex record: [type][payload_size=16][x][y][w][h]. */
static void add_stub_complex(struct ytest *test, struct yetty_ydraw_drawable_list *list, float x,
                               float y, float w, float h)
{
    uint32_t record[6];
    record[0] = STUB_COMPLEX_TYPE_ID;
    record[1] = 16u;
    memcpy(&record[2], &x, sizeof(float));
    memcpy(&record[3], &y, sizeof(float));
    memcpy(&record[4], &w, sizeof(float));
    memcpy(&record[5], &h, sizeof(float));
    struct yetty_ydraw_id_result add_res =
        yetty_ydraw_drawable_list_add_prim(list, record, sizeof(record));
    YTEST_REQUIRE_OK(test, add_res);
}

/*===========================================================================
 * Complex minted under a node dies with the node's CMD_DELETE — no
 * ghost, no leak (the staging sweep reaps instances whose span left the
 * committed scene).
 *=========================================================================*/
static void test_complex_delete_destroys_instance(struct ytest *test)
{
    struct stub_complex_factory *stub = NULL;
    struct yetty_ydraw_complex_factory *factory = make_stub_complex_factory(test, &stub);
    struct yetty_yclass_object *obj = make_scene(test);
    YTEST_REQUIRE_OK(test, yetty_yscene_set_complex_factory(obj, factory));

    struct yetty_ydraw_drawable_list *list = make_list(test);
    struct yetty_ydraw_id_result group_res = yetty_ydraw_drawable_list_begin_group(list, 7u);
    YTEST_REQUIRE_OK(test, group_res);
    add_stub_complex(test, list, 20, 30, 32, 16);
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_end_group(list, group_res.value));
    feed(test, obj, list);
    yetty_ydraw_drawable_list_destroy(list);

    build_staging(test, obj);
    YTEST_CHECK(test, stub->instances_destroyed == 0); /* alive under node 7 */

    struct yetty_ydraw_drawable_list *delete_list = make_list(test);
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_delete(delete_list, 7u));
    feed(test, obj, delete_list);
    yetty_ydraw_drawable_list_destroy(delete_list);

    build_staging(test, obj);
    YTEST_CHECK(test, stub->instances_destroyed == 1);

    destroy_scene(obj);
    yetty_ydraw_complex_factory_destroy(factory);
}

/*===========================================================================
 * Re-opening the CMD_GROUP of a node that owns a complex replaces the
 * instance instead of stacking a duplicate.
 *=========================================================================*/
static void test_complex_reopen_no_duplicate(struct ytest *test)
{
    struct stub_complex_factory *stub = NULL;
    struct yetty_ydraw_complex_factory *factory = make_stub_complex_factory(test, &stub);
    struct yetty_yclass_object *obj = make_scene(test);
    YTEST_REQUIRE_OK(test, yetty_yscene_set_complex_factory(obj, factory));

    for (int round = 0; round < 2; round++) {
        struct yetty_ydraw_drawable_list *list = make_list(test);
        struct yetty_ydraw_id_result group_res = yetty_ydraw_drawable_list_begin_group(list, 5u);
        YTEST_REQUIRE_OK(test, group_res);
        add_stub_complex(test, list, 0, 0, 10, 10);
        YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_end_group(list, group_res.value));
        feed(test, obj, list);
        yetty_ydraw_drawable_list_destroy(list);
    }

    build_staging(test, obj);
    /* Exactly one live instance: the re-emit minted a replacement and
     * the sweep reaped the original. */
    YTEST_CHECK(test, stub->instances_destroyed == 1);
    YTEST_CHECK(test, leaf_count(test, obj) == 1);

    destroy_scene(obj);
    yetty_ydraw_complex_factory_destroy(factory);
}

/*===========================================================================
 * CMD_UPDATE in a later body routes to the live complex through its
 * per-body producer ordinal (1-based), carrying target_field + body.
 *=========================================================================*/
static void test_complex_cmd_update_routes(struct ytest *test)
{
    struct stub_complex_factory *stub = NULL;
    struct yetty_ydraw_complex_factory *factory = make_stub_complex_factory(test, &stub);
    struct yetty_yclass_object *obj = make_scene(test);
    YTEST_REQUIRE_OK(test, yetty_yscene_set_complex_factory(obj, factory));

    struct yetty_ydraw_drawable_list *list = make_list(test);
    add_stub_complex(test, list, 0, 0, 10, 10); /* root scope, ordinal 1 */
    feed(test, obj, list);
    yetty_ydraw_drawable_list_destroy(list);

    uint32_t payload[3] = {3u, 0xAABBCCDDu, 0x11223344u}; /* field=3 + 8-byte body */
    struct yetty_ydraw_drawable_list *update_list = make_list(test);
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_update(update_list, /*target_id=*/1u,
                                                                    payload, sizeof(payload)));
    feed(test, obj, update_list);
    yetty_ydraw_drawable_list_destroy(update_list);

    YTEST_CHECK(test, stub->updates_received == 1);
    YTEST_CHECK(test, stub->last_update_field == 3u);
    YTEST_CHECK(test, stub->last_update_body_len == 8u);

    destroy_scene(obj);
    yetty_ydraw_complex_factory_destroy(factory);
}

int main(void)
{
    struct ytest test = ytest_begin("yscene_wire_parity");
    YTEST_RUN(&test, test_nested_group_and_hit);
    YTEST_RUN(&test, test_multi_group_delta);
    YTEST_RUN(&test, test_dirty_lifecycle);
    YTEST_RUN(&test, test_malformed_then_recover);
    YTEST_RUN(&test, test_dump_stable);
    YTEST_RUN(&test, test_complex_delete_destroys_instance);
    YTEST_RUN(&test, test_complex_reopen_no_duplicate);
    YTEST_RUN(&test, test_complex_cmd_update_routes);
    return ytest_end(&test);
}
