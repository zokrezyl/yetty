/*
 * ygui-object-test.c — Phase 1+2+4 sanity test.
 *
 * Builds two trivial classes on top of the base widget class — one
 * that overrides on_press, one that overrides on_motion — and asserts:
 *   - class registration succeeds for both
 *   - instance allocation initialises the widget data slice
 *   - dispatch routes to the overridden implementation
 *   - non-overridden methods fall through to the base default
 *   - super invocation reaches the parent's implementation
 *   - tooltip pilot widget exercises the path end-to-end (data_get +
 *     paint hook write into the framework's ygrid_body buffer)
 *   - destructor chain runs leaf-first without leaking
 *
 * Assertions use the shared ytest.h harness so the checks stay live under
 * Release/NDEBUG.
 */

#include <yetty/ydraw-list/drawable-list.h>
#include "yetty/gen/impl/ygui/mixins/draggable.h"
#include "yetty/gen/impl/ygui/widget.h"
#include <yetty/ygui/ygui.h>
#include "yetty/gen/impl/ygui/widgets/tooltip.h"

/* This test pokes at internals — it hand-registers a probe class via
 * yclass and needs ygui's slot domain (YETTY_YGUI_DOMAIN). */
#include <yetty/ygui/internal.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*-----------------------------------------------------------------------------
 * Probe class A — overrides on_press, returns 1 (consumed) + records a tag
 * in its own data slice so the test can verify the dispatch landed here.
 *---------------------------------------------------------------------------*/

struct probe_a_data {
    int press_count;
    int motion_count;
};

static const struct yetty_yclass *probe_a_class_get(void);

static struct yetty_ycore_int_result probe_a_on_press(struct yetty_yclass_object *_yc_obj, float x,
                                                      float y, int button)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)_yc_obj;
    (void)x;
    (void)y;
    (void)button;
    struct probe_a_data *d = yetty_yclass_object_data(obj, probe_a_class_get()).value;
    d->press_count++;
    return YETTY_OK(yetty_ycore_int, 1);
}

static const struct yetty_yclass_op probe_a_ops[] = {
    {YETTY_YGUI_DOMAIN, "widget_on_press", (yetty_yclass_method_id_t)yetty_ygui_widget_on_press,
     (yetty_yclass_impl_t)probe_a_on_press}};

static const struct yetty_yclass_descriptor probe_a_desc = {
    .name = "probe_a",
    .type = YETTY_YCLASS_TYPE_REGULAR,
    .data_size = sizeof(struct probe_a_data),
    .data_align = _Alignof(struct probe_a_data),
};

static const struct yetty_yclass *probe_a_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (!cls) {
        const struct yetty_yclass *mixins[] = {NULL};
        struct yetty_yclass_ptr_result r = yetty_yclass_register(
            &probe_a_desc, probe_a_ops, sizeof(probe_a_ops) / sizeof(probe_a_ops[0]),
            yetty_ygui_widget_class_get().value, mixins, 0);
        if (YETTY_IS_OK(r)) {
            cls = r.value;
        } else {
            yetty_ycore_error_destroy(r.error);
        }
    }
    return cls;
}

/*-----------------------------------------------------------------------------
 * Probe class B — subclass of probe_a. Overrides on_motion only; on_press
 * inherits from probe_a. Constructor chains via super_void.
 *---------------------------------------------------------------------------*/

struct probe_b_data {
    int motion_count;
    int ctor_marker;
};

static const struct yetty_yclass *probe_b_class_get(void);

static struct yetty_ycore_void_result probe_b_constructor(struct yetty_yclass_object *_yc_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)_yc_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, probe_b_class_get(), (yetty_yclass_method_id_t)yetty_ygui_constructor);
    if (YETTY_IS_ERR(sr)) {
        return sr;
    }
    struct probe_b_data *d = yetty_yclass_object_data(obj, probe_b_class_get()).value;
    d->ctor_marker = 0xCAFE;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_int_result probe_b_on_motion(struct yetty_yclass_object *_yc_obj, float x,
                                                       float y)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)_yc_obj;
    (void)x;
    (void)y;
    struct probe_b_data *d = yetty_yclass_object_data(obj, probe_b_class_get()).value;
    d->motion_count++;
    return YETTY_OK(yetty_ycore_int, 1);
}

static const struct yetty_yclass_op probe_b_ops[] = {
    {YETTY_YGUI_DOMAIN, "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor,
     (yetty_yclass_impl_t)probe_b_constructor},
    {YETTY_YGUI_DOMAIN, "widget_on_motion", (yetty_yclass_method_id_t)yetty_ygui_widget_on_motion,
     (yetty_yclass_impl_t)probe_b_on_motion}};

static const struct yetty_yclass_descriptor probe_b_desc = {
    .name = "probe_b",
    .type = YETTY_YCLASS_TYPE_REGULAR,
    .data_size = sizeof(struct probe_b_data),
    .data_align = _Alignof(struct probe_b_data),
};

static const struct yetty_yclass *probe_b_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (!cls) {
        const struct yetty_yclass *mixins[] = {NULL};
        struct yetty_yclass_ptr_result r = yetty_yclass_register(
            &probe_b_desc, probe_b_ops, sizeof(probe_b_ops) / sizeof(probe_b_ops[0]),
            probe_a_class_get(), mixins, 0);
        if (YETTY_IS_OK(r)) {
            cls = r.value;
        } else {
            yetty_ycore_error_destroy(r.error);
        }
    }
    return cls;
}

/*-----------------------------------------------------------------------------
 * Tests.
 *---------------------------------------------------------------------------*/

static void test_class_registration(struct ytest *test)
{
    const struct yetty_yclass *base = yetty_ygui_widget_class_get().value;
    YTEST_CHECK_NOT_NULL(test, base);
    const struct yetty_yclass *a = probe_a_class_get();
    YTEST_CHECK_NOT_NULL(test, a);
    const struct yetty_yclass *b = probe_b_class_get();
    YTEST_CHECK_NOT_NULL(test, b);
    /* Re-calling the accessor must return the cached pointer. */
    YTEST_CHECK(test, probe_a_class_get() == a);
    YTEST_CHECK(test, probe_b_class_get() == b);
}

static void test_instance_alloc_and_dispatch(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_new(probe_a_class_get());
    YTEST_REQUIRE_OK(test, r);
    struct yetty_yclass_object *obj = r.value;

    /* Base widget class data slice should be present and zero-initialised. */
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YTEST_REQUIRE_OK(test, rect_res);
    struct yetty_ycore_rectangle rect = rect_res.value;
    YTEST_CHECK(test, rect.min.x == 0 && rect.max.x == 0);

    /* Dispatch on_press → probe_a override. */
    struct yetty_ycore_int_result pr =
        yetty_ygui_widget_on_press((struct yetty_yclass_object *)obj, 1.0f, 2.0f, 0);
    YTEST_REQUIRE_OK(test, pr);
    YTEST_CHECK_EQ_INT(test, pr.value, 1);
    struct probe_a_data *ad = yetty_yclass_object_data(obj, probe_a_class_get()).value;
    YTEST_CHECK_EQ_INT(test, ad->press_count, 1);

    /* Dispatch on_motion → falls through to base widget default (return 0). */
    struct yetty_ycore_int_result mr =
        yetty_ygui_widget_on_motion((struct yetty_yclass_object *)obj, 1.0f, 2.0f);
    YTEST_REQUIRE_OK(test, mr);
    YTEST_CHECK_EQ_INT(test, mr.value, 0);

    /* Clean up. */
    struct yetty_ycore_void_result dr = yetty_ygui_widget_destroy(obj);
    YTEST_CHECK_OK(test, dr);
}

static void test_subclass_dispatch_and_super(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_new(probe_b_class_get());
    YTEST_REQUIRE_OK(test, r);
    struct yetty_yclass_object *obj = r.value;

    /* Constructor chained: probe_b's ctor sets marker, base widget's
     * ctor initialised the layout struct. */
    struct probe_b_data *bd = yetty_yclass_object_data(obj, probe_b_class_get()).value;
    YTEST_CHECK_EQ_INT(test, bd->ctor_marker, 0xCAFE);
    struct yetty_ygui_layout_const_ptr_result lay_res = yetty_ygui_widget_layout_get(obj);
    YTEST_REQUIRE_OK(test, lay_res);
    const struct yetty_ygui_layout *lay = lay_res.value;
    YTEST_REQUIRE_NOT_NULL(test, lay);
    YTEST_CHECK_EQ_INT(test, lay->direction, YETTY_YGUI_FLEX_ROW);

    /* on_motion → probe_b's override. */
    struct yetty_ycore_int_result mr =
        yetty_ygui_widget_on_motion((struct yetty_yclass_object *)obj, 0, 0);
    YTEST_REQUIRE_OK(test, mr);
    YTEST_CHECK_EQ_INT(test, mr.value, 1);
    YTEST_CHECK_EQ_INT(test, bd->motion_count, 1);

    /* on_press → inherited from probe_a. */
    struct yetty_ycore_int_result pr =
        yetty_ygui_widget_on_press((struct yetty_yclass_object *)obj, 0, 0, 0);
    YTEST_REQUIRE_OK(test, pr);
    YTEST_CHECK_EQ_INT(test, pr.value, 1);
    struct probe_a_data *ad = yetty_yclass_object_data(obj, probe_a_class_get()).value;
    YTEST_CHECK_EQ_INT(test, ad->press_count, 1);

    yetty_ygui_widget_destroy(obj);
}

static void test_tooltip_pilot(struct ytest *test)
{
    const struct yetty_yclass *cls = yetty_ygui_tooltip_class_get().value;
    YTEST_REQUIRE_NOT_NULL(test, cls);

    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_new(cls);
    YTEST_REQUIRE_OK(test, r);
    struct yetty_yclass_object *obj = r.value;

    /* Default label is NULL. */
    struct yetty_ycore_const_char_ptr_result text_default = yetty_ygui_tooltip_get_text(obj);
    YTEST_REQUIRE_OK(test, text_default);
    YTEST_CHECK_NULL(test, text_default.value);

    struct yetty_ycore_void_result sr = yetty_ygui_tooltip_set_text(obj, "hello");
    YTEST_REQUIRE_OK(test, sr);
    struct yetty_ycore_const_char_ptr_result text_after = yetty_ygui_tooltip_get_text(obj);
    YTEST_REQUIRE_OK(test, text_after);
    YTEST_CHECK_STR_EQ(test, text_after.value, "hello");
    struct yetty_ycore_int_result dirty_after = yetty_ygui_widget_is_dirty(obj);
    YTEST_REQUIRE_OK(test, dirty_after);
    YTEST_CHECK(test, dirty_after.value);

    /* Position the widget so paint emits a TEXT_DRAWABLE_LIST at a known coord. */
    struct yetty_ycore_rectangle wr = {{10.0f, 20.0f}, {200.0f, 60.0f}};
    yetty_ygui_widget_set_rect(obj, wr);

    /* Drive a minimal pass-2 emit: set up an ad-hoc drawable_list, call
     * paint, and verify a TEXT_DRAWABLE_LIST drawable-list entry record landed. */
    struct yetty_ydraw_drawable_list_result dlr =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, dlr);
    struct yetty_ygui_emit_ctx ctx = {
        .framework = NULL,
        .ygrid_drawable_list = dlr.value,
        .current_figure_id = 0,
    };
    struct yetty_ycore_void_result er =
        yetty_ygui_widget_paint((struct yetty_yclass_object *)obj, &ctx);
    YTEST_REQUIRE_OK(test, er);
    /* First u32 in the primitives buffer is the type word —
     * YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST = 0x40000002 (no id flag, anonymous). */
    const uint32_t *prims = (const uint32_t *)yetty_ydraw_drawable_list_data(dlr.value);
    size_t prims_size = yetty_ydraw_drawable_list_size(dlr.value);
    YTEST_REQUIRE(test, prims_size >= 8);
    YTEST_CHECK_EQ_INT(test, prims[0], 0x40000002u);
    yetty_ydraw_drawable_list_destroy(dlr.value);
    yetty_ygui_widget_destroy(obj);
}

int main(void)
{
    struct ytest test = ytest_begin("ygui_object");
    YTEST_RUN(&test, test_class_registration);
    YTEST_RUN(&test, test_instance_alloc_and_dispatch);
    YTEST_RUN(&test, test_subclass_dispatch_and_super);
    YTEST_RUN(&test, test_tooltip_pilot);
    return ytest_end(&test);
}
