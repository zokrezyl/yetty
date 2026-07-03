/*
 * ygui widget behavior + mixin composition test (#420).
 *
 * Complements the layout matrix (matrix-test.c: justify/align/flex + the
 * clickable/checkbox/toggle state machines) with the widget BEHAVIOUR and
 * MIXIN COMPOSITION those don't touch:
 *   - slider: press x maps monotonically to value; set/get round-trip,
 *   - draggable mixin (via scrollarea): press→dragging, motion fires the drag
 *     callback with a delta, release→not dragging,
 *   - clickable mixin reused by non-button widgets: radio click selects,
 *     selectable click toggles,
 *   - tabbar tab model: add/count/active/remove.
 *
 * Pure event/state logic — no GPU/display/network.
 */

#include <yetty/ygui/mixins/draggable.h>
#include <yetty/ygui/widgets/radio.h>
#include <yetty/ygui/widgets/scrollarea.h>
#include <yetty/ygui/widgets/selectable.h>
#include <yetty/ygui/widgets/slider.h>
#include <yetty/ygui/widgets/tabbar.h>
#include <yetty/ygui/ygui.h>

#include "ytest.h"

#include <stdint.h>

static struct yetty_yclass_object *make(struct ytest *test, struct yetty_yclass_ptr_result cls)
{
    YTEST_REQUIRE_OK(test, cls);
    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_new(cls.value);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

static void press(struct ytest *test, struct yetty_yclass_object *w, float x, float y)
{
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_press(w, x, y, 0));
}
static void release(struct ytest *test, struct yetty_yclass_object *w, float x, float y)
{
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_release(w, x, y, 0));
}
static void motion(struct ytest *test, struct yetty_yclass_object *w, float x, float y)
{
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_motion(w, x, y));
}

/*---------------------------------------------------------------------------
 * Slider: set/get value round-trips, and a press maps its x monotonically to
 * the value (its own on_press override, not the clickable-mixin flag).
 *-------------------------------------------------------------------------*/
static float slider_value(struct ytest *test, struct yetty_yclass_object *sl)
{
    struct yetty_ycore_float_result v = yetty_ygui_slider_get_value(sl);
    YTEST_REQUIRE_OK(test, v);
    return v.value;
}

static void test_slider_press_maps_value(struct ytest *test)
{
    struct yetty_yclass_object *sl = make(test, yetty_ygui_slider_class_get());
    struct yetty_ycore_rectangle r = {{0, 0}, {100, 20}};
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_set_rect(sl, r));
    YTEST_REQUIRE_OK(test, yetty_ygui_slider_set_range(sl, 0.0f, 100.0f));

    /* Direct set/get round-trips exactly. */
    YTEST_REQUIRE_OK(test, yetty_ygui_slider_set_value(sl, 30.0f));
    YTEST_CHECK_NEAR(test, slider_value(test, sl), 30.0f, 0.01f);

    /* A press near the left maps to a smaller value than one near the right. */
    press(test, sl, 10.0f, 10.0f);
    float lo = slider_value(test, sl);
    press(test, sl, 90.0f, 10.0f);
    float hi = slider_value(test, sl);
    YTEST_CHECK(test, hi > lo);
    /* And the mapped values stay within the configured range. */
    YTEST_CHECK(test, lo >= 0.0f && lo <= 100.0f);
    YTEST_CHECK(test, hi >= 0.0f && hi <= 100.0f);

    yetty_ygui_widget_destroy(sl);
}

/*---------------------------------------------------------------------------
 * Draggable mixin (scrollarea): press starts a drag, motion fires the drag
 * callback, release ends the drag.
 *-------------------------------------------------------------------------*/
static struct yetty_ycore_void_result on_drag(struct yetty_yclass_object *obj, float dx, float dy,
                                              void *userdata)
{
    (void)obj;
    (void)dx;
    (void)dy;
    (*(int *)userdata)++;
    return YETTY_OK_VOID();
}

static int is_dragging(struct ytest *test, struct yetty_yclass_object *w)
{
    struct yetty_ycore_int_result d = yetty_ygui_draggable_is_dragging(w);
    YTEST_REQUIRE_OK(test, d);
    return d.value;
}

static void test_draggable_sequence(struct ytest *test)
{
    struct yetty_yclass_object *sa = make(test, yetty_ygui_scrollarea_class_get());
    int drags = 0;
    YTEST_REQUIRE_OK(test, yetty_ygui_draggable_on_drag_set(sa, on_drag, &drags));

    YTEST_CHECK(test, !is_dragging(test, sa));
    press(test, sa, 10.0f, 10.0f);
    YTEST_CHECK(test, is_dragging(test, sa)); /* press captures the drag */

    motion(test, sa, 10.0f, 20.0f);
    motion(test, sa, 10.0f, 35.0f);
    YTEST_CHECK(test, drags >= 1); /* motion while dragging fires the callback */
    YTEST_CHECK(test, is_dragging(test, sa));

    release(test, sa, 10.0f, 35.0f);
    YTEST_CHECK(test, !is_dragging(test, sa));

    yetty_ygui_widget_destroy(sa);
}

/*---------------------------------------------------------------------------
 * clickable mixin reused by non-button widgets.
 *-------------------------------------------------------------------------*/
static void click(struct ytest *test, struct yetty_yclass_object *w)
{
    press(test, w, 1.0f, 1.0f);
    release(test, w, 1.0f, 1.0f);
}

static int radio_selected(struct ytest *test, struct yetty_yclass_object *w)
{
    struct yetty_ycore_int_result r = yetty_ygui_radio_is_selected(w);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

static void test_radio_click_selects(struct ytest *test)
{
    struct yetty_yclass_object *rb = make(test, yetty_ygui_radio_class_get());
    YTEST_CHECK_EQ_INT(test, radio_selected(test, rb), 0); /* default unselected */

    click(test, rb);
    YTEST_CHECK_EQ_INT(test, radio_selected(test, rb), 1); /* click selects */
    click(test, rb);
    YTEST_CHECK_EQ_INT(test, radio_selected(test, rb), 1); /* radio stays selected */

    YTEST_REQUIRE_OK(test, yetty_ygui_radio_set_selected(rb, 0));
    YTEST_CHECK_EQ_INT(test, radio_selected(test, rb), 0); /* setter deselects */

    yetty_ygui_widget_destroy(rb);
}

static int selectable_selected(struct ytest *test, struct yetty_yclass_object *w)
{
    struct yetty_ycore_int_result r = yetty_ygui_selectable_is_selected(w);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

static void test_selectable_click_toggles(struct ytest *test)
{
    struct yetty_yclass_object *se = make(test, yetty_ygui_selectable_class_get());
    YTEST_CHECK_EQ_INT(test, selectable_selected(test, se), 0);

    click(test, se);
    YTEST_CHECK_EQ_INT(test, selectable_selected(test, se), 1); /* click toggles on */
    click(test, se);
    YTEST_CHECK_EQ_INT(test, selectable_selected(test, se), 0); /* click toggles off */

    YTEST_REQUIRE_OK(test, yetty_ygui_selectable_set_selected(se, 1));
    YTEST_CHECK_EQ_INT(test, selectable_selected(test, se), 1);

    yetty_ygui_widget_destroy(se);
}

/*---------------------------------------------------------------------------
 * Tabbar tab model: add / count / active / remove.
 *-------------------------------------------------------------------------*/
static int tabbar_count(struct ytest *test, struct yetty_yclass_object *tb)
{
    struct yetty_ycore_int_result r = yetty_ygui_tabbar_count(tb);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}
static int tabbar_active(struct ytest *test, struct yetty_yclass_object *tb)
{
    struct yetty_ycore_int_result r = yetty_ygui_tabbar_active(tb);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

static void test_tabbar_model(struct ytest *test)
{
    struct yetty_yclass_object *tb = make(test, yetty_ygui_tabbar_class_get());
    YTEST_CHECK_EQ_INT(test, tabbar_count(test, tb), 0);

    YTEST_REQUIRE_OK(test, yetty_ygui_tabbar_add_tab(tb, "one"));
    YTEST_REQUIRE_OK(test, yetty_ygui_tabbar_add_tab(tb, "two"));
    YTEST_REQUIRE_OK(test, yetty_ygui_tabbar_add_tab(tb, "three"));
    YTEST_CHECK_EQ_INT(test, tabbar_count(test, tb), 3);

    YTEST_REQUIRE_OK(test, yetty_ygui_tabbar_set_active(tb, 2));
    YTEST_CHECK_EQ_INT(test, tabbar_active(test, tb), 2);

    YTEST_REQUIRE_OK(test, yetty_ygui_tabbar_remove_tab(tb, 0));
    YTEST_CHECK_EQ_INT(test, tabbar_count(test, tb), 2);

    yetty_ygui_widget_destroy(tb);
}

int main(void)
{
    struct ytest test = ytest_begin("ygui_behavior");
    YTEST_RUN(&test, test_slider_press_maps_value);
    YTEST_RUN(&test, test_draggable_sequence);
    YTEST_RUN(&test, test_radio_click_selects);
    YTEST_RUN(&test, test_selectable_click_toggles);
    YTEST_RUN(&test, test_tabbar_model);
    return ytest_end(&test);
}
