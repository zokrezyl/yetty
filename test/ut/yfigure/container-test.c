/*
 * yfigure container contract test — z-order, hit testing, dirty propagation.
 *
 * Complements yfigure-wire-test.c (create/delete/clear/rect/body + local vs
 * RPC) by pinning the parts that test never touches: stacking order and the
 * topmost-wins hit test, and the per-figure dirty flag lifecycle. Uses a stub
 * figure (counts bytes, no GPU/font) registered under one kind token.
 */

#include "yetty/gen/impl/yfigure/container.h"
#include "yetty/gen/impl/yfigure/figure.h"
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
        .name = "yfigure_container_test_stub",
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

/*===========================================================================
 * Veil stub — input-TRANSPARENT except its top-left 50x50 corner (#699.4).
 * Pins the container's hit_opaque consult: a topmost figure that does not
 * consume at the point yields the hit to the figure below.
 *===========================================================================*/
static struct yetty_yclass_ptr_result veil_class_get(void);

static struct yetty_ycore_int_result veil_hit_opaque(struct yetty_yclass_object *obj, float local_x,
                                                     float local_y)
{
    (void)obj;
    return YETTY_OK(yetty_ycore_int, (local_x < 50.0f && local_y < 50.0f) ? 1 : 0);
}

static struct yetty_yclass_ptr_result veil_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    static const struct yetty_yclass_descriptor desc = {
        .name = "yfigure_container_test_veil",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct stub_figure),
        .data_align = _Alignof(struct stub_figure),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render,
         (yetty_yclass_impl_t)stub_render},
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy,
         (yetty_yclass_impl_t)stub_destroy},
        {"yetty_yfigure", "hit_opaque", (yetty_yclass_method_id_t)yetty_yfigure_hit_opaque,
         (yetty_yclass_impl_t)veil_hit_opaque},
    };
    struct yetty_yclass_ptr_result parent = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(parent)) {
        return parent;
    }
    struct yetty_yclass_ptr_result registered =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), parent.value, NULL, 0);
    if (YETTY_IS_OK(registered)) {
        cls = registered.value;
    }
    return registered;
}

static struct yetty_yfigure_figure_ptr_result veil_factory(struct yetty_ycore_rectangle rect,
                                                           const struct yetty_context *ctx,
                                                           void *user)
{
    (void)ctx;
    (void)user;
    struct yetty_yclass_ptr_result cls = veil_class_get();
    if (YETTY_IS_ERR(cls)) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "veil_factory: class", cls);
    }
    struct yetty_yclass_object_ptr_result obj = yetty_yclass_object_alloc(cls.value);
    if (YETTY_IS_ERR(obj)) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "veil_factory: alloc", obj);
    }
    struct stub_figure *figure = stub_from_obj(obj.value);
    figure->base = (struct yetty_yfigure_figure *)(obj.value + 1);
    yetty_yfigure_figure_rect_set(obj.value, rect);
    yetty_yfigure_figure_dirty_set(obj.value, 1);
    return YETTY_OK(yetty_yfigure_figure_ptr, figure->base);
}

#define VEIL_KIND "yfigure_container_test_veil"

#define STUB_KIND "yfigure_container_test_stub"

/* Build a container with the stub kind registered. Out-param returns the
 * registry (caller frees). */
static struct yetty_yclass_object *make_container(struct ytest *test,
                                                  struct yetty_yfigure_registry **out_registry)
{
    struct yetty_yfigure_registry_ptr_result reg = yetty_yfigure_registry_create();
    YTEST_REQUIRE_OK(test, reg);
    struct yetty_ycore_void_result rr = yetty_yfigure_registry_register(
        reg.value, yetty_yfigure_kind_token(STUB_KIND), stub_factory, NULL);
    YTEST_REQUIRE_OK(test, rr);
    struct yetty_ycore_void_result veil_rr = yetty_yfigure_registry_register(
        reg.value, yetty_yfigure_kind_token(VEIL_KIND), veil_factory, NULL);
    YTEST_REQUIRE_OK(test, veil_rr);

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
    struct yetty_ycore_void_result r =
        yetty_yfigure_create_child(container, yetty_yfigure_kind_token(STUB_KIND), id, rect, init);
    YTEST_REQUIRE_OK(test, r);
}

static uint32_t hit_id(struct ytest *test, struct yetty_yclass_object *container, float x, float y)
{
    struct yetty_yfigure_hit_result hit = yetty_yfigure_container_hit_test(container, x, y);
    YTEST_REQUIRE_OK(test, hit);
    return hit.value.figure_id;
}

/*---------------------------------------------------------------------------
 * Tests.
 *-------------------------------------------------------------------------*/
static void test_hit_test_basic(struct ytest *test)
{
    struct yetty_yfigure_registry *reg = NULL;
    struct yetty_yclass_object *container = make_container(test, &reg);

    add_child(test, container, 1, 0, 0, 100, 100);   /* A: top-left quadrant */
    add_child(test, container, 2, 100, 100, 80, 80); /* B: bottom-right, disjoint */

    /* A point inside A only. */
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 25, 25), 1u);
    /* A point inside B only, with correct local coords. */
    struct yetty_yfigure_hit_result hit_b = yetty_yfigure_container_hit_test(container, 120, 130);
    YTEST_REQUIRE_OK(test, hit_b);
    YTEST_CHECK_EQ_SIZE(test, hit_b.value.figure_id, 2u);
    YTEST_CHECK_NEAR(test, hit_b.value.local_x, 20.0f, 0.5f); /* 120 - 100 */
    YTEST_CHECK_NEAR(test, hit_b.value.local_y, 30.0f, 0.5f); /* 130 - 100 */
    /* A point outside every child misses (figure_id 0). */
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 195, 5), 0u);

    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(reg);
}

/* Overlapping children: the LAST-inserted (top of the z-stack) wins the hit —
 * the overlay-first input contract (#699.4): an overlay figure seated above
 * the content figure at the same rect receives the pointer first. */
static void test_hit_test_topmost_overlap(struct ytest *test)
{
    struct yetty_yfigure_registry *reg = NULL;
    struct yetty_yclass_object *container = make_container(test, &reg);

    add_child(test, container, 1, 0, 0, 200, 200); /* content: full rect */
    add_child(test, container, 2, 0, 0, 200, 200); /* overlay: same rect, on top */

    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 50, 50), 2u);
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 199, 199), 2u);

    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(reg);
}

/* #699.4 overlay-first consumption: the topmost figure takes the hit ONLY
 * where it reports itself input-opaque; elsewhere the event falls through to
 * the figure below (consume one, reject one). */
static void test_hit_opaque_fall_through(struct ytest *test)
{
    struct yetty_yfigure_registry *reg = NULL;
    struct yetty_yclass_object *container = make_container(test, &reg);

    add_child(test, container, 1, 0, 0, 200, 200); /* content: opaque default */
    struct yetty_ycore_rectangle overlay_rect = {{0, 0}, {200, 200}};
    struct yetty_ycore_buffer overlay_init = {0};
    YTEST_REQUIRE_OK(test,
                     yetty_yfigure_create_child(container, yetty_yfigure_kind_token(VEIL_KIND), 2,
                                                overlay_rect, overlay_init));

    /* Inside the veil's opaque corner: the overlay CONSUMES. */
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 25, 25), 2u);
    /* Outside it: the overlay REJECTS; the hit falls through to content,
     * with content's local coords. */
    struct yetty_yfigure_hit_result through = yetty_yfigure_container_hit_test(container, 150, 150);
    YTEST_REQUIRE_OK(test, through);
    YTEST_CHECK_EQ_SIZE(test, through.value.figure_id, 1u);
    YTEST_CHECK_NEAR(test, through.value.local_x, 150.0f, 0.5f);
    YTEST_CHECK_NEAR(test, through.value.local_y, 150.0f, 0.5f);

    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(reg);
}

static void test_z_order_topmost_wins(struct ytest *test)
{
    struct yetty_yfigure_registry *reg = NULL;
    struct yetty_yclass_object *container = make_container(test, &reg);

    /* Two overlapping children; (60,60) is inside both. */
    add_child(test, container, 1, 0, 0, 100, 100);
    add_child(test, container, 2, 50, 50, 100, 100);

    /* Raise 2 above 1 → 2 wins the overlap. */
    YTEST_REQUIRE_OK(test, yetty_yfigure_set_child_z(container, 2, 10));
    YTEST_REQUIRE_OK(test, yetty_yfigure_set_child_z(container, 1, 0));
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 60, 60), 2u);

    /* Raise 1 above 2 → 1 wins. */
    YTEST_REQUIRE_OK(test, yetty_yfigure_set_child_z(container, 1, 20));
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 60, 60), 1u);

    /* raise_child_by_id lifts 2 back to the top. */
    YTEST_REQUIRE_OK(test, yetty_yfigure_container_raise_child_by_id(container, 2));
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 60, 60), 2u);

    /* The non-overlapping region of 1 still hits 1 regardless of z. */
    YTEST_CHECK_EQ_SIZE(test, hit_id(test, container, 10, 10), 1u);

    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(reg);
}

static void test_dirty_lifecycle(struct ytest *test)
{
    struct yetty_yfigure_registry *reg = NULL;
    struct yetty_yclass_object *container = make_container(test, &reg);
    add_child(test, container, 1, 0, 0, 100, 100);

    struct yetty_yfigure_figure_ptr_result child_res =
        yetty_yfigure_container_find_child_by_id(container, 1);
    YTEST_REQUIRE_OK(test, child_res);
    YTEST_REQUIRE_NOT_NULL(test, child_res.value);
    struct yetty_yclass_object *child = (struct yetty_yclass_object *)child_res.value - 1;

    /* Freshly created → dirty. */
    struct yetty_ycore_int_result dirty0 = yetty_yfigure_figure_dirty_get(child);
    YTEST_REQUIRE_OK(test, dirty0);
    YTEST_CHECK(test, dirty0.value);

    /* Clear it, then a body application re-dirties the child. */
    YTEST_REQUIRE_OK(test, yetty_yfigure_figure_dirty_set(child, 0));
    struct yetty_ycore_int_result dirty1 = yetty_yfigure_figure_dirty_get(child);
    YTEST_REQUIRE_OK(test, dirty1);
    YTEST_CHECK(test, !dirty1.value);

    struct yetty_ycore_buffer body = {.data = (uint8_t *)"payload", .capacity = 0, .size = 7};
    YTEST_REQUIRE_OK(test, yetty_yfigure_apply_child_body(container, 1, body));
    struct yetty_ycore_int_result dirty2 = yetty_yfigure_figure_dirty_get(child);
    YTEST_REQUIRE_OK(test, dirty2);
    YTEST_CHECK(test, dirty2.value);

    /* The body actually reached the stub. */
    YTEST_CHECK(test, stub_from_obj(child)->bytes_seen == 7);

    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(reg);
}

int main(void)
{
    struct ytest test = ytest_begin("yfigure_container");
    YTEST_RUN(&test, test_hit_test_basic);
    YTEST_RUN(&test, test_hit_test_topmost_overlap);
    YTEST_RUN(&test, test_hit_opaque_fall_through);
    YTEST_RUN(&test, test_z_order_topmost_wins);
    YTEST_RUN(&test, test_dirty_lifecycle);
    return ytest_end(&test);
}
