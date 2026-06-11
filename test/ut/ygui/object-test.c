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
 */

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ygui/mixins/draggable.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/widgets/tooltip.h>

/* This test pokes at internals — it hand-registers a probe class via
 * yclass and needs ygui's slot domain (YETTY_YGUI_DOMAIN). */
#include <yetty/ygui/internal.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
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

static struct yetty_ycore_int_result probe_a_on_press(struct yetty_yclass_ctx *_yc_ctx,
                                                      struct yetty_yclass_object *_yc_obj, float x,
                                                      float y, int button)
{
    (void)_yc_ctx;
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
        assert(YETTY_IS_OK(r));
        cls = r.value;
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

static struct yetty_ycore_void_result probe_b_constructor(struct yetty_yclass_ctx *_yc_ctx,
                                                          struct yetty_yclass_object *_yc_obj)
{
    (void)_yc_ctx;
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

static struct yetty_ycore_int_result probe_b_on_motion(struct yetty_yclass_ctx *_yc_ctx,
                                                       struct yetty_yclass_object *_yc_obj, float x,
                                                       float y)
{
    (void)_yc_ctx;
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
        assert(YETTY_IS_OK(r));
        cls = r.value;
    }
    return cls;
}

/*-----------------------------------------------------------------------------
 * Tests.
 *---------------------------------------------------------------------------*/

static void test_class_registration(void)
{
    const struct yetty_yclass *base = yetty_ygui_widget_class_get().value;
    assert(base != NULL);
    const struct yetty_yclass *a = probe_a_class_get();
    assert(a != NULL);
    const struct yetty_yclass *b = probe_b_class_get();
    assert(b != NULL);
    /* Re-calling the accessor must return the cached pointer. */
    assert(probe_a_class_get() == a);
    assert(probe_b_class_get() == b);
}

static void test_instance_alloc_and_dispatch(void)
{
    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_new(probe_a_class_get());
    assert(YETTY_IS_OK(r));
    struct yetty_yclass_object *obj = r.value;

    /* Base widget class data slice should be present and zero-initialised. */
    struct yetty_ycore_rectangle rect = yetty_ygui_widget_rect(obj);
    assert(rect.min.x == 0 && rect.max.x == 0);

    /* Dispatch on_press → probe_a override. */
    struct yetty_ycore_int_result pr =
        yetty_ygui_widget_on_press(NULL, (struct yetty_yclass_object *)obj, 1.0f, 2.0f, 0);
    assert(YETTY_IS_OK(pr));
    assert(pr.value == 1);
    struct probe_a_data *ad = yetty_yclass_object_data(obj, probe_a_class_get()).value;
    assert(ad->press_count == 1);

    /* Dispatch on_motion → falls through to base widget default (return 0). */
    struct yetty_ycore_int_result mr =
        yetty_ygui_widget_on_motion(NULL, (struct yetty_yclass_object *)obj, 1.0f, 2.0f);
    assert(YETTY_IS_OK(mr));
    assert(mr.value == 0);

    /* Clean up. */
    struct yetty_ycore_void_result dr = yetty_ygui_widget_destroy(obj);
    assert(YETTY_IS_OK(dr));
}

static void test_subclass_dispatch_and_super(void)
{
    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_new(probe_b_class_get());
    assert(YETTY_IS_OK(r));
    struct yetty_yclass_object *obj = r.value;

    /* Constructor chained: probe_b's ctor sets marker, base widget's
     * ctor initialised the layout struct. */
    struct probe_b_data *bd = yetty_yclass_object_data(obj, probe_b_class_get()).value;
    assert(bd->ctor_marker == 0xCAFE);
    const struct yetty_ygui_layout *lay = yetty_ygui_widget_layout_get(obj);
    assert(lay != NULL);
    assert(lay->direction == YETTY_YGUI_FLEX_ROW);

    /* on_motion → probe_b's override. */
    struct yetty_ycore_int_result mr =
        yetty_ygui_widget_on_motion(NULL, (struct yetty_yclass_object *)obj, 0, 0);
    assert(YETTY_IS_OK(mr));
    assert(mr.value == 1);
    assert(bd->motion_count == 1);

    /* on_press → inherited from probe_a. */
    struct yetty_ycore_int_result pr =
        yetty_ygui_widget_on_press(NULL, (struct yetty_yclass_object *)obj, 0, 0, 0);
    assert(YETTY_IS_OK(pr));
    assert(pr.value == 1);
    struct probe_a_data *ad = yetty_yclass_object_data(obj, probe_a_class_get()).value;
    assert(ad->press_count == 1);

    yetty_ygui_widget_destroy(obj);
}

static void test_tooltip_pilot(void)
{
    const struct yetty_yclass *cls = yetty_ygui_tooltip_class_get().value;
    assert(cls != NULL);

    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_new(cls);
    assert(YETTY_IS_OK(r));
    struct yetty_yclass_object *obj = r.value;

    /* Default label is NULL. */
    struct yetty_ycore_const_char_ptr_result text_default = yetty_ygui_tooltip_get_text(obj);
    assert(YETTY_IS_OK(text_default));
    assert(text_default.value == NULL);

    struct yetty_ycore_void_result sr = yetty_ygui_tooltip_set_text(obj, "hello");
    assert(YETTY_IS_OK(sr));
    struct yetty_ycore_const_char_ptr_result text_after = yetty_ygui_tooltip_get_text(obj);
    assert(YETTY_IS_OK(text_after));
    assert(strcmp(text_after.value, "hello") == 0);
    assert(yetty_ygui_widget_is_dirty(obj));

    /* Position the widget so paint emits a TEXT_DRAWABLE_LIST at a known coord. */
    struct yetty_ycore_rectangle wr = {{10.0f, 20.0f}, {200.0f, 60.0f}};
    yetty_ygui_widget_set_rect(obj, wr);

    /* Drive a minimal pass-2 emit: set up an ad-hoc drawable_list, call
     * paint, and verify a TEXT_DRAWABLE_LIST drawable-list entry record landed. */
    struct yetty_ydraw_drawable_list_result dlr =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    assert(YETTY_IS_OK(dlr));
    struct yetty_ygui_emit_ctx ctx = {
        .framework = NULL,
        .container_records = NULL,
        .ygrid_drawable_list = dlr.value,
        .figure_bodies = NULL,
        .current_figure_id = 0,
    };
    struct yetty_ycore_void_result er =
        yetty_ygui_widget_paint(NULL, (struct yetty_yclass_object *)obj, &ctx);
    assert(YETTY_IS_OK(er));
    /* First u32 in the primitives buffer is the type word —
     * YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST = 0x40000002 (no id flag, anonymous). */
    const uint32_t *prims = (const uint32_t *)yetty_ydraw_drawable_list_data(dlr.value);
    size_t prims_size = yetty_ydraw_drawable_list_size(dlr.value);
    assert(prims_size >= 8);
    assert(prims[0] == 0x40000002u);
    yetty_ydraw_drawable_list_destroy(dlr.value);
    yetty_ygui_widget_destroy(obj);
}

int main(void)
{
    test_class_registration();
    test_instance_alloc_and_dispatch();
    test_subclass_dispatch_and_super();
    test_tooltip_pilot();
    puts("ygui-object-test: OK");
    return 0;
}
