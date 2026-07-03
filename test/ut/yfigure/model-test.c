/*
 * yfigure container model / hit-test / dirty-propagation matrix (#419).
 *
 * Complements container-test.c (basic hit, z-order topmost-wins, per-child
 * dirty lifecycle) and yfigure-wire-test.c (create/delete/clear/rect/body
 * dump goldens) by pinning the parts those never touch:
 *   - hit-test honours `hidden` children and falls through to the next,
 *   - hit-test tracks a child's rect after set_child_rect, and misses cleanly,
 *   - every child mutation (body / rect / hide / delete / clear_all)
 *     propagates dirty UP to the container.
 *
 * Uses the same stub figure (byte-counter, no GPU/font) as container-test.c,
 * registered under one kind token.
 */

#include <yetty/yfigure/container.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/registry.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*===========================================================================
 * Stub figure (byte-counter, no GPU).
 *===========================================================================*/
struct stub_figure {
    struct yetty_yfigure_figure *base;
    size_t bytes_seen;
};

static struct yetty_yclass_ptr_result stub_class_get(void);

static struct stub_figure *stub_from_obj(struct yetty_yclass_object *obj)
{
    return (struct stub_figure *)yetty_yclass_object_data(obj, stub_class_get().value).value;
}

static struct yetty_ycore_void_result stub_destroy(struct yetty_yclass_object *obj)
{
    return yetty_yclass_object_free(obj);
}
static struct yetty_ycore_void_result stub_render(struct yetty_yclass_object *obj,
                                                  struct yetty_ydraw_target *target)
{
    (void)obj;
    (void)target;
    return YETTY_OK_VOID();
}
static struct yetty_ycore_void_result stub_process_bytes(struct yetty_yclass_object *obj,
                                                         const uint8_t *bytes, size_t bytes_len)
{
    (void)bytes;
    stub_from_obj(obj)->bytes_seen += bytes_len;
    return YETTY_OK_VOID();
}
static struct yetty_ycore_void_result stub_reset_content(struct yetty_yclass_object *obj)
{
    stub_from_obj(obj)->bytes_seen = 0;
    return YETTY_OK_VOID();
}

static struct yetty_yclass_ptr_result stub_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    static const struct yetty_yclass_descriptor desc = {
        .name = "yfigure_model_test_stub",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct stub_figure),
        .data_align = _Alignof(struct stub_figure),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render,
         (yetty_yclass_impl_t)stub_render},
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy,
         (yetty_yclass_impl_t)stub_destroy},
        {"yetty_yfigure", "process_bytes", (yetty_yclass_method_id_t)yetty_yfigure_process_bytes,
         (yetty_yclass_impl_t)stub_process_bytes},
        {"yetty_yfigure", "reset_content", (yetty_yclass_method_id_t)yetty_yfigure_reset_content,
         (yetty_yclass_impl_t)stub_reset_content},
    };
    struct yetty_yclass_ptr_result parent = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(parent)) {
        return parent;
    }
    struct yetty_yclass_ptr_result r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), parent.value, NULL, 0);
    if (YETTY_IS_OK(r)) {
        cls = r.value;
    }
    return r;
}

static struct yetty_yfigure_figure_ptr_result stub_factory(struct yetty_ycore_rectangle rect,
                                                           const struct yetty_context *ctx,
                                                           void *user)
{
    (void)ctx;
    (void)user;
    struct yetty_yclass_ptr_result cls = stub_class_get();
    if (YETTY_IS_ERR(cls)) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "stub_factory: class", cls);
    }
    struct yetty_yclass_object_ptr_result obj = yetty_yclass_object_alloc(cls.value);
    if (YETTY_IS_ERR(obj)) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "stub_factory: alloc", obj);
    }
    struct stub_figure *figure = stub_from_obj(obj.value);
    figure->base = (struct yetty_yfigure_figure *)(obj.value + 1);
    yetty_yfigure_figure_rect_set(obj.value, rect);
    yetty_yfigure_figure_dirty_set(obj.value, 1);
    return YETTY_OK(yetty_yfigure_figure_ptr, figure->base);
}

#define STUB_KIND "yfigure_model_test_stub"

static struct yetty_yclass_object *make_container(struct ytest *test,
                                                  struct yetty_yfigure_registry **out_registry)
{
    struct yetty_yfigure_registry_ptr_result reg = yetty_yfigure_registry_create();
    YTEST_REQUIRE_OK(test, reg);
    struct yetty_ycore_void_result rr = yetty_yfigure_registry_register(
        reg.value, yetty_yfigure_kind_token(STUB_KIND), stub_factory, NULL);
    YTEST_REQUIRE_OK(test, rr);

    struct yetty_yclass_ctx ctx = {0};
    struct yetty_yclass_object_ptr_result cont = yetty_yfigure_container_create(&ctx);
    YTEST_REQUIRE_OK(test, cont);
    struct yetty_ycore_rectangle rect = {{0, 0}, {200, 200}};
    yetty_yfigure_container_set_registry(cont.value, reg.value);
    yetty_yfigure_container_set_rect(cont.value, rect);

    *out_registry = reg.value;
    return cont.value;
}

static void add_child(struct ytest *test, struct yetty_yclass_object *container, uint32_t id,
                      float x, float y, float w, float h)
{
    struct yetty_ycore_rectangle rect = {{x, y}, {x + w, y + h}};
    struct yetty_ycore_buffer init = {0};
    struct yetty_ycore_void_result r = yetty_yfigure_create_child(
        container, yetty_yfigure_kind_token(STUB_KIND), id, rect, init);
    YTEST_REQUIRE_OK(test, r);
}

static uint32_t hit_id(struct ytest *test, struct yetty_yclass_object *container, float x, float y)
{
    struct yetty_yfigure_hit_result hit = yetty_yfigure_container_hit_test(container, x, y);
    YTEST_REQUIRE_OK(test, hit);
    return hit.value.figure_id;
}

static int is_dirty(struct ytest *test, struct yetty_yclass_object *obj)
{
    struct yetty_ycore_int_result d = yetty_yfigure_figure_dirty_get(obj);
    YTEST_REQUIRE_OK(test, d);
    return d.value;
}

static void clear_dirty(struct ytest *test, struct yetty_yclass_object *obj)
{
    YTEST_REQUIRE_OK(test, yetty_yfigure_figure_dirty_set(obj, 0));
}

/*---------------------------------------------------------------------------
 * Empty / miss: hit-test on an empty container and outside every child both
 * return figure_id 0 rather than an arbitrary child.
 *-------------------------------------------------------------------------*/
static void test_hit_test_empty_and_miss(struct ytest *test)
{
    struct yetty_yfigure_registry *reg = NULL;
    struct yetty_yclass_object *container = make_container(test, &reg);

    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 10, 10), 0u); /* empty */

    add_child(test, container, 1, 0, 0, 40, 40); /* small corner child */
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 20, 20), 1u); /* inside */
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 150, 150), 0u); /* outside */

    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(reg);
}

/*---------------------------------------------------------------------------
 * Hidden children are skipped by the hit-test; the hit falls through to the
 * next visible child below, and unhiding restores it.
 *-------------------------------------------------------------------------*/
static void test_hit_test_hidden_falls_through(struct ytest *test)
{
    struct yetty_yfigure_registry *reg = NULL;
    struct yetty_yclass_object *container = make_container(test, &reg);

    /* Two fully overlapping children; 2 is on top (higher z). */
    add_child(test, container, 1, 0, 0, 100, 100);
    add_child(test, container, 2, 0, 0, 100, 100);
    YTEST_REQUIRE_OK(test, yetty_yfigure_set_child_z(container, 1, 0));
    YTEST_REQUIRE_OK(test, yetty_yfigure_set_child_z(container, 2, 10));
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 50, 50), 2u);

    /* Hide the top child → the hit falls through to the one below. */
    YTEST_REQUIRE_OK(test, yetty_yfigure_set_child_hidden(container, 2, 1));
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 50, 50), 1u);

    /* Hide both → no hit. */
    YTEST_REQUIRE_OK(test, yetty_yfigure_set_child_hidden(container, 1, 1));
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 50, 50), 0u);

    /* Unhide the top → it wins again. */
    YTEST_REQUIRE_OK(test, yetty_yfigure_set_child_hidden(container, 2, 0));
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 50, 50), 2u);

    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(reg);
}

/*---------------------------------------------------------------------------
 * Deleting the topmost child exposes the next one down to the hit-test.
 *-------------------------------------------------------------------------*/
static void test_hit_test_after_delete(struct ytest *test)
{
    struct yetty_yfigure_registry *reg = NULL;
    struct yetty_yclass_object *container = make_container(test, &reg);

    add_child(test, container, 1, 0, 0, 100, 100);
    add_child(test, container, 2, 0, 0, 100, 100);
    YTEST_REQUIRE_OK(test, yetty_yfigure_set_child_z(container, 1, 0));
    YTEST_REQUIRE_OK(test, yetty_yfigure_set_child_z(container, 2, 10));
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 50, 50), 2u);

    YTEST_REQUIRE_OK(test, yetty_yfigure_delete_child(container, 2));
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 50, 50), 1u);

    YTEST_REQUIRE_OK(test, yetty_yfigure_delete_child(container, 1));
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 50, 50), 0u);

    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(reg);
}

/*---------------------------------------------------------------------------
 * set_child_rect moves the hittable region: the old spot misses, the new spot
 * hits with correctly re-origined local coordinates.
 *-------------------------------------------------------------------------*/
static void test_set_child_rect_moves_hit(struct ytest *test)
{
    struct yetty_yfigure_registry *reg = NULL;
    struct yetty_yclass_object *container = make_container(test, &reg);

    add_child(test, container, 1, 0, 0, 40, 40);
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 20, 20), 1u);
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 120, 120), 0u);

    struct yetty_ycore_rectangle moved = {{100, 100}, {140, 140}};
    YTEST_REQUIRE_OK(test, yetty_yfigure_set_child_rect(container, 1, moved));

    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 20, 20), 0u); /* old spot empty */
    struct yetty_yfigure_hit_result hit = yetty_yfigure_container_hit_test(container, 120, 120);
    YTEST_REQUIRE_OK(test, hit);
    YTEST_CHECK_EQ_SIZE(test, hit.value.figure_id, 1u);
    YTEST_CHECK_NEAR(test, hit.value.local_x, 20.0f, 0.5f); /* 120 - 100 */
    YTEST_CHECK_NEAR(test, hit.value.local_y, 20.0f, 0.5f);

    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(reg);
}

/*---------------------------------------------------------------------------
 * Dirty propagation: every child mutation marks the CONTAINER dirty, so the
 * compositor re-renders. Covers body, rect, hide, delete and clear_all.
 *-------------------------------------------------------------------------*/
static void test_dirty_propagates_to_container(struct ytest *test)
{
    struct yetty_yfigure_registry *reg = NULL;
    struct yetty_yclass_object *container = make_container(test, &reg);
    add_child(test, container, 1, 0, 0, 100, 100);

    /* apply_child_body → container dirty. */
    clear_dirty(test, container);
    struct yetty_ycore_buffer body = {.data = (uint8_t *)"payload", .capacity = 0, .size = 7};
    YTEST_REQUIRE_OK(test, yetty_yfigure_apply_child_body(container, 1, body));
    YTEST_CHECK(test, is_dirty(test, container));

    /* set_child_rect → container dirty. */
    clear_dirty(test, container);
    struct yetty_ycore_rectangle moved = {{10, 10}, {60, 60}};
    YTEST_REQUIRE_OK(test, yetty_yfigure_set_child_rect(container, 1, moved));
    YTEST_CHECK(test, is_dirty(test, container));

    /* Hiding a visible child → container dirty. */
    clear_dirty(test, container);
    YTEST_REQUIRE_OK(test, yetty_yfigure_set_child_hidden(container, 1, 1));
    YTEST_CHECK(test, is_dirty(test, container));

    /* delete_child → container dirty. */
    clear_dirty(test, container);
    YTEST_REQUIRE_OK(test, yetty_yfigure_delete_child(container, 1));
    YTEST_CHECK(test, is_dirty(test, container));

    /* clear_all → container dirty. */
    add_child(test, container, 2, 0, 0, 50, 50);
    clear_dirty(test, container);
    YTEST_REQUIRE_OK(test, yetty_yfigure_clear_all(container));
    YTEST_CHECK(test, is_dirty(test, container));

    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(reg);
}

int main(void)
{
    struct ytest test = ytest_begin("yfigure_model");
    YTEST_RUN(&test, test_hit_test_empty_and_miss);
    YTEST_RUN(&test, test_hit_test_hidden_falls_through);
    YTEST_RUN(&test, test_hit_test_after_delete);
    YTEST_RUN(&test, test_set_child_rect_moves_hit);
    YTEST_RUN(&test, test_dirty_propagates_to_container);
    return ytest_end(&test);
}
