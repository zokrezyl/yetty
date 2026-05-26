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
 */

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/ygui.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct yetty_ycore_rectangle rect(float minx, float miny, float maxx, float maxy)
{
    struct yetty_ycore_rectangle r = {{minx, miny}, {maxx, maxy}};
    return r;
}

static int approx_eq(float a, float b)
{
    float d = a - b;
    if (d < 0) d = -d;
    return d < 0.5f;
}

static void test_hbox_two_children(void)
{
    struct yetty_ygui_object_ptr_result r = yetty_ygui_add(yetty_ygui_hbox_class_get(), NULL);
    assert(YETTY_IS_OK(r));
    struct yetty_ygui_object *root = r.value;

    struct yetty_ygui_object_ptr_result rc1 =
        yetty_ygui_add(yetty_ygui_label_class_get(), root);
    assert(YETTY_IS_OK(rc1));
    struct yetty_ygui_object_ptr_result rc2 =
        yetty_ygui_add(yetty_ygui_label_class_get(), root);
    assert(YETTY_IS_OK(rc2));

    /* Set widths via layout. */
    struct yetty_ygui_layout l1 = *yetty_ygui_widget_layout_get(rc1.value);
    l1.width = 30.0f;
    l1.height = 20.0f;
    yetty_ygui_widget_layout_set(rc1.value, &l1);

    struct yetty_ygui_layout l2 = *yetty_ygui_widget_layout_get(rc2.value);
    l2.width = 50.0f;
    l2.height = 20.0f;
    yetty_ygui_widget_layout_set(rc2.value, &l2);

    struct yetty_ygui_layout lp = *yetty_ygui_widget_layout_get(root);
    lp.gap = 5.0f;
    yetty_ygui_widget_layout_set(root, &lp);

    struct yetty_ycore_void_result lr =
        yetty_ygui_layout_compute(root, rect(0, 0, 200, 100));
    assert(YETTY_IS_OK(lr));

    /* Children appear in insertion order (yetty_ygui_add appends at
     * tail). rc1 first, then rc2. */
    struct yetty_ygui_object *first = yetty_ygui_object_first_child(root);
    struct yetty_ygui_object *second = yetty_ygui_object_next_sibling(first);
    assert(first == rc1.value);
    assert(second == rc2.value);

    struct yetty_ycore_rectangle r1 = yetty_ygui_widget_rect(first);
    struct yetty_ycore_rectangle r2 = yetty_ygui_widget_rect(second);
    /* rc1 (width 30) at x=0, then gap=5, then rc2 (width 50) at x=35. */
    assert(approx_eq(r1.min.x, 0.0f));
    assert(approx_eq(r1.max.x, 30.0f));
    assert(approx_eq(r2.min.x, 35.0f));
    assert(approx_eq(r2.max.x, 85.0f));

    yetty_ygui_del(root);
}

static void test_vbox_flex_grow(void)
{
    /* vbox 200 tall, three children. First has flex_grow=1, others=0
     * with explicit heights. Free space should land in the grow child. */
    struct yetty_ygui_object_ptr_result r = yetty_ygui_add(yetty_ygui_vbox_class_get(), NULL);
    assert(YETTY_IS_OK(r));
    struct yetty_ygui_object *root = r.value;

    /* Append at tail: insertion order == layout order (c1, c2, c3). */
    struct yetty_ygui_object *c1 = yetty_ygui_add(yetty_ygui_label_class_get(), root).value;
    struct yetty_ygui_object *c2 = yetty_ygui_add(yetty_ygui_label_class_get(), root).value;
    struct yetty_ygui_object *c3 = yetty_ygui_add(yetty_ygui_label_class_get(), root).value;

    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(c1);
    l.height = 0.0f;
    l.flex_grow = 1.0f;
    l.width = 100.0f;
    yetty_ygui_widget_layout_set(c1, &l);

    struct yetty_ygui_layout l2 = *yetty_ygui_widget_layout_get(c2);
    l2.height = 40.0f;
    l2.width = 100.0f;
    yetty_ygui_widget_layout_set(c2, &l2);

    struct yetty_ygui_layout l3 = *yetty_ygui_widget_layout_get(c3);
    l3.height = 60.0f;
    l3.width = 100.0f;
    yetty_ygui_widget_layout_set(c3, &l3);

    struct yetty_ycore_void_result lr =
        yetty_ygui_layout_compute(root, rect(0, 0, 200, 200));
    assert(YETTY_IS_OK(lr));

    struct yetty_ycore_rectangle rc1 = yetty_ygui_widget_rect(c1);
    struct yetty_ycore_rectangle rc2 = yetty_ygui_widget_rect(c2);
    struct yetty_ycore_rectangle rc3 = yetty_ygui_widget_rect(c3);

    /* c1 absorbs the free space: 200 - (40 + 60) = 100. */
    assert(approx_eq(rc1.max.y - rc1.min.y, 100.0f));
    assert(approx_eq(rc2.max.y - rc2.min.y, 40.0f));
    assert(approx_eq(rc3.max.y - rc3.min.y, 60.0f));
    /* Stacked top-to-bottom in insertion order. */
    assert(approx_eq(rc1.min.y, 0.0f));
    assert(approx_eq(rc2.min.y, 100.0f));
    assert(approx_eq(rc3.min.y, 140.0f));

    yetty_ygui_del(root);
}

static void test_padding(void)
{
    struct yetty_ygui_object *root = yetty_ygui_add(yetty_ygui_hbox_class_get(), NULL).value;
    struct yetty_ygui_object *c = yetty_ygui_add(yetty_ygui_label_class_get(), root).value;

    struct yetty_ygui_layout lp = *yetty_ygui_widget_layout_get(root);
    lp.padding_left = 10;
    lp.padding_top = 20;
    lp.padding_right = 30;
    lp.padding_bottom = 40;
    /* Stretch child cross-axis so we can verify content_h. */
    lp.align = YETTY_YGUI_ALIGN_STRETCH;
    yetty_ygui_widget_layout_set(root, &lp);

    struct yetty_ygui_layout lc = *yetty_ygui_widget_layout_get(c);
    lc.width = 50;
    yetty_ygui_widget_layout_set(c, &lc);

    yetty_ygui_layout_compute(root, rect(0, 0, 200, 200));

    struct yetty_ycore_rectangle cr = yetty_ygui_widget_rect(c);
    /* content origin: (10, 20). Child at (10, 20) width 50, height stretched
     * to content_h = 200 - 20 - 40 = 140. */
    assert(approx_eq(cr.min.x, 10.0f));
    assert(approx_eq(cr.min.y, 20.0f));
    assert(approx_eq(cr.max.x, 60.0f));
    assert(approx_eq(cr.max.y, 160.0f));

    yetty_ygui_del(root);
}

/* Click callback test. */
static int click_fired = 0;
static struct yetty_ycore_void_result on_click_cb(struct yetty_ygui_object *obj, void *userdata)
{
    (void)obj;
    int *counter = (int *)userdata;
    (*counter)++;
    return YETTY_OK_VOID();
}

static void test_clickable_state_machine(void)
{
    struct yetty_ygui_object *btn = yetty_ygui_add(yetty_ygui_button_class_get(), NULL).value;
    assert(btn);
    yetty_ygui_button_set_label(btn, "OK");
    assert(strcmp(yetty_ygui_button_get_label(btn), "OK") == 0);

    click_fired = 0;
    struct yetty_ycore_void_result sr =
        yetty_ygui_clickable_on_click_set(btn, on_click_cb, &click_fired);
    assert(YETTY_IS_OK(sr));

    assert(!yetty_ygui_clickable_is_pressed(btn));
    struct yetty_ycore_int_result pr = yetty_ygui_widget_on_press(btn, 1, 1, 0);
    assert(YETTY_IS_OK(pr));
    assert(pr.value == 1);
    assert(yetty_ygui_clickable_is_pressed(btn));
    assert(click_fired == 0); /* not yet — press alone doesn't fire */

    struct yetty_ycore_int_result rr = yetty_ygui_widget_on_release(btn, 1, 1, 0);
    assert(YETTY_IS_OK(rr));
    assert(rr.value == 1);
    assert(!yetty_ygui_clickable_is_pressed(btn));
    assert(click_fired == 1);

    /* Release without prior press → no fire. */
    struct yetty_ycore_int_result rr2 = yetty_ygui_widget_on_release(btn, 1, 1, 0);
    assert(YETTY_IS_OK(rr2));
    assert(click_fired == 1);

    yetty_ygui_del(btn);
}

static void test_widget_paint_emits_real_prims(void)
{
    /* Build a tree: panel (10,10)-(110,110) containing a label and a
     * button. Drive paint and verify real SDF prims + a TEXT_SPAN
     * land in the ydraw draw_list. */
    struct yetty_ygui_object *panel = yetty_ygui_add(yetty_ygui_panel_class_get(), NULL).value;
    struct yetty_ygui_object *label = yetty_ygui_add(yetty_ygui_label_class_get(), panel).value;
    struct yetty_ygui_object *btn = yetty_ygui_add(yetty_ygui_button_class_get(), panel).value;
    yetty_ygui_label_set_text(label, "hi");
    yetty_ygui_button_set_label(btn, "go");

    /* Size the widgets so paint produces non-empty geometry. */
    yetty_ygui_widget_set_rect(panel, rect(10, 10, 110, 110));
    yetty_ygui_widget_set_rect(label, rect(20, 20, 100, 40));
    yetty_ygui_widget_set_rect(btn, rect(20, 60, 100, 90));

    struct yetty_ydraw_draw_list_result dlr = yetty_ydraw_draw_list_config_buffer_create(NULL);
    assert(YETTY_IS_OK(dlr));
    struct yetty_ygui_emit_ctx ctx = {
        .engine = NULL,
        .container_records = NULL,
        .ygrid_draw_list = dlr.value,
        .figure_bodies = NULL,
        .current_figure_id = 0,
    };
    yetty_ygui_widget_paint(panel, &ctx);
    yetty_ygui_widget_paint(label, &ctx);
    yetty_ygui_widget_paint(btn, &ctx);

    /* Walk the prims by type word — confirm: panel SDF_BOX (0x7FFFFFFE),
     * label TEXT_SPAN (0x40000002), button SDF_ROUNDED_BOX (0x7FFFFFF7)
     * + another TEXT_SPAN. */
    size_t sz = yetty_ydraw_draw_list_size(dlr.value);
    const uint32_t *p = (const uint32_t *)yetty_ydraw_draw_list_data(dlr.value);
    int saw_box = 0, saw_rounded = 0, saw_text = 0;
    size_t off = 0;
    while (off + 4 <= sz) {
        uint32_t type = p[off / 4];
        if (type == 0x7FFFFFFEu) saw_box = 1;
        if (type == 0x7FFFFFF7u) saw_rounded = 1;
        if (type == 0x40000002u) saw_text++;
        /* Word count differs per prim — for this assertion we walk by
         * type-word locations using the known counts. SDF_BOX = 10
         * words; SDF_ROUNDED_BOX = 13; TEXT_SPAN is FAM with the size
         * word right after the type word. */
        if (type == 0x7FFFFFFEu) {
            off += 10 * 4;
        } else if (type == 0x7FFFFFF7u) {
            off += 13 * 4;
        } else if (type == 0x40000002u) {
            /* TEXT_SPAN: type | payload_size | payload (padded to 4) */
            uint32_t payload_size = p[(off / 4) + 1];
            off += 8 + ((payload_size + 3) & ~3u);
        } else {
            break;
        }
    }
    assert(saw_box);
    assert(saw_rounded);
    assert(saw_text >= 2);

    yetty_ydraw_draw_list_destroy(dlr.value);
    yetty_ygui_del(panel);
}

int main(void)
{
    test_hbox_two_children();
    test_vbox_flex_grow();
    test_padding();
    test_clickable_state_machine();
    test_widget_paint_emits_real_prims();
    puts("ygui-layout-test: OK");
    return 0;
}
