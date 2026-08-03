/*
 * ygui layout + event-state matrix test.
 *
 * Complements object-test.c (dispatch) and layout-test.c (hbox/vbox/flex_grow/
 * padding/clickable/paint) with the fuller matrix #406 asks for: justify and
 * align across all their enum values, gap+padding together, flex grow ratios
 * and flex shrink, min/max clamping, nested layouts, and the click-driven
 * state machines of the clickable mixin and the checkbox/toggle widgets
 * (asserting the transitions, not just the final value).
 *
 * Pure layout/event logic — no GPU/display/network.
 */

#include <yetty/ygui/ygui.h>
#include "yetty/gen/impl/ygui/widgets/checkbox.h"
#include "yetty/gen/impl/ygui/widgets/toggle.h"

#include "ytest.h"

#include <stdint.h>

#define TOL 0.5f

static struct yetty_ycore_rectangle rect(float minx, float miny, float maxx, float maxy)
{
    struct yetty_ycore_rectangle r = {{minx, miny}, {maxx, maxy}};
    return r;
}

static struct yetty_yclass_object *hbox(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_new(yetty_ygui_hbox_class_get().value);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

static struct yetty_yclass_object *add_label(struct ytest *test, struct yetty_yclass_object *parent,
                                             float w, float h)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_add(parent, yetty_ygui_label_class_get().value);
    YTEST_REQUIRE_OK(test, r);
    struct yetty_ygui_layout_const_ptr_result lay = yetty_ygui_widget_layout_get(r.value);
    YTEST_REQUIRE_OK(test, lay);
    struct yetty_ygui_layout l = *lay.value;
    l.width = w;
    l.height = h;
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_layout_set(r.value, &l));
    return r.value;
}

/* Overwrite the root's layout with a caller-tweaked copy. */
static void set_root_layout(struct ytest *test, struct yetty_yclass_object *root,
                            struct yetty_ygui_layout patched)
{
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_layout_set(root, &patched));
}

static struct yetty_ygui_layout layout_of(struct ytest *test, struct yetty_yclass_object *obj)
{
    struct yetty_ygui_layout_const_ptr_result lay = yetty_ygui_widget_layout_get(obj);
    YTEST_REQUIRE_OK(test, lay);
    return *lay.value;
}

static struct yetty_ycore_rectangle rect_of(struct ytest *test, struct yetty_yclass_object *obj)
{
    struct yetty_ycore_rectangle_result r = yetty_ygui_widget_rect(obj);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

/*---------------------------------------------------------------------------
 * Layout: justify (main axis) across all four values on a 300-wide hbox with
 * three 40-wide children (120 used, 180 free), gap 0.
 *-------------------------------------------------------------------------*/
static void test_justify(struct ytest *test)
{
    const struct {
        enum yetty_ygui_flex_justify justify;
        float x0, x1, x2;
    } cases[] = {
        {YETTY_YGUI_JUSTIFY_START, 0.0f, 40.0f, 80.0f},
        {YETTY_YGUI_JUSTIFY_END, 180.0f, 220.0f, 260.0f},
        {YETTY_YGUI_JUSTIFY_CENTER, 90.0f, 130.0f, 170.0f},
        {YETTY_YGUI_JUSTIFY_SPACE_BETWEEN, 0.0f, 130.0f, 260.0f},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        struct yetty_yclass_object *root = hbox(test);
        struct yetty_yclass_object *c0 = add_label(test, root, 40, 20);
        struct yetty_yclass_object *c1 = add_label(test, root, 40, 20);
        struct yetty_yclass_object *c2 = add_label(test, root, 40, 20);
        struct yetty_ygui_layout lp = layout_of(test, root);
        lp.gap = 0.0f;
        lp.justify = cases[i].justify;
        set_root_layout(test, root, lp);
        YTEST_REQUIRE_OK(test, yetty_ygui_layout_compute(root, rect(0, 0, 300, 100)));

        YTEST_CHECK_NEAR(test, rect_of(test, c0).min.x, cases[i].x0, TOL);
        YTEST_CHECK_NEAR(test, rect_of(test, c1).min.x, cases[i].x1, TOL);
        YTEST_CHECK_NEAR(test, rect_of(test, c2).min.x, cases[i].x2, TOL);
        yetty_ygui_widget_destroy(root);
    }
}

/*---------------------------------------------------------------------------
 * Layout: align (cross axis) across all four values. 300x100 hbox, one 40x20
 * child.
 *-------------------------------------------------------------------------*/
static void test_align(struct ytest *test)
{
    const struct {
        enum yetty_ygui_flex_align align;
        float child_h; /* input cross size (0 = auto, so STRETCH can grow it) */
        float min_y, height;
    } cases[] = {
        {YETTY_YGUI_ALIGN_START, 20.0f, 0.0f, 20.0f},
        {YETTY_YGUI_ALIGN_CENTER, 20.0f, 40.0f, 20.0f},
        {YETTY_YGUI_ALIGN_END, 20.0f, 80.0f, 20.0f},
        /* STRETCH only stretches an item with no explicit cross size. */
        {YETTY_YGUI_ALIGN_STRETCH, 0.0f, 0.0f, 100.0f},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        struct yetty_yclass_object *root = hbox(test);
        struct yetty_yclass_object *child = add_label(test, root, 40, cases[i].child_h);
        struct yetty_ygui_layout lp = layout_of(test, root);
        lp.align = cases[i].align;
        set_root_layout(test, root, lp);
        YTEST_REQUIRE_OK(test, yetty_ygui_layout_compute(root, rect(0, 0, 300, 100)));

        struct yetty_ycore_rectangle cr = rect_of(test, child);
        YTEST_CHECK_NEAR(test, cr.min.y, cases[i].min_y, TOL);
        YTEST_CHECK_NEAR(test, cr.max.y - cr.min.y, cases[i].height, TOL);
        yetty_ygui_widget_destroy(root);
    }
}

/*---------------------------------------------------------------------------
 * Layout: gap + padding together. hbox padding-left 10, gap 5, two 30-wide
 * children → first at x=10, second at x=10+30+5=45.
 *-------------------------------------------------------------------------*/
static void test_gap_and_padding(struct ytest *test)
{
    struct yetty_yclass_object *root = hbox(test);
    struct yetty_yclass_object *c0 = add_label(test, root, 30, 20);
    struct yetty_yclass_object *c1 = add_label(test, root, 30, 20);
    struct yetty_ygui_layout lp = layout_of(test, root);
    lp.padding_left = 10;
    lp.gap = 5;
    set_root_layout(test, root, lp);
    YTEST_REQUIRE_OK(test, yetty_ygui_layout_compute(root, rect(0, 0, 300, 100)));

    YTEST_CHECK_NEAR(test, rect_of(test, c0).min.x, 10.0f, TOL);
    YTEST_CHECK_NEAR(test, rect_of(test, c1).min.x, 45.0f, TOL);
    yetty_ygui_widget_destroy(root);
}

/*---------------------------------------------------------------------------
 * Layout: flex_grow ratio. hbox 300 wide, two base-0 children grow 1 : 2 →
 * widths 100 and 200.
 *-------------------------------------------------------------------------*/
static void test_flex_grow_ratio(struct ytest *test)
{
    struct yetty_yclass_object *root = hbox(test);
    struct yetty_yclass_object *c0 = add_label(test, root, 0, 20);
    struct yetty_yclass_object *c1 = add_label(test, root, 0, 20);
    struct yetty_ygui_layout l0 = layout_of(test, c0);
    l0.flex_grow = 1.0f;
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_layout_set(c0, &l0));
    struct yetty_ygui_layout l1 = layout_of(test, c1);
    l1.flex_grow = 2.0f;
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_layout_set(c1, &l1));
    struct yetty_ygui_layout lp = layout_of(test, root);
    lp.gap = 0.0f;
    set_root_layout(test, root, lp);
    YTEST_REQUIRE_OK(test, yetty_ygui_layout_compute(root, rect(0, 0, 300, 100)));

    struct yetty_ycore_rectangle r0 = rect_of(test, c0);
    struct yetty_ycore_rectangle r1 = rect_of(test, c1);
    YTEST_CHECK_NEAR(test, r0.max.x - r0.min.x, 100.0f, TOL);
    YTEST_CHECK_NEAR(test, r1.max.x - r1.min.x, 200.0f, TOL);
    YTEST_CHECK_NEAR(test, r1.min.x, 100.0f, TOL);
    yetty_ygui_widget_destroy(root);
}

/*---------------------------------------------------------------------------
 * Layout: flex_shrink. hbox 100 wide, two 80-wide children (60 overflow) with
 * shrink 1 each → 30 shrunk from each → 50 wide each.
 *-------------------------------------------------------------------------*/
static void test_flex_shrink(struct ytest *test)
{
    struct yetty_yclass_object *root = hbox(test);
    struct yetty_yclass_object *c0 = add_label(test, root, 80, 20);
    struct yetty_yclass_object *c1 = add_label(test, root, 80, 20);
    struct yetty_ygui_layout l0 = layout_of(test, c0);
    l0.flex_shrink = 1.0f;
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_layout_set(c0, &l0));
    struct yetty_ygui_layout l1 = layout_of(test, c1);
    l1.flex_shrink = 1.0f;
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_layout_set(c1, &l1));
    struct yetty_ygui_layout lp = layout_of(test, root);
    lp.gap = 0.0f;
    set_root_layout(test, root, lp);
    YTEST_REQUIRE_OK(test, yetty_ygui_layout_compute(root, rect(0, 0, 100, 100)));

    struct yetty_ycore_rectangle r0 = rect_of(test, c0);
    struct yetty_ycore_rectangle r1 = rect_of(test, c1);
    YTEST_CHECK_NEAR(test, r0.max.x - r0.min.x, 50.0f, TOL);
    YTEST_CHECK_NEAR(test, r1.max.x - r1.min.x, 50.0f, TOL);
    yetty_ygui_widget_destroy(root);
}

/*---------------------------------------------------------------------------
 * Layout: max_width clamps a grow child.
 *-------------------------------------------------------------------------*/
static void test_max_width_clamp(struct ytest *test)
{
    struct yetty_yclass_object *root = hbox(test);
    struct yetty_yclass_object *child = add_label(test, root, 0, 20);
    struct yetty_ygui_layout l = layout_of(test, child);
    l.flex_grow = 1.0f;  /* would take all 300 */
    l.max_width = 50.0f; /* but capped here */
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_layout_set(child, &l));
    YTEST_REQUIRE_OK(test, yetty_ygui_layout_compute(root, rect(0, 0, 300, 100)));

    struct yetty_ycore_rectangle cr = rect_of(test, child);
    YTEST_CHECK_NEAR(test, cr.max.x - cr.min.x, 50.0f, TOL);
    yetty_ygui_widget_destroy(root);
}

/*---------------------------------------------------------------------------
 * Layout: nested. hbox → vbox(width 100) → two 30-tall labels stacked.
 *-------------------------------------------------------------------------*/
static void test_nested(struct ytest *test)
{
    struct yetty_yclass_object *root = hbox(test);
    struct yetty_yclass_object_ptr_result vb =
        yetty_ygui_widget_add(root, yetty_ygui_vbox_class_get().value);
    YTEST_REQUIRE_OK(test, vb);
    struct yetty_yclass_object *col = vb.value;
    struct yetty_ygui_layout cl = layout_of(test, col);
    cl.width = 100;
    cl.height = 100;
    cl.gap = 0.0f;
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_layout_set(col, &cl));
    struct yetty_yclass_object *a = add_label(test, col, 100, 30);
    struct yetty_yclass_object *b = add_label(test, col, 100, 30);

    struct yetty_ygui_layout lp = layout_of(test, root);
    lp.gap = 0.0f;
    set_root_layout(test, root, lp);
    YTEST_REQUIRE_OK(test, yetty_ygui_layout_compute(root, rect(0, 0, 300, 100)));

    /* Column at origin; its two children stack vertically. */
    YTEST_CHECK_NEAR(test, rect_of(test, col).min.x, 0.0f, TOL);
    YTEST_CHECK_NEAR(test, rect_of(test, a).min.y, 0.0f, TOL);
    YTEST_CHECK_NEAR(test, rect_of(test, b).min.y, 30.0f, TOL);
    yetty_ygui_widget_destroy(root);
}

/*---------------------------------------------------------------------------
 * Events: clickable state machine transitions.
 *-------------------------------------------------------------------------*/
static struct yetty_ycore_void_result count_click(struct yetty_yclass_object *obj, void *ud)
{
    (void)obj;
    (*(int *)ud)++;
    return YETTY_OK_VOID();
}

static int is_pressed(struct ytest *test, struct yetty_yclass_object *btn)
{
    struct yetty_ycore_int_result r = yetty_ygui_clickable_is_pressed(btn);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

static void test_clickable_transitions(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_new(yetty_ygui_button_class_get().value);
    YTEST_REQUIRE_OK(test, r);
    struct yetty_yclass_object *btn = r.value;
    int clicks = 0;
    YTEST_REQUIRE_OK(test, yetty_ygui_clickable_on_click_set(btn, count_click, &clicks));

    /* idle → pressed */
    YTEST_CHECK(test, !is_pressed(test, btn));
    struct yetty_ycore_int_result p1 = yetty_ygui_widget_on_press(btn, 1, 1, 0);
    YTEST_REQUIRE_OK(test, p1);
    YTEST_CHECK(test, is_pressed(test, btn));
    YTEST_CHECK_EQ_INT(test, clicks, 0); /* press alone does not click */

    /* pressed → released fires exactly one click */
    struct yetty_ycore_int_result rel1 = yetty_ygui_widget_on_release(btn, 1, 1, 0);
    YTEST_REQUIRE_OK(test, rel1);
    YTEST_CHECK(test, !is_pressed(test, btn));
    YTEST_CHECK_EQ_INT(test, clicks, 1);

    /* release with no prior press → no extra click, stays idle */
    struct yetty_ycore_int_result rel2 = yetty_ygui_widget_on_release(btn, 1, 1, 0);
    YTEST_REQUIRE_OK(test, rel2);
    YTEST_CHECK_EQ_INT(test, clicks, 1);
    YTEST_CHECK(test, !is_pressed(test, btn));

    yetty_ygui_widget_destroy(btn);
}

/*---------------------------------------------------------------------------
 * Events: checkbox toggles its checked state through the press→release path
 * (the clickable mixin fires on_release → the checkbox's on_click_toggle).
 *-------------------------------------------------------------------------*/
static int checked(struct ytest *test, struct yetty_yclass_object *cb)
{
    struct yetty_ycore_int_result r = yetty_ygui_checkbox_get_checked(cb);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

static void click(struct ytest *test, struct yetty_yclass_object *w)
{
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_press(w, 1, 1, 0));
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_release(w, 1, 1, 0));
}

static void test_checkbox_toggle_via_events(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_new(yetty_ygui_checkbox_class_get().value);
    YTEST_REQUIRE_OK(test, r);
    struct yetty_yclass_object *cb = r.value;

    YTEST_CHECK_EQ_INT(test, checked(test, cb), 0); /* default unchecked */
    click(test, cb);
    YTEST_CHECK_EQ_INT(test, checked(test, cb), 1); /* click → checked */
    click(test, cb);
    YTEST_CHECK_EQ_INT(test, checked(test, cb), 0); /* click → unchecked */

    /* Direct setter still works and is independent of the event path. */
    YTEST_REQUIRE_OK(test, yetty_ygui_checkbox_set_checked(cb, 1));
    YTEST_CHECK_EQ_INT(test, checked(test, cb), 1);

    yetty_ygui_widget_destroy(cb);
}

/*---------------------------------------------------------------------------
 * Events: toggle widget on/off state via setter + getter.
 *-------------------------------------------------------------------------*/
static void test_toggle_state(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_new(yetty_ygui_toggle_class_get().value);
    YTEST_REQUIRE_OK(test, r);
    struct yetty_yclass_object *tg = r.value;

    struct yetty_ycore_int_result on0 = yetty_ygui_toggle_get_on(tg);
    YTEST_REQUIRE_OK(test, on0);
    YTEST_CHECK_EQ_INT(test, on0.value, 0);

    YTEST_REQUIRE_OK(test, yetty_ygui_toggle_set_on(tg, 1));
    struct yetty_ycore_int_result on1 = yetty_ygui_toggle_get_on(tg);
    YTEST_REQUIRE_OK(test, on1);
    YTEST_CHECK_EQ_INT(test, on1.value, 1);

    yetty_ygui_widget_destroy(tg);
}

int main(void)
{
    struct ytest test = ytest_begin("ygui_matrix");
    YTEST_RUN(&test, test_justify);
    YTEST_RUN(&test, test_align);
    YTEST_RUN(&test, test_gap_and_padding);
    YTEST_RUN(&test, test_flex_grow_ratio);
    YTEST_RUN(&test, test_flex_shrink);
    YTEST_RUN(&test, test_max_width_clamp);
    YTEST_RUN(&test, test_nested);
    YTEST_RUN(&test, test_clickable_transitions);
    YTEST_RUN(&test, test_checkbox_toggle_via_events);
    YTEST_RUN(&test, test_toggle_state);
    return ytest_end(&test);
}
