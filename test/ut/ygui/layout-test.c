/*
 * ygui-layout-test.c — Phase 5/7/8 sanity tests.
 *
 * Exercises:
 *   - Layout pass for hbox (row direction) with two children
 *   - Layout pass for vbox (column direction) with three children
 *   - flex_grow distribution of free space
 *   - Padding shrinks the content box
 *   - Clickable mixin: press → release → on_click fires, with state
 *   - Button widget composed on top of clickable
 *   - Label widget data slice access
 *   - Panel widget background defaults
 *
 * Assertions use the shared ytest.h harness so the checks stay live under
 * Release/NDEBUG.
 */

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ydraw-core/drawable-list-registry.h>
#include <yetty/ydraw-core/text-drawable-list.h>
#include <yetty/ydraw/drawable-list-registry.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ysdf/types.gen.h>

#include "ytest.h"

#include <stdint.h>
#include <string.h>

/* Layout geometry rounds to the nearest pixel; half a pixel of slack. */
#define LAYOUT_TOL 0.5f

static struct yetty_ycore_rectangle rect(float minx, float miny, float maxx, float maxy)
{
    struct yetty_ycore_rectangle r = {{minx, miny}, {maxx, maxy}};
    return r;
}

static void test_hbox_two_children(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_new(yetty_ygui_hbox_class_get().value);
    YTEST_REQUIRE_OK(test, r);
    struct yetty_yclass_object *root = r.value;

    struct yetty_yclass_object_ptr_result rc1 =
        yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
    YTEST_REQUIRE_OK(test, rc1);
    struct yetty_yclass_object_ptr_result rc2 =
        yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
    YTEST_REQUIRE_OK(test, rc2);

    /* Set widths via layout. */
    struct yetty_ygui_layout_const_ptr_result layout1_res = yetty_ygui_widget_layout_get(rc1.value);
    YTEST_REQUIRE_OK(test, layout1_res);
    struct yetty_ygui_layout l1 = *layout1_res.value;
    l1.width = 30.0f;
    l1.height = 20.0f;
    yetty_ygui_widget_layout_set(rc1.value, &l1);

    struct yetty_ygui_layout_const_ptr_result layout2_res = yetty_ygui_widget_layout_get(rc2.value);
    YTEST_REQUIRE_OK(test, layout2_res);
    struct yetty_ygui_layout l2 = *layout2_res.value;
    l2.width = 50.0f;
    l2.height = 20.0f;
    yetty_ygui_widget_layout_set(rc2.value, &l2);

    struct yetty_ygui_layout_const_ptr_result root_layout_res = yetty_ygui_widget_layout_get(root);
    YTEST_REQUIRE_OK(test, root_layout_res);
    struct yetty_ygui_layout lp = *root_layout_res.value;
    lp.gap = 5.0f;
    yetty_ygui_widget_layout_set(root, &lp);

    struct yetty_ycore_void_result lr = yetty_ygui_layout_compute(root, rect(0, 0, 200, 100));
    YTEST_REQUIRE_OK(test, lr);

    /* Children appear in insertion order (yetty_ygui_add appends at
     * tail). rc1 first, then rc2. */
    struct yetty_yclass_object_ptr_result first_res = yetty_ygui_widget_first_child(root);
    YTEST_REQUIRE_OK(test, first_res);
    struct yetty_yclass_object *first = first_res.value;
    struct yetty_yclass_object_ptr_result second_res = yetty_ygui_widget_next_sibling(first);
    YTEST_REQUIRE_OK(test, second_res);
    struct yetty_yclass_object *second = second_res.value;
    YTEST_CHECK(test, first == rc1.value);
    YTEST_CHECK(test, second == rc2.value);

    struct yetty_ycore_rectangle_result r1_res = yetty_ygui_widget_rect(first);
    YTEST_REQUIRE_OK(test, r1_res);
    struct yetty_ycore_rectangle r1 = r1_res.value;
    struct yetty_ycore_rectangle_result r2_res = yetty_ygui_widget_rect(second);
    YTEST_REQUIRE_OK(test, r2_res);
    struct yetty_ycore_rectangle r2 = r2_res.value;
    /* rc1 (width 30) at x=0, then gap=5, then rc2 (width 50) at x=35. */
    YTEST_CHECK_NEAR(test, r1.min.x, 0.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, r1.max.x, 30.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, r2.min.x, 35.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, r2.max.x, 85.0f, LAYOUT_TOL);

    yetty_ygui_widget_destroy(root);
}

static void test_vbox_flex_grow(struct ytest *test)
{
    /* vbox 200 tall, three children. First has flex_grow=1, others=0
     * with explicit heights. Free space should land in the grow child. */
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_new(yetty_ygui_vbox_class_get().value);
    YTEST_REQUIRE_OK(test, r);
    struct yetty_yclass_object *root = r.value;

    /* Append at tail: insertion order == layout order (c1, c2, c3). */
    struct yetty_yclass_object *c1 =
        yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value).value;
    struct yetty_yclass_object *c2 =
        yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value).value;
    struct yetty_yclass_object *c3 =
        yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value).value;

    struct yetty_ygui_layout_const_ptr_result layout_c1_res = yetty_ygui_widget_layout_get(c1);
    YTEST_REQUIRE_OK(test, layout_c1_res);
    struct yetty_ygui_layout l = *layout_c1_res.value;
    l.height = 0.0f;
    l.flex_grow = 1.0f;
    l.width = 100.0f;
    yetty_ygui_widget_layout_set(c1, &l);

    struct yetty_ygui_layout_const_ptr_result layout_c2_res = yetty_ygui_widget_layout_get(c2);
    YTEST_REQUIRE_OK(test, layout_c2_res);
    struct yetty_ygui_layout l2 = *layout_c2_res.value;
    l2.height = 40.0f;
    l2.width = 100.0f;
    yetty_ygui_widget_layout_set(c2, &l2);

    struct yetty_ygui_layout_const_ptr_result layout_c3_res = yetty_ygui_widget_layout_get(c3);
    YTEST_REQUIRE_OK(test, layout_c3_res);
    struct yetty_ygui_layout l3 = *layout_c3_res.value;
    l3.height = 60.0f;
    l3.width = 100.0f;
    yetty_ygui_widget_layout_set(c3, &l3);

    struct yetty_ycore_void_result lr = yetty_ygui_layout_compute(root, rect(0, 0, 200, 200));
    YTEST_REQUIRE_OK(test, lr);

    struct yetty_ycore_rectangle_result rc1_res = yetty_ygui_widget_rect(c1);
    YTEST_REQUIRE_OK(test, rc1_res);
    struct yetty_ycore_rectangle rc1 = rc1_res.value;
    struct yetty_ycore_rectangle_result rc2_res = yetty_ygui_widget_rect(c2);
    YTEST_REQUIRE_OK(test, rc2_res);
    struct yetty_ycore_rectangle rc2 = rc2_res.value;
    struct yetty_ycore_rectangle_result rc3_res = yetty_ygui_widget_rect(c3);
    YTEST_REQUIRE_OK(test, rc3_res);
    struct yetty_ycore_rectangle rc3 = rc3_res.value;

    /* c1 absorbs the free space: 200 - (40 + 60) = 100. */
    YTEST_CHECK_NEAR(test, rc1.max.y - rc1.min.y, 100.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, rc2.max.y - rc2.min.y, 40.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, rc3.max.y - rc3.min.y, 60.0f, LAYOUT_TOL);
    /* Stacked top-to-bottom in insertion order. */
    YTEST_CHECK_NEAR(test, rc1.min.y, 0.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, rc2.min.y, 100.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, rc3.min.y, 140.0f, LAYOUT_TOL);

    yetty_ygui_widget_destroy(root);
}

static void test_padding(struct ytest *test)
{
    struct yetty_yclass_object *root =
        yetty_ygui_widget_new(yetty_ygui_hbox_class_get().value).value;
    struct yetty_yclass_object *c =
        yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value).value;

    struct yetty_ygui_layout_const_ptr_result root_layout_res = yetty_ygui_widget_layout_get(root);
    YTEST_REQUIRE_OK(test, root_layout_res);
    struct yetty_ygui_layout lp = *root_layout_res.value;
    lp.padding_left = 10;
    lp.padding_top = 20;
    lp.padding_right = 30;
    lp.padding_bottom = 40;
    /* Stretch child cross-axis so we can verify content_h. */
    lp.align = YETTY_YGUI_ALIGN_STRETCH;
    yetty_ygui_widget_layout_set(root, &lp);

    struct yetty_ygui_layout_const_ptr_result child_layout_res = yetty_ygui_widget_layout_get(c);
    YTEST_REQUIRE_OK(test, child_layout_res);
    struct yetty_ygui_layout lc = *child_layout_res.value;
    lc.width = 50;
    yetty_ygui_widget_layout_set(c, &lc);

    yetty_ygui_layout_compute(root, rect(0, 0, 200, 200));

    struct yetty_ycore_rectangle_result cr_res = yetty_ygui_widget_rect(c);
    YTEST_REQUIRE_OK(test, cr_res);
    struct yetty_ycore_rectangle cr = cr_res.value;
    /* content origin: (10, 20). Child at (10, 20) width 50, height stretched
     * to content_h = 200 - 20 - 40 = 140. */
    YTEST_CHECK_NEAR(test, cr.min.x, 10.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, cr.min.y, 20.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, cr.max.x, 60.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, cr.max.y, 160.0f, LAYOUT_TOL);

    yetty_ygui_widget_destroy(root);
}

/* Click callback: bumps the counter handed through userdata. */
static struct yetty_ycore_void_result on_click_cb(struct yetty_yclass_object *_yc_obj,
                                                  void *userdata)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)_yc_obj;
    (void)obj;
    int *counter = (int *)userdata;
    (*counter)++;
    return YETTY_OK_VOID();
}

static void test_clickable_state_machine(struct ytest *test)
{
    struct yetty_yclass_object *btn =
        yetty_ygui_widget_new(yetty_ygui_button_class_get().value).value;
    YTEST_REQUIRE_NOT_NULL(test, btn);
    yetty_ygui_button_set_label(btn, "OK");
    struct yetty_ycore_const_char_ptr_result label = yetty_ygui_button_get_label(btn);
    YTEST_REQUIRE_OK(test, label);
    YTEST_CHECK_STR_EQ(test, label.value, "OK");

    int click_fired = 0;
    struct yetty_ycore_void_result sr =
        yetty_ygui_clickable_on_click_set(btn, on_click_cb, &click_fired);
    YTEST_REQUIRE_OK(test, sr);

    struct yetty_ycore_int_result pressed_before = yetty_ygui_clickable_is_pressed(btn);
    YTEST_REQUIRE_OK(test, pressed_before);
    YTEST_CHECK(test, !pressed_before.value);
    struct yetty_ycore_int_result pr =
        yetty_ygui_widget_on_press((struct yetty_yclass_object *)btn, 1, 1, 0);
    YTEST_REQUIRE_OK(test, pr);
    YTEST_CHECK_EQ_INT(test, pr.value, 1);
    struct yetty_ycore_int_result pressed_after = yetty_ygui_clickable_is_pressed(btn);
    YTEST_REQUIRE_OK(test, pressed_after);
    YTEST_CHECK(test, pressed_after.value);
    YTEST_CHECK_EQ_INT(test, click_fired, 0); /* not yet — press alone doesn't fire */

    struct yetty_ycore_int_result rr =
        yetty_ygui_widget_on_release((struct yetty_yclass_object *)btn, 1, 1, 0);
    YTEST_REQUIRE_OK(test, rr);
    YTEST_CHECK_EQ_INT(test, rr.value, 1);
    struct yetty_ycore_int_result pressed_released = yetty_ygui_clickable_is_pressed(btn);
    YTEST_REQUIRE_OK(test, pressed_released);
    YTEST_CHECK(test, !pressed_released.value);
    YTEST_CHECK_EQ_INT(test, click_fired, 1);

    /* Release without prior press → no fire. */
    struct yetty_ycore_int_result rr2 =
        yetty_ygui_widget_on_release((struct yetty_yclass_object *)btn, 1, 1, 0);
    YTEST_REQUIRE_OK(test, rr2);
    YTEST_CHECK_EQ_INT(test, click_fired, 1);

    yetty_ygui_widget_destroy(btn);
}

static void test_widget_paint_emits_real_prims(struct ytest *test)
{
    /* Build a tree: panel (10,10)-(110,110) containing a label and a
     * button. Drive paint and verify real SDF prims + a TEXT_DRAWABLE_LIST
     * land in the ydraw drawable_list. */
    struct yetty_yclass_object *panel =
        yetty_ygui_widget_new(yetty_ygui_panel_class_get().value).value;
    struct yetty_yclass_object *label =
        yetty_ygui_widget_add(panel, yetty_ygui_label_class_get().value).value;
    struct yetty_yclass_object *btn =
        yetty_ygui_widget_add(panel, yetty_ygui_button_class_get().value).value;
    yetty_ygui_label_set_text(label, "hi");
    yetty_ygui_button_set_label(btn, "go");

    /* Size the widgets so paint produces non-empty geometry. */
    yetty_ygui_widget_set_rect(panel, rect(10, 10, 110, 110));
    yetty_ygui_widget_set_rect(label, rect(20, 20, 100, 40));
    yetty_ygui_widget_set_rect(btn, rect(20, 60, 100, 90));

    struct yetty_ydraw_drawable_list_result dlr =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, dlr);
    struct yetty_ygui_emit_ctx ctx = {
        .framework = NULL,
        .ygrid_drawable_list = dlr.value,
        .current_figure_id = 0,
    };
    yetty_ygui_widget_paint((struct yetty_yclass_object *)panel, &ctx);
    yetty_ygui_widget_paint((struct yetty_yclass_object *)label, &ctx);
    yetty_ygui_widget_paint((struct yetty_yclass_object *)btn, &ctx);

    /* Walk the emitted prims with the drawable-list registry iterator — the
     * drift-proof way, no hardcoded per-prim word counts. Confirm the panel
     * background box, the button's rounded surface primitive, and the two
     * TEXT_DRAWABLE_LIST entries (label text + button label) all landed.
     *
     * The button surface is a rounded box: an unpressed button paints a
     * rounded LINEAR_GRADIENT_BOX, a pressed one a flat ROUNDED_BOX. Accept
     * either so the check tracks the widget's intent, not its idle state. */
    struct yetty_ydraw_drawable_list_registry_ptr_result reg_res =
        yetty_ydraw_drawable_list_registry_create_default();
    YTEST_REQUIRE_OK(test, reg_res);
    struct yetty_ydraw_drawable_list_registry *reg = reg_res.value;

    int saw_box = 0, saw_button_surface = 0, saw_text = 0;
    struct yetty_ydraw_drawable_iter_result iter_res =
        yetty_ydraw_drawable_list_drawable_first(dlr.value, reg);
    YTEST_REQUIRE_OK(test, iter_res);
    struct yetty_ydraw_drawable_iter iter = iter_res.value;
    for (;;) {
        uint32_t type = iter.fw.data[0];
        if (type == YETTY_YSDF_BOX) {
            saw_box = 1;
        } else if (type == YETTY_YSDF_ROUNDED_BOX || type == YETTY_YSDF_LINEAR_GRADIENT_BOX) {
            saw_button_surface = 1;
        } else if (type == YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST) {
            saw_text++;
        }
        struct yetty_ydraw_drawable_iter_result next_res =
            yetty_ydraw_drawable_list_drawable_next(dlr.value, reg, &iter);
        if (YETTY_IS_ERR(next_res)) {
            /* End of buffer is signalled as an error with no cause chain. */
            yetty_ycore_error_destroy(next_res.error);
            break;
        }
        iter = next_res.value;
    }
    YTEST_CHECK(test, saw_box);
    YTEST_CHECK(test, saw_button_surface);
    YTEST_CHECK(test, saw_text >= 2);

    yetty_ydraw_drawable_list_registry_destroy(reg);
    yetty_ydraw_drawable_list_destroy(dlr.value);
    yetty_ygui_widget_destroy(panel);
}

/* Shrinking with a min clamp must REDISTRIBUTE the deficit a frozen child can't
 * absorb to its still-flexible siblings, not strand it (the pre-fix behavior
 * left independent per-child clamps that overflowed the container). */
static void test_flex_shrink_min_clamp_redistributes(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_new(yetty_ygui_hbox_class_get().value);
    YTEST_REQUIRE_OK(test, r);
    struct yetty_yclass_object *root = r.value;

    struct yetty_yclass_object_ptr_result ra =
        yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
    YTEST_REQUIRE_OK(test, ra);
    struct yetty_yclass_object_ptr_result rb =
        yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
    YTEST_REQUIRE_OK(test, rb);

    struct yetty_ygui_layout_const_ptr_result la_res = yetty_ygui_widget_layout_get(ra.value);
    YTEST_REQUIRE_OK(test, la_res);
    struct yetty_ygui_layout la = *la_res.value;
    la.width = 80.0f;
    la.height = 20.0f;
    la.flex_shrink = 1.0f;
    la.min_width = 60.0f; /* A cannot shrink below 60 */
    yetty_ygui_widget_layout_set(ra.value, &la);

    struct yetty_ygui_layout_const_ptr_result lb_res = yetty_ygui_widget_layout_get(rb.value);
    YTEST_REQUIRE_OK(test, lb_res);
    struct yetty_ygui_layout lb = *lb_res.value;
    lb.width = 80.0f;
    lb.height = 20.0f;
    lb.flex_shrink = 1.0f;
    yetty_ygui_widget_layout_set(rb.value, &lb);

    struct yetty_ygui_layout_const_ptr_result lp_res = yetty_ygui_widget_layout_get(root);
    YTEST_REQUIRE_OK(test, lp_res);
    struct yetty_ygui_layout lp = *lp_res.value;
    lp.direction = YETTY_YGUI_FLEX_ROW;
    lp.gap = 0.0f;
    lp.padding_left = lp.padding_right = lp.padding_top = lp.padding_bottom = 0.0f;
    yetty_ygui_widget_layout_set(root, &lp);

    /* 100 wide, total base 160 → 60 overflow. A floors at 60 and freezes; B
     * absorbs the whole 40 remaining (60 + 40 = 100 fits exactly). */
    struct yetty_ycore_void_result lr = yetty_ygui_layout_compute(root, rect(0, 0, 100, 50));
    YTEST_REQUIRE_OK(test, lr);

    struct yetty_ycore_rectangle_result ar = yetty_ygui_widget_rect(ra.value);
    YTEST_REQUIRE_OK(test, ar);
    struct yetty_ycore_rectangle_result br = yetty_ygui_widget_rect(rb.value);
    YTEST_REQUIRE_OK(test, br);
    YTEST_CHECK_NEAR(test, ar.value.max.x - ar.value.min.x, 60.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, br.value.max.x - br.value.min.x, 40.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, br.value.min.x, 60.0f, LAYOUT_TOL);

    yetty_ygui_widget_destroy(root);
}

/* An absolutely-positioned child is placed at (pos_x, pos_y) in the content box
 * at its own size and takes no part in flex flow — its in-flow siblings lay out
 * as if it were not there. */
static void test_absolute_positioning(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_new(yetty_ygui_vbox_class_get().value);
    YTEST_REQUIRE_OK(test, r);
    struct yetty_yclass_object *root = r.value;

    struct yetty_yclass_object_ptr_result rabs =
        yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
    YTEST_REQUIRE_OK(test, rabs);
    struct yetty_yclass_object_ptr_result rflow =
        yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
    YTEST_REQUIRE_OK(test, rflow);

    struct yetty_ygui_layout_const_ptr_result labs_res = yetty_ygui_widget_layout_get(rabs.value);
    YTEST_REQUIRE_OK(test, labs_res);
    struct yetty_ygui_layout labs = *labs_res.value;
    labs.absolute = 1;
    labs.pos_x = 10.0f;
    labs.pos_y = 20.0f;
    labs.width = 30.0f;
    labs.height = 40.0f;
    yetty_ygui_widget_layout_set(rabs.value, &labs);

    struct yetty_ygui_layout_const_ptr_result lflow_res = yetty_ygui_widget_layout_get(rflow.value);
    YTEST_REQUIRE_OK(test, lflow_res);
    struct yetty_ygui_layout lflow = *lflow_res.value;
    lflow.width = 50.0f;
    lflow.height = 25.0f;
    yetty_ygui_widget_layout_set(rflow.value, &lflow);

    struct yetty_ygui_layout_const_ptr_result lp_res = yetty_ygui_widget_layout_get(root);
    YTEST_REQUIRE_OK(test, lp_res);
    struct yetty_ygui_layout lp = *lp_res.value;
    lp.padding_left = lp.padding_right = lp.padding_top = lp.padding_bottom = 0.0f;
    yetty_ygui_widget_layout_set(root, &lp);

    struct yetty_ycore_void_result lr = yetty_ygui_layout_compute(root, rect(0, 0, 200, 200));
    YTEST_REQUIRE_OK(test, lr);

    struct yetty_ycore_rectangle_result ar = yetty_ygui_widget_rect(rabs.value);
    YTEST_REQUIRE_OK(test, ar);
    YTEST_CHECK_NEAR(test, ar.value.min.x, 10.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, ar.value.min.y, 20.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, ar.value.max.x - ar.value.min.x, 30.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, ar.value.max.y - ar.value.min.y, 40.0f, LAYOUT_TOL);

    /* The in-flow child starts at the content top (y == 0), unaffected by the
     * absolute sibling that precedes it in the child list. */
    struct yetty_ycore_rectangle_result fr = yetty_ygui_widget_rect(rflow.value);
    YTEST_REQUIRE_OK(test, fr);
    YTEST_CHECK_NEAR(test, fr.value.min.y, 0.0f, LAYOUT_TOL);

    yetty_ygui_widget_destroy(root);
}

/* A child that is BOTH absolute and hidden must be folded away entirely: the
 * layout pass must not place it (no new rect) and must not recurse into it.
 * Regression guard for the pass that once checked `absolute` before `hidden`
 * and so re-placed hidden absolute overlays. We seed a sentinel rect, mark the
 * child absolute+hidden with a pos that WOULD move it, run layout, and assert
 * the rect is untouched. */
static void test_hidden_absolute_not_placed(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_new(yetty_ygui_vbox_class_get().value);
    YTEST_REQUIRE_OK(test, r);
    struct yetty_yclass_object *root = r.value;

    struct yetty_yclass_object_ptr_result rhidden =
        yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
    YTEST_REQUIRE_OK(test, rhidden);

    /* Seed a distinctive sentinel rect the layout must leave alone. */
    struct yetty_ycore_rectangle sentinel = rect(3, 7, 33, 47);
    yetty_ygui_widget_set_rect(rhidden.value, sentinel);

    struct yetty_ygui_layout_const_ptr_result lh_res = yetty_ygui_widget_layout_get(rhidden.value);
    YTEST_REQUIRE_OK(test, lh_res);
    struct yetty_ygui_layout lh = *lh_res.value;
    lh.absolute = 1;
    lh.hidden = 1;
    /* A pos/size that differs from the sentinel — if the pass placed it, the
     * rect would become (100,120)-(150,180) and the asserts below would fail. */
    lh.pos_x = 100.0f;
    lh.pos_y = 120.0f;
    lh.width = 50.0f;
    lh.height = 60.0f;
    yetty_ygui_widget_layout_set(rhidden.value, &lh);

    struct yetty_ycore_void_result lr = yetty_ygui_layout_compute(root, rect(0, 0, 200, 200));
    YTEST_REQUIRE_OK(test, lr);

    struct yetty_ycore_rectangle_result hr = yetty_ygui_widget_rect(rhidden.value);
    YTEST_REQUIRE_OK(test, hr);
    /* Rect is exactly the sentinel: layout neither placed nor recursed. */
    YTEST_CHECK_NEAR(test, hr.value.min.x, sentinel.min.x, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, hr.value.min.y, sentinel.min.y, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, hr.value.max.x, sentinel.max.x, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, hr.value.max.y, sentinel.max.y, LAYOUT_TOL);

    yetty_ygui_widget_destroy(root);
}

/* wrap == WRAP breaks children onto a new line when they overflow the main
 * axis; the wrapped child advances on the cross axis by the previous line's
 * height. */
static void test_flex_wrap(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_new(yetty_ygui_hbox_class_get().value);
    YTEST_REQUIRE_OK(test, r);
    struct yetty_yclass_object *root = r.value;

    struct yetty_yclass_object *kids[3];
    for (int i = 0; i < 3; ++i) {
        struct yetty_yclass_object_ptr_result kr =
            yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
        YTEST_REQUIRE_OK(test, kr);
        kids[i] = kr.value;
        struct yetty_ygui_layout_const_ptr_result kl_res = yetty_ygui_widget_layout_get(kids[i]);
        YTEST_REQUIRE_OK(test, kl_res);
        struct yetty_ygui_layout kl = *kl_res.value;
        kl.width = 40.0f;
        kl.height = 20.0f;
        yetty_ygui_widget_layout_set(kids[i], &kl);
    }

    struct yetty_ygui_layout_const_ptr_result lp_res = yetty_ygui_widget_layout_get(root);
    YTEST_REQUIRE_OK(test, lp_res);
    struct yetty_ygui_layout lp = *lp_res.value;
    lp.direction = YETTY_YGUI_FLEX_ROW;
    lp.gap = 0.0f;
    lp.wrap = YETTY_YGUI_WRAP_WRAP;
    lp.padding_left = lp.padding_right = lp.padding_top = lp.padding_bottom = 0.0f;
    yetty_ygui_widget_layout_set(root, &lp);

    /* 100 wide: 40+40 fit on line 1, the third (120 total) wraps to line 2. */
    struct yetty_ycore_void_result lr = yetty_ygui_layout_compute(root, rect(0, 0, 100, 100));
    YTEST_REQUIRE_OK(test, lr);

    struct yetty_ycore_rectangle_result r0 = yetty_ygui_widget_rect(kids[0]);
    struct yetty_ycore_rectangle_result r1 = yetty_ygui_widget_rect(kids[1]);
    struct yetty_ycore_rectangle_result r2 = yetty_ygui_widget_rect(kids[2]);
    YTEST_REQUIRE_OK(test, r0);
    YTEST_REQUIRE_OK(test, r1);
    YTEST_REQUIRE_OK(test, r2);
    /* Line 1: kids 0 and 1 at y == 0, side by side. */
    YTEST_CHECK_NEAR(test, r0.value.min.y, 0.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, r1.value.min.y, 0.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, r1.value.min.x, 40.0f, LAYOUT_TOL);
    /* Line 2: kid 2 wrapped down one line (height 20) and back to x == 0. */
    YTEST_CHECK_NEAR(test, r2.value.min.x, 0.0f, LAYOUT_TOL);
    YTEST_CHECK_NEAR(test, r2.value.min.y, 20.0f, LAYOUT_TOL);

    yetty_ygui_widget_destroy(root);
}

/* align-self overrides the parent's cross alignment for one child, and a
 * per-side margin offsets the child's box on both axes. */
static void test_align_self_and_margin(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_new(yetty_ygui_hbox_class_get().value);
    YTEST_REQUIRE_OK(test, r);
    struct yetty_yclass_object *root = r.value;

    struct yetty_yclass_object_ptr_result ka =
        yetty_ygui_widget_add(root, yetty_ygui_label_class_get().value);
    YTEST_REQUIRE_OK(test, ka);

    struct yetty_ygui_layout_const_ptr_result la_res = yetty_ygui_widget_layout_get(ka.value);
    YTEST_REQUIRE_OK(test, la_res);
    struct yetty_ygui_layout la = *la_res.value;
    la.width = 40.0f;
    la.height = 30.0f;
    la.align_self = YETTY_YGUI_ALIGN_SELF_END; /* bottom in a row */
    la.margin_left = 15.0f;                    /* main-axis lead offset */
    yetty_ygui_widget_layout_set(ka.value, &la);

    struct yetty_ygui_layout_const_ptr_result lp_res = yetty_ygui_widget_layout_get(root);
    YTEST_REQUIRE_OK(test, lp_res);
    struct yetty_ygui_layout lp = *lp_res.value;
    lp.direction = YETTY_YGUI_FLEX_ROW;
    lp.align = YETTY_YGUI_ALIGN_START; /* parent default the child overrides */
    lp.gap = 0.0f;
    lp.padding_left = lp.padding_right = lp.padding_top = lp.padding_bottom = 0.0f;
    yetty_ygui_widget_layout_set(root, &lp);

    struct yetty_ycore_void_result lr = yetty_ygui_layout_compute(root, rect(0, 0, 200, 100));
    YTEST_REQUIRE_OK(test, lr);

    struct yetty_ycore_rectangle_result ar = yetty_ygui_widget_rect(ka.value);
    YTEST_REQUIRE_OK(test, ar);
    /* margin_left pushes the main-axis start to x == 15. */
    YTEST_CHECK_NEAR(test, ar.value.min.x, 15.0f, LAYOUT_TOL);
    /* align-self END puts the 30-tall box at the bottom of the 100 cross:
     * y == 100 - 30 == 70 (not 0, which the parent's START would give). */
    YTEST_CHECK_NEAR(test, ar.value.min.y, 70.0f, LAYOUT_TOL);

    yetty_ygui_widget_destroy(root);
}

int main(void)
{
    struct ytest test = ytest_begin("ygui_layout");
    YTEST_RUN(&test, test_hbox_two_children);
    YTEST_RUN(&test, test_vbox_flex_grow);
    YTEST_RUN(&test, test_padding);
    YTEST_RUN(&test, test_flex_shrink_min_clamp_redistributes);
    YTEST_RUN(&test, test_absolute_positioning);
    YTEST_RUN(&test, test_hidden_absolute_not_placed);
    YTEST_RUN(&test, test_flex_wrap);
    YTEST_RUN(&test, test_align_self_and_margin);
    YTEST_RUN(&test, test_clickable_state_machine);
    YTEST_RUN(&test, test_widget_paint_emits_real_prims);
    return ytest_end(&test);
}
