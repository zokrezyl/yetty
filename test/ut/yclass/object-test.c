/*
 * yclass object-model contract test.
 *
 * Builds a small class hierarchy directly on the yclass runtime — no generated
 * code — and pins the object/dispatch spine every module depends on:
 *
 *   ytshape_base (regular)
 *     └── ytshape_square (regular, parent = base)          overrides area, sides
 *           + ytshape_meta (mixin)                          provides label
 *           = ytshape_tagged (parent = square, mixin meta)
 *
 * Covers: registration, object alloc/free, data-slice access + alignment +
 * non-overlap, inheritance dispatch, super calls, mixin dispatch, and the
 * dispatch-lookup / slot-registration error and idempotence contracts.
 *
 * Method stubs self-dispatch: each stub's own address is its slot key, so
 * calling ytshape_area(obj) resolves the slot and invokes the impl the object's
 * class installed — exactly the path generated stubs take.
 */

#include <yetty/yclass/class.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>

/*---------------------------------------------------------------------------
 * Data slices. Deliberately mixed alignment so the layout test is meaningful:
 * base carries a double (align 8), square an int (align 4), meta a long long
 * (align 8).
 *-------------------------------------------------------------------------*/
struct base_data {
    double scale;
    int marker;
};
struct square_data {
    int side;
};
struct meta_data {
    long long tag;
};

#define YTSHAPE_DOMAIN "ytshape"

/*---------------------------------------------------------------------------
 * Self-dispatching stubs. The address of each is the vtable key; calling it
 * looks up the slot on obj->klass and invokes the installed impl.
 *-------------------------------------------------------------------------*/
static const struct yetty_yclass *base_class_get(void);
static const struct yetty_yclass *meta_class_get(void);

typedef struct yetty_ycore_int_result (*int_method_fn)(struct yetty_yclass_object *);

static struct yetty_ycore_int_result dispatch_int(struct yetty_yclass_object *obj,
                                                  yetty_yclass_method_id_t key, const char *what)
{
    struct yetty_yclass_method_slot_result slot_res =
        yetty_yclass_method_slot_get(YTSHAPE_DOMAIN, key);
    if (YETTY_IS_ERR(slot_res)) {
        return YETTY_ERR(yetty_ycore_int, what, slot_res);
    }
    struct yetty_yclass_impl_t_result impl_res =
        yetty_yclass_dispatch_lookup(obj->klass, slot_res.value);
    if (YETTY_IS_ERR(impl_res)) {
        return YETTY_ERR(yetty_ycore_int, what, impl_res);
    }
    return ((int_method_fn)impl_res.value)(obj);
}

static struct yetty_ycore_int_result ytshape_area(struct yetty_yclass_object *obj)
{
    return dispatch_int(obj, (yetty_yclass_method_id_t)ytshape_area, "ytshape_area");
}
static struct yetty_ycore_int_result ytshape_sides(struct yetty_yclass_object *obj)
{
    return dispatch_int(obj, (yetty_yclass_method_id_t)ytshape_sides, "ytshape_sides");
}
static struct yetty_ycore_int_result ytshape_label(struct yetty_yclass_object *obj)
{
    return dispatch_int(obj, (yetty_yclass_method_id_t)ytshape_label, "ytshape_label");
}

/* Invoke a specific class's impl for a slot — the "super" primitive. */
static struct yetty_ycore_int_result call_impl_of(const struct yetty_yclass *cls,
                                                  yetty_yclass_method_id_t key,
                                                  struct yetty_yclass_object *obj)
{
    struct yetty_yclass_method_slot_result slot_res =
        yetty_yclass_method_slot_get(YTSHAPE_DOMAIN, key);
    if (YETTY_IS_ERR(slot_res)) {
        return YETTY_ERR(yetty_ycore_int, "call_impl_of: slot", slot_res);
    }
    struct yetty_yclass_impl_t_result impl_res = yetty_yclass_dispatch_lookup(cls, slot_res.value);
    if (YETTY_IS_ERR(impl_res)) {
        return YETTY_ERR(yetty_ycore_int, "call_impl_of: dispatch", impl_res);
    }
    return ((int_method_fn)impl_res.value)(obj);
}

/*---------------------------------------------------------------------------
 * Implementations.
 *-------------------------------------------------------------------------*/
static struct yetty_ycore_int_result base_area_impl(struct yetty_yclass_object *obj)
{
    struct base_data *data = yetty_yclass_object_data(obj, base_class_get()).value;
    return YETTY_OK(yetty_ycore_int, data->marker);
}
static struct yetty_ycore_int_result base_sides_impl(struct yetty_yclass_object *obj)
{
    (void)obj;
    return YETTY_OK(yetty_ycore_int, 0);
}

static const struct yetty_yclass *square_class_get(void);

static struct yetty_ycore_int_result square_area_impl(struct yetty_yclass_object *obj)
{
    struct square_data *data = yetty_yclass_object_data(obj, square_class_get()).value;
    return YETTY_OK(yetty_ycore_int, data->side * data->side);
}
static struct yetty_ycore_int_result square_sides_impl(struct yetty_yclass_object *obj)
{
    /* super: reach the base default (0) explicitly, then add this class's own
     * contribution — the canonical super-call shape. */
    struct yetty_ycore_int_result super =
        call_impl_of(base_class_get(), (yetty_yclass_method_id_t)ytshape_sides, obj);
    if (YETTY_IS_ERR(super)) {
        return super;
    }
    return YETTY_OK(yetty_ycore_int, super.value + 4);
}

static struct yetty_ycore_int_result meta_label_impl(struct yetty_yclass_object *obj)
{
    struct meta_data *data = yetty_yclass_object_data(obj, meta_class_get()).value;
    return YETTY_OK(yetty_ycore_int, (int)data->tag);
}

/*---------------------------------------------------------------------------
 * Class accessors (register once, cache).
 *-------------------------------------------------------------------------*/
static const struct yetty_yclass *base_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (!cls) {
        static const struct yetty_yclass_op ops[] = {
            {YTSHAPE_DOMAIN, "area", (yetty_yclass_method_id_t)ytshape_area,
             (yetty_yclass_impl_t)base_area_impl},
            {YTSHAPE_DOMAIN, "sides", (yetty_yclass_method_id_t)ytshape_sides,
             (yetty_yclass_impl_t)base_sides_impl},
        };
        static const struct yetty_yclass_descriptor desc = {
            .name = "ytshape_base",
            .type = YETTY_YCLASS_TYPE_REGULAR,
            .data_size = sizeof(struct base_data),
            .data_align = _Alignof(struct base_data),
        };
        struct yetty_yclass_ptr_result r =
            yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
        if (YETTY_IS_OK(r)) {
            cls = r.value;
        } else {
            yetty_ycore_error_destroy(r.error);
        }
    }
    return cls;
}

static const struct yetty_yclass *square_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (!cls) {
        static const struct yetty_yclass_op ops[] = {
            {YTSHAPE_DOMAIN, "area", (yetty_yclass_method_id_t)ytshape_area,
             (yetty_yclass_impl_t)square_area_impl},
            {YTSHAPE_DOMAIN, "sides", (yetty_yclass_method_id_t)ytshape_sides,
             (yetty_yclass_impl_t)square_sides_impl},
        };
        static const struct yetty_yclass_descriptor desc = {
            .name = "ytshape_square",
            .type = YETTY_YCLASS_TYPE_REGULAR,
            .data_size = sizeof(struct square_data),
            .data_align = _Alignof(struct square_data),
        };
        struct yetty_yclass_ptr_result r = yetty_yclass_register(
            &desc, ops, sizeof(ops) / sizeof(ops[0]), base_class_get(), NULL, 0);
        if (YETTY_IS_OK(r)) {
            cls = r.value;
        } else {
            yetty_ycore_error_destroy(r.error);
        }
    }
    return cls;
}

static const struct yetty_yclass *meta_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (!cls) {
        static const struct yetty_yclass_op ops[] = {
            {YTSHAPE_DOMAIN, "label", (yetty_yclass_method_id_t)ytshape_label,
             (yetty_yclass_impl_t)meta_label_impl},
        };
        static const struct yetty_yclass_descriptor desc = {
            .name = "ytshape_meta",
            .type = YETTY_YCLASS_TYPE_MIXIN,
            .data_size = sizeof(struct meta_data),
            .data_align = _Alignof(struct meta_data),
        };
        struct yetty_yclass_ptr_result r =
            yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
        if (YETTY_IS_OK(r)) {
            cls = r.value;
        } else {
            yetty_ycore_error_destroy(r.error);
        }
    }
    return cls;
}

static const struct yetty_yclass *tagged_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (!cls) {
        static const struct yetty_yclass_descriptor desc = {
            .name = "ytshape_tagged",
            .type = YETTY_YCLASS_TYPE_REGULAR,
            .data_size = 0,
            .data_align = 0,
        };
        const struct yetty_yclass *mixins[] = {meta_class_get()};
        struct yetty_yclass_ptr_result r =
            yetty_yclass_register(&desc, NULL, 0, square_class_get(), mixins, 1);
        if (YETTY_IS_OK(r)) {
            cls = r.value;
        } else {
            yetty_ycore_error_destroy(r.error);
        }
    }
    return cls;
}

/*---------------------------------------------------------------------------
 * Tests.
 *-------------------------------------------------------------------------*/
static void test_register_alloc_free(struct ytest *test)
{
    YTEST_REQUIRE_NOT_NULL(test, base_class_get());
    YTEST_REQUIRE_NOT_NULL(test, square_class_get());
    YTEST_REQUIRE_NOT_NULL(test, meta_class_get());
    YTEST_REQUIRE_NOT_NULL(test, tagged_class_get());

    struct yetty_yclass_object_ptr_result obj_res = yetty_yclass_object_alloc(base_class_get());
    YTEST_REQUIRE_OK(test, obj_res);
    struct yetty_yclass_object *obj = obj_res.value;
    YTEST_CHECK(test, obj->klass == base_class_get());
    YTEST_CHECK_NULL(test, obj->session); /* local object → no RPC session */

    struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
    YTEST_CHECK_OK(test, free_res);

    /* NULL class is rejected, not crashed. */
    struct yetty_yclass_object_ptr_result null_res = yetty_yclass_object_alloc(NULL);
    YTEST_CHECK_ERR(test, null_res);
}

static void test_data_slice_zero_init(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result obj_res = yetty_yclass_object_alloc(base_class_get());
    YTEST_REQUIRE_OK(test, obj_res);
    struct yetty_yclass_object *obj = obj_res.value;

    struct yetty_yclass_void_ptr_result slice_res = yetty_yclass_object_data(obj, base_class_get());
    YTEST_REQUIRE_OK(test, slice_res);
    struct base_data *data = slice_res.value;
    YTEST_REQUIRE_NOT_NULL(test, data);
    /* calloc-backed: every slice starts zeroed. */
    YTEST_CHECK_EQ_INT(test, data->marker, 0);
    YTEST_CHECK(test, data->scale == 0.0);

    data->marker = 7;
    struct base_data *reread = yetty_yclass_object_data(obj, base_class_get()).value;
    YTEST_CHECK_EQ_INT(test, reread->marker, 7);

    yetty_yclass_object_free(obj);
}

static void test_data_slice_alignment_and_overlap(struct ytest *test)
{
    /* A tagged instance carries three data slices (base, square, meta). Each
     * must be aligned to its struct's alignment, and the three must not
     * overlap. */
    struct yetty_yclass_object_ptr_result obj_res = yetty_yclass_object_alloc(tagged_class_get());
    YTEST_REQUIRE_OK(test, obj_res);
    struct yetty_yclass_object *obj = obj_res.value;

    struct yetty_yclass_void_ptr_result base_slice =
        yetty_yclass_object_data(obj, base_class_get());
    struct yetty_yclass_void_ptr_result square_slice =
        yetty_yclass_object_data(obj, square_class_get());
    struct yetty_yclass_void_ptr_result meta_slice =
        yetty_yclass_object_data(obj, meta_class_get());
    YTEST_REQUIRE_OK(test, base_slice);
    YTEST_REQUIRE_OK(test, square_slice);
    YTEST_REQUIRE_OK(test, meta_slice);

    struct base_data *base = base_slice.value;
    struct square_data *square = square_slice.value;
    struct meta_data *meta = meta_slice.value;

    /* Alignment: each slice pointer is suitably aligned for its struct. */
    YTEST_CHECK_EQ_SIZE(test, (uintptr_t)base % _Alignof(struct base_data), 0);
    YTEST_CHECK_EQ_SIZE(test, (uintptr_t)square % _Alignof(struct square_data), 0);
    YTEST_CHECK_EQ_SIZE(test, (uintptr_t)meta % _Alignof(struct meta_data), 0);

    /* data_offset agrees with the slice pointer and is aligned. */
    struct yetty_ycore_size_result base_off =
        yetty_yclass_object_data_offset(tagged_class_get(), base_class_get());
    YTEST_REQUIRE_OK(test, base_off);
    YTEST_CHECK_EQ_SIZE(test, (uintptr_t)base - (uintptr_t)obj, base_off.value);

    /* No aliasing: distinct sentinels in each slice all survive. */
    base->marker = 111;
    square->side = 222;
    meta->tag = 333;
    YTEST_CHECK_EQ_INT(test, base->marker, 111);
    YTEST_CHECK_EQ_INT(test, square->side, 222);
    YTEST_CHECK_EQ_INT(test, (int)meta->tag, 333);

    yetty_yclass_object_free(obj);
}

static void test_inheritance_dispatch(struct ytest *test)
{
    /* base.area returns its marker; base.sides returns 0. */
    struct yetty_yclass_object *base = yetty_yclass_object_alloc(base_class_get()).value;
    YTEST_REQUIRE_NOT_NULL(test, base);
    ((struct base_data *)yetty_yclass_object_data(base, base_class_get()).value)->marker = 7;
    struct yetty_ycore_int_result base_area = ytshape_area(base);
    YTEST_REQUIRE_OK(test, base_area);
    YTEST_CHECK_EQ_INT(test, base_area.value, 7);

    /* square overrides area (side²) and sides (4). */
    struct yetty_yclass_object *square = yetty_yclass_object_alloc(square_class_get()).value;
    YTEST_REQUIRE_NOT_NULL(test, square);
    ((struct square_data *)yetty_yclass_object_data(square, square_class_get()).value)->side = 5;
    struct yetty_ycore_int_result square_area = ytshape_area(square);
    YTEST_REQUIRE_OK(test, square_area);
    YTEST_CHECK_EQ_INT(test, square_area.value, 25);

    yetty_yclass_object_free(base);
    yetty_yclass_object_free(square);
}

static void test_super_call(struct ytest *test)
{
    /* square.sides calls base.sides (0) via super, then adds 4. */
    struct yetty_yclass_object *square = yetty_yclass_object_alloc(square_class_get()).value;
    YTEST_REQUIRE_NOT_NULL(test, square);
    struct yetty_ycore_int_result sides = ytshape_sides(square);
    YTEST_REQUIRE_OK(test, sides);
    YTEST_CHECK_EQ_INT(test, sides.value, 4);
    yetty_yclass_object_free(square);
}

static void test_mixin_dispatch(struct ytest *test)
{
    /* tagged inherits square's area/sides AND the meta mixin's label. */
    struct yetty_yclass_object *obj = yetty_yclass_object_alloc(tagged_class_get()).value;
    YTEST_REQUIRE_NOT_NULL(test, obj);
    ((struct square_data *)yetty_yclass_object_data(obj, square_class_get()).value)->side = 6;
    ((struct meta_data *)yetty_yclass_object_data(obj, meta_class_get()).value)->tag = 99;

    struct yetty_ycore_int_result area = ytshape_area(obj);
    YTEST_REQUIRE_OK(test, area);
    YTEST_CHECK_EQ_INT(test, area.value, 36); /* inherited square override */

    struct yetty_ycore_int_result sides = ytshape_sides(obj);
    YTEST_REQUIRE_OK(test, sides);
    YTEST_CHECK_EQ_INT(test, sides.value, 4); /* inherited square override + super */

    struct yetty_ycore_int_result label = ytshape_label(obj);
    YTEST_REQUIRE_OK(test, label);
    YTEST_CHECK_EQ_INT(test, label.value, 99); /* mixin-provided */

    /* mixin_count / mixin_at reflect the mixin. */
    struct yetty_ycore_size_result mixin_count = yetty_yclass_mixin_count(tagged_class_get());
    YTEST_REQUIRE_OK(test, mixin_count);
    YTEST_CHECK_EQ_SIZE(test, mixin_count.value, 1);

    yetty_yclass_object_free(obj);
}

static void test_dispatch_lookup_errors(struct ytest *test)
{
    struct yetty_yclass_method_slot_result label_slot =
        yetty_yclass_method_slot_get(YTSHAPE_DOMAIN, (yetty_yclass_method_id_t)ytshape_label);
    YTEST_REQUIRE_OK(test, label_slot);

    /* NULL class → error, not crash. */
    struct yetty_yclass_impl_t_result null_cls =
        yetty_yclass_dispatch_lookup(NULL, label_slot.value);
    YTEST_CHECK_ERR(test, null_cls);

    /* base does not implement the mixin-only 'label' slot → error, not a NULL
     * impl silently returned. */
    struct yetty_yclass_impl_t_result missing =
        yetty_yclass_dispatch_lookup(base_class_get(), label_slot.value);
    YTEST_CHECK_ERR(test, missing);

    /* An undefined slot value is rejected. */
    struct yetty_yclass_impl_t_result undefined =
        yetty_yclass_dispatch_lookup(base_class_get(), YETTY_YCLASS_METHOD_SLOT_UNDEFINED);
    YTEST_CHECK_ERR(test, undefined);
}

static void test_slot_registration(struct ytest *test)
{
    /* by-id and by-name resolve to the same slot; re-registration is
     * idempotent (same (domain, name, id) → same slot). */
    struct yetty_yclass_method_slot_result by_id =
        yetty_yclass_method_slot_get(YTSHAPE_DOMAIN, (yetty_yclass_method_id_t)ytshape_area);
    YTEST_REQUIRE_OK(test, by_id);
    struct yetty_yclass_method_slot_result by_name =
        yetty_yclass_method_slot_by_name(YTSHAPE_DOMAIN, "area");
    YTEST_REQUIRE_OK(test, by_name);
    YTEST_CHECK_EQ_SIZE(test, by_id.value, by_name.value);

    struct yetty_yclass_method_slot_result again = yetty_yclass_method_slot_register(
        YTSHAPE_DOMAIN, "area", (yetty_yclass_method_id_t)ytshape_area);
    YTEST_REQUIRE_OK(test, again);
    YTEST_CHECK_EQ_SIZE(test, again.value, by_id.value);
}

int main(void)
{
    struct ytest test = ytest_begin("yclass_object");
    YTEST_RUN(&test, test_register_alloc_free);
    YTEST_RUN(&test, test_data_slice_zero_init);
    YTEST_RUN(&test, test_data_slice_alignment_and_overlap);
    YTEST_RUN(&test, test_inheritance_dispatch);
    YTEST_RUN(&test, test_super_call);
    YTEST_RUN(&test, test_mixin_dispatch);
    YTEST_RUN(&test, test_dispatch_lookup_errors);
    YTEST_RUN(&test, test_slot_registration);
    return ytest_end(&test);
}
