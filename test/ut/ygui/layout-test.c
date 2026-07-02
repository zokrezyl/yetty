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

int main(void)
{
    struct ytest test = ytest_begin("ygui_layout");
    YTEST_RUN(&test, test_hbox_two_children);
    YTEST_RUN(&test, test_vbox_flex_grow);
    YTEST_RUN(&test, test_padding);
    YTEST_RUN(&test, test_clickable_state_machine);
    YTEST_RUN(&test, test_widget_paint_emits_real_prims);
    return ytest_end(&test);
}
