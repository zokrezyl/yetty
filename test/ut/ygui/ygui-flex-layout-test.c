/*
 * ygui-flex-layout-test.c — exercises the flexbox layout pass.
 *
 * Drives the public ygui API in headless mode (no OSC, no event loop):
 *   - create engine
 *   - inject a known canvas size via the testing API
 *   - build a flex container with children using grow / justify / align
 *   - call yetty_ygui_old_engine_layout()
 *   - assert resolved layout boxes
 *
 * Returns 0 on success, non-zero on first failed assertion.
 */

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <yetty/ygui-old/ygui.h>

static int g_failures = 0;

#define EPS 0.5f

#define ASSERT_NEAR(name, got, expect)                                                              \
    do {                                                                                            \
        float _g = (got);                                                                           \
        float _e = (expect);                                                                        \
        if (fabsf(_g - _e) > EPS) {                                                                 \
            fprintf(stderr, "FAIL %s: got %.3f expected %.3f\n", (name), _g, _e);                   \
            g_failures++;                                                                           \
        } else {                                                                                    \
            fprintf(stderr, "ok   %s = %.3f\n", (name), _g);                                        \
        }                                                                                           \
    } while (0)

static int g_devnull_fd = -1;

static struct yetty_ygui_old_engine *make_engine(float canvas_w, float canvas_h)
{
    struct yetty_ygui_old_engine_args args = { .name = "flex-test" };
    struct ygui_engine_ptr_result r = yetty_ygui_old_engine_create(args);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "engine_create failed\n");
        exit(2);
    }
    struct yetty_ygui_old_engine *engine = r.value;
    yetty_ygui_old_engine_set_size(engine, canvas_w, canvas_h);
    /* Silence destroy-time OSC writes so the test output is clean. */
    if (g_devnull_fd < 0) {
        g_devnull_fd = open("/dev/null", O_WRONLY);
    }
    if (g_devnull_fd >= 0) {
        yetty_ygui_old_engine_set_output_fd(engine, g_devnull_fd);
    }
    return engine;
}

static void run_layout(struct yetty_ygui_old_engine *engine)
{
    struct yetty_ycore_void_result lr = yetty_ygui_old_engine_layout(engine);
    if (YETTY_IS_ERR(lr)) {
        fprintf(stderr, "engine_layout failed\n");
        yetty_ycore_error_destroy(lr.error);
        exit(3);
    }
}

/* Unwrap helper — tests are run after a successful layout pass, the widget
 * pointers are non-NULL, so a Result error here is a test-infrastructure
 * failure. Abort loudly instead of polluting every assertion with the
 * check. */
static struct yetty_ycore_rectangle layout_box(const struct yetty_ygui_old_widget *w)
{
    struct rectangle_result r = yetty_ygui_old_widget_get_layout_box(w);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "layout_box: NULL widget\n");
        yetty_ycore_error_destroy(r.error);
        exit(4);
    }
    return r.value;
}

/* Test 1: row with three children, middle has flex_grow=1.
 *   container 0..400, padding=0, gap=10
 *   child A: authored_w=50, child B: authored_w=50 grow=1, child C: authored_w=50
 *   total fixed = 150 + 2*10 gap = 170 → free = 230 → B becomes 280
 *   expected: A.x=0,w=50; B.x=60,w=280; C.x=350,w=50
 */
static void test_row_grow(void)
{
    fprintf(stderr, "\n[test_row_grow]\n");
    struct yetty_ygui_old_engine *e = make_engine(400, 100);

    struct yetty_ygui_old_widget *box = yetty_ygui_old_engine_hbox(e, "row", 0, 0, 400, 100);
    yetty_ygui_old_widget_set_padding(box, 0, 0, 0, 0);
    yetty_ygui_old_widget_set_gap(box, 10);

    struct yetty_ygui_old_widget *a = yetty_ygui_old_engine_button(e, "a", 0, 0, 50, 30, "A");
    struct yetty_ygui_old_widget *b = yetty_ygui_old_engine_button(e, "b", 0, 0, 50, 30, "B");
    struct yetty_ygui_old_widget *c = yetty_ygui_old_engine_button(e, "c", 0, 0, 50, 30, "C");
    yetty_ygui_old_widget_add_child(box, a);
    yetty_ygui_old_widget_add_child(box, b);
    yetty_ygui_old_widget_add_child(box, c);
    yetty_ygui_old_widget_set_flex(b, 1.0f, 0.0f, 0.0f);

    run_layout(e);

    struct yetty_ycore_rectangle ab = layout_box(a);
    ASSERT_NEAR("A.x", ab.min.x, 0.0f);
    ASSERT_NEAR("A.w", ab.max.x - ab.min.x, 50.0f);

    struct yetty_ycore_rectangle bb = layout_box(b);
    ASSERT_NEAR("B.x", bb.min.x, 60.0f);
    ASSERT_NEAR("B.w", bb.max.x - bb.min.x, 280.0f);

    struct yetty_ycore_rectangle cb = layout_box(c);
    ASSERT_NEAR("C.x", cb.min.x, 350.0f);
    ASSERT_NEAR("C.w", cb.max.x - cb.min.x, 50.0f);

    yetty_ygui_old_engine_destroy(e);
}

/* Test 2: column with align_items = STRETCH, child cross-axis fills container.
 *   container at (10, 20), 200x300, padding=0
 *   child cross-axis (width) should equal 200.
 */
static void test_column_stretch(void)
{
    fprintf(stderr, "\n[test_column_stretch]\n");
    struct yetty_ygui_old_engine *e = make_engine(500, 500);

    struct yetty_ygui_old_widget *box = yetty_ygui_old_engine_vbox(e, "col", 10, 20, 200, 300);
    yetty_ygui_old_widget_set_padding(box, 0, 0, 0, 0);
    yetty_ygui_old_widget_set_gap(box, 0);
    yetty_ygui_old_widget_set_align_items(box, YETTY_YGUI_OLD_ALIGN_STRETCH);

    struct yetty_ygui_old_widget *a = yetty_ygui_old_engine_button(e, "a", 0, 0, 80, 40, "A");
    yetty_ygui_old_widget_add_child(box, a);

    run_layout(e);

    struct yetty_ycore_rectangle ab = layout_box(a);
    ASSERT_NEAR("A.x (absolute)", ab.min.x, 10.0f);
    ASSERT_NEAR("A.y (absolute)", ab.min.y, 20.0f);
    ASSERT_NEAR("A.w (stretched)", ab.max.x - ab.min.x, 200.0f);
    ASSERT_NEAR("A.h (authored)", ab.max.y - ab.min.y, 40.0f);

    yetty_ygui_old_engine_destroy(e);
}

/* Test 3: justify_content = SPACE_BETWEEN distributes free space between children.
 *   row 0..600, padding=0, gap=0, three 100-wide children
 *   total used = 300, free = 300 → 150 between each pair
 *   expected: A.x=0, B.x=250, C.x=500
 */
static void test_justify_space_between(void)
{
    fprintf(stderr, "\n[test_justify_space_between]\n");
    struct yetty_ygui_old_engine *e = make_engine(600, 100);

    struct yetty_ygui_old_widget *row = yetty_ygui_old_engine_hbox(e, "row", 0, 0, 600, 100);
    yetty_ygui_old_widget_set_padding(row, 0, 0, 0, 0);
    yetty_ygui_old_widget_set_gap(row, 0);
    yetty_ygui_old_widget_set_justify_content(row, YETTY_YGUI_OLD_JUSTIFY_SPACE_BETWEEN);

    struct yetty_ygui_old_widget *a = yetty_ygui_old_engine_button(e, "a", 0, 0, 100, 30, "A");
    struct yetty_ygui_old_widget *b = yetty_ygui_old_engine_button(e, "b", 0, 0, 100, 30, "B");
    struct yetty_ygui_old_widget *c = yetty_ygui_old_engine_button(e, "c", 0, 0, 100, 30, "C");
    yetty_ygui_old_widget_add_child(row, a);
    yetty_ygui_old_widget_add_child(row, b);
    yetty_ygui_old_widget_add_child(row, c);

    run_layout(e);

    struct yetty_ycore_rectangle ab = layout_box(a);
    struct yetty_ycore_rectangle bb = layout_box(b);
    struct yetty_ycore_rectangle cb = layout_box(c);

    ASSERT_NEAR("A.x", ab.min.x, 0.0f);
    ASSERT_NEAR("B.x", bb.min.x, 250.0f);
    ASSERT_NEAR("C.x", cb.min.x, 500.0f);

    yetty_ygui_old_engine_destroy(e);
}

/* Test 4: padding on container shifts children.
 *   container 0..400, padding {top: 5, right: 10, bottom: 15, left: 20}, gap=0
 *   single child 50x50: x = 20, y = 5.
 */
static void test_padding(void)
{
    fprintf(stderr, "\n[test_padding]\n");
    struct yetty_ygui_old_engine *e = make_engine(400, 400);

    struct yetty_ygui_old_widget *row = yetty_ygui_old_engine_hbox(e, "row", 0, 0, 400, 200);
    yetty_ygui_old_widget_set_padding(row, 5, 10, 15, 20);
    yetty_ygui_old_widget_set_gap(row, 0);

    struct yetty_ygui_old_widget *a = yetty_ygui_old_engine_button(e, "a", 0, 0, 50, 50, "A");
    yetty_ygui_old_widget_add_child(row, a);

    run_layout(e);

    struct yetty_ycore_rectangle ab = layout_box(a);
    ASSERT_NEAR("A.x (padded)", ab.min.x, 20.0f);
    ASSERT_NEAR("A.y (padded)", ab.min.y, 5.0f);

    yetty_ygui_old_engine_destroy(e);
}

/* Test 5: manual mode keeps authored x/y untouched.
 *   top-level button at authored (37, 91); after layout, layout_x/y must match.
 *   Repeat layout twice to make sure authored geometry is not drifting.
 */
static void test_manual_no_drift(void)
{
    fprintf(stderr, "\n[test_manual_no_drift]\n");
    struct yetty_ygui_old_engine *e = make_engine(300, 300);

    struct yetty_ygui_old_widget *btn = yetty_ygui_old_engine_button(e, "btn", 37, 91, 60, 22, "btn");

    run_layout(e);
    struct yetty_ycore_rectangle b1 = layout_box(btn);
    ASSERT_NEAR("btn.x first", b1.min.x, 37.0f);
    ASSERT_NEAR("btn.y first", b1.min.y, 91.0f);
    ASSERT_NEAR("btn.w first", b1.max.x - b1.min.x, 60.0f);
    ASSERT_NEAR("btn.h first", b1.max.y - b1.min.y, 22.0f);

    run_layout(e);
    struct yetty_ycore_rectangle b2 = layout_box(btn);
    ASSERT_NEAR("btn.x second", b2.min.x, 37.0f);
    ASSERT_NEAR("btn.y second", b2.min.y, 91.0f);
    ASSERT_NEAR("btn.w second", b2.max.x - b2.min.x, 60.0f);
    ASSERT_NEAR("btn.h second", b2.max.y - b2.min.y, 22.0f);

    yetty_ygui_old_engine_destroy(e);
}

/* Test 6: absolute positioning skips flex flow.
 *   row 0..400, padding=0, gap=0, two flex children + one absolute child.
 *   The absolute child sits at its authored x/y inside content box and
 *   doesn't push the flex children. */
static void test_position_absolute(void)
{
    fprintf(stderr, "\n[test_position_absolute]\n");
    struct yetty_ygui_old_engine *e = make_engine(400, 200);

    struct yetty_ygui_old_widget *row = yetty_ygui_old_engine_hbox(e, "row", 0, 0, 400, 200);
    yetty_ygui_old_widget_set_padding(row, 0, 0, 0, 0);
    yetty_ygui_old_widget_set_gap(row, 0);

    struct yetty_ygui_old_widget *a = yetty_ygui_old_engine_button(e, "a", 0, 0, 100, 50, "A");
    struct yetty_ygui_old_widget *b = yetty_ygui_old_engine_button(e, "b", 0, 0, 100, 50, "B");
    struct yetty_ygui_old_widget *abs_btn = yetty_ygui_old_engine_button(e, "abs", 200, 80, 60, 30, "abs");
    yetty_ygui_old_widget_add_child(row, a);
    yetty_ygui_old_widget_add_child(row, b);
    yetty_ygui_old_widget_add_child(row, abs_btn);
    yetty_ygui_old_widget_set_position_mode(abs_btn, YETTY_YGUI_OLD_POSITION_ABSOLUTE);

    run_layout(e);

    struct yetty_ycore_rectangle ab = layout_box(a);
    struct yetty_ycore_rectangle bb = layout_box(b);
    struct yetty_ycore_rectangle abs_b = layout_box(abs_btn);

    /* Flex children are unaffected by the absolute one. */
    ASSERT_NEAR("A.x", ab.min.x, 0.0f);
    ASSERT_NEAR("B.x", bb.min.x, 100.0f);
    /* Absolute child sits at row.content + (200, 80). */
    ASSERT_NEAR("abs.x", abs_b.min.x, 200.0f);
    ASSERT_NEAR("abs.y", abs_b.min.y, 80.0f);

    yetty_ygui_old_engine_destroy(e);
}

/* Test 7: flex_basis_percent. Single child basis = 25% of 400 = 100. */
static void test_basis_percent(void)
{
    fprintf(stderr, "\n[test_basis_percent]\n");
    struct yetty_ygui_old_engine *e = make_engine(400, 200);

    struct yetty_ygui_old_widget *row = yetty_ygui_old_engine_hbox(e, "row", 0, 0, 400, 200);
    yetty_ygui_old_widget_set_padding(row, 0, 0, 0, 0);
    yetty_ygui_old_widget_set_gap(row, 0);

    struct yetty_ygui_old_widget *a = yetty_ygui_old_engine_button(e, "a", 0, 0, 50, 30, "A");
    yetty_ygui_old_widget_add_child(row, a);
    yetty_ygui_old_widget_set_flex_basis_percent(a, 25.0f);

    run_layout(e);
    struct yetty_ycore_rectangle ab = layout_box(a);
    ASSERT_NEAR("A.w (25% of 400)", ab.max.x - ab.min.x, 100.0f);

    yetty_ygui_old_engine_destroy(e);
}

/* Test 8: flex-wrap puts overflowing children on a new line.
 *   row 200, three 90-wide children + gap=0. Wrap on:
 *   line 1: A (90), B (90)  → fits in 200 (180 + ?)
 *   line 2: C (90)
 *   Each line has cross_size = max child cross. */
static void test_flex_wrap(void)
{
    fprintf(stderr, "\n[test_flex_wrap]\n");
    struct yetty_ygui_old_engine *e = make_engine(400, 400);

    struct yetty_ygui_old_widget *row = yetty_ygui_old_engine_hbox(e, "row", 0, 0, 200, 200);
    yetty_ygui_old_widget_set_padding(row, 0, 0, 0, 0);
    yetty_ygui_old_widget_set_gap(row, 0);
    yetty_ygui_old_widget_set_flex_wrap(row, YETTY_YGUI_OLD_FLEX_WRAP);

    struct yetty_ygui_old_widget *a = yetty_ygui_old_engine_button(e, "a", 0, 0, 90, 30, "A");
    struct yetty_ygui_old_widget *b = yetty_ygui_old_engine_button(e, "b", 0, 0, 90, 30, "B");
    struct yetty_ygui_old_widget *c = yetty_ygui_old_engine_button(e, "c", 0, 0, 90, 30, "C");
    yetty_ygui_old_widget_add_child(row, a);
    yetty_ygui_old_widget_add_child(row, b);
    yetty_ygui_old_widget_add_child(row, c);

    run_layout(e);
    struct yetty_ycore_rectangle ab = layout_box(a);
    struct yetty_ycore_rectangle bb = layout_box(b);
    struct yetty_ycore_rectangle cb = layout_box(c);

    /* A and B fit on line 1 at y=0 */
    ASSERT_NEAR("A.y", ab.min.y, 0.0f);
    ASSERT_NEAR("B.y", bb.min.y, 0.0f);
    /* C wraps to line 2: y = first line cross_size = 30 */
    ASSERT_NEAR("C.y (wrapped)", cb.min.y, 30.0f);
    /* C is back at the start of the main axis */
    ASSERT_NEAR("C.x", cb.min.x, 0.0f);

    yetty_ygui_old_engine_destroy(e);
}

/* Test 9: CSS parser sets flex direction + gap + padding from a string. */
static void test_css_apply(void)
{
    fprintf(stderr, "\n[test_css_apply]\n");
    struct yetty_ygui_old_engine *e = make_engine(400, 400);

    struct yetty_ygui_old_widget *row = yetty_ygui_old_engine_hbox(e, "row", 0, 0, 400, 200);
    /* Override the hbox defaults via CSS: zero padding, custom gap,
     * justify-content center. Then add a flex-grow on a child. */
    struct yetty_ycore_void_result cr = yetty_ygui_old_widget_apply_css(
        row, "padding: 0; gap: 10px; justify-content: center;");
    if (YETTY_IS_ERR(cr)) {
        fprintf(stderr, "FAIL apply_css returned error: %s\n",
                cr.error.msg ? cr.error.msg : "(no msg)");
        yetty_ycore_error_destroy(cr.error);
        g_failures++;
    } else {
        fprintf(stderr, "ok   apply_css ok\n");
    }

    struct yetty_ygui_old_widget *a = yetty_ygui_old_engine_button(e, "a", 0, 0, 50, 30, "A");
    struct yetty_ygui_old_widget *b = yetty_ygui_old_engine_button(e, "b", 0, 0, 50, 30, "B");
    yetty_ygui_old_widget_add_child(row, a);
    yetty_ygui_old_widget_add_child(row, b);
    yetty_ygui_old_widget_apply_css(a, "flex: 0 0 80px;"); /* 80px basis, no grow */

    run_layout(e);
    struct yetty_ycore_rectangle ab = layout_box(a);
    struct yetty_ycore_rectangle bb = layout_box(b);

    /* total fixed = 80 + 50 + 10 gap = 140. center => leading = (400-140)/2 = 130. */
    ASSERT_NEAR("A.x (center)", ab.min.x, 130.0f);
    ASSERT_NEAR("A.w (basis 80px)", ab.max.x - ab.min.x, 80.0f);
    ASSERT_NEAR("B.x", bb.min.x, 130.0f + 80.0f + 10.0f);

    yetty_ygui_old_engine_destroy(e);
}

/* Test 10: width-percent in manual mode.
 *   panel 200x100 with one child (manual mode); child width: 50% → 100px. */
static void test_width_percent_manual(void)
{
    fprintf(stderr, "\n[test_width_percent_manual]\n");
    struct yetty_ygui_old_engine *e = make_engine(400, 400);

    struct yetty_ygui_old_widget *p = yetty_ygui_old_engine_panel(e, "p", 10, 10, 200, 100);
    struct yetty_ygui_old_widget *c = yetty_ygui_old_engine_button(e, "c", 5, 5, 30, 20, "C");
    yetty_ygui_old_widget_add_child(p, c);
    /* Panel is MANUAL by default. width: 50% of panel content. */
    yetty_ygui_old_widget_set_size_percent(c, 50.0f, 0.0f);

    run_layout(e);
    struct yetty_ycore_rectangle cb = layout_box(c);
    /* Panel content_w == 200 (no padding). 50% = 100. */
    ASSERT_NEAR("C.w (50% of 200)", cb.max.x - cb.min.x, 100.0f);

    yetty_ygui_old_engine_destroy(e);
}

/* Test 11: tree_node toggle hides/shows children list. After expanding,
 * a leaf row inside the tree_node should land below the header (positive
 * y relative to the tree_node) and shifted right by the indent. */
static void test_tree_node_basic(void)
{
    fprintf(stderr, "\n[test_tree_node_basic]\n");
    struct yetty_ygui_old_engine *e = make_engine(400, 400);

    struct yetty_ygui_old_widget *root = yetty_ygui_old_engine_list(e, "root", 0, 0, 300, 400);
    struct yetty_ygui_old_widget *node = yetty_ygui_old_engine_tree_node(e, "node", "Folder");
    yetty_ygui_old_widget_add_child(root, node);

    struct yetty_ygui_old_widget *leaf = yetty_ygui_old_engine_label(e, "leaf", 0, 0, "child");
    yetty_ygui_old_widget_add_child(yetty_ygui_old_widget_tree_node_children(node), leaf);

    /* Collapsed by default. children_list invisible → no contribution to
     * the tree_node height. */
    run_layout(e);
    int kids_visible = yetty_ygui_old_widget_is_visible(yetty_ygui_old_widget_tree_node_children(node));
    if (kids_visible) {
        fprintf(stderr, "FAIL children visible while collapsed\n");
        g_failures++;
    } else {
        fprintf(stderr, "ok   children invisible while collapsed\n");
    }

    /* Expand and re-layout. */
    yetty_ygui_old_widget_tree_node_set_expanded(node, 1);
    run_layout(e);
    kids_visible = yetty_ygui_old_widget_is_visible(yetty_ygui_old_widget_tree_node_children(node));
    if (!kids_visible) {
        fprintf(stderr, "FAIL children invisible after expand\n");
        g_failures++;
    } else {
        fprintf(stderr, "ok   children visible after expand\n");
    }

    /* The leaf's absolute layout_x should be at least node.layout_x +
     * default indent (20px). y should be below the header (>= 24). */
    struct yetty_ycore_rectangle leaf_b = layout_box(leaf);
    struct yetty_ycore_rectangle node_b = layout_box(node);

    float dx = leaf_b.min.x - node_b.min.x;
    float dy = leaf_b.min.y - node_b.min.y;
    if (dx < 18.0f) {
        fprintf(stderr, "FAIL indent dx = %.2f, expected >= 18\n", dx);
        g_failures++;
    } else {
        fprintf(stderr, "ok   indent dx = %.2f\n", dx);
    }
    if (dy < 22.0f) {
        fprintf(stderr, "FAIL header dy = %.2f, expected >= 22\n", dy);
        g_failures++;
    } else {
        fprintf(stderr, "ok   header dy = %.2f\n", dy);
    }

    yetty_ygui_old_engine_destroy(e);
}

/* Test 12: list selection state — clicking a row updates list.selected.
 * We exercise that by calling the list's on_press hit-test directly. */
static void test_list_selection(void)
{
    fprintf(stderr, "\n[test_list_selection]\n");
    struct yetty_ygui_old_engine *e = make_engine(400, 400);

    struct yetty_ygui_old_widget *list = yetty_ygui_old_engine_list(e, "list", 0, 0, 300, 400);
    struct yetty_ygui_old_widget *a = yetty_ygui_old_engine_label(e, "a", 0, 0, "Alpha");
    struct yetty_ygui_old_widget *b = yetty_ygui_old_engine_label(e, "b", 0, 0, "Beta");
    yetty_ygui_old_widget_add_child(list, a);
    yetty_ygui_old_widget_add_child(list, b);

    /* No selection initially. */
    if (yetty_ygui_old_widget_list_get_selected(list) != NULL) {
        fprintf(stderr, "FAIL list.selected != NULL initially\n");
        g_failures++;
    } else {
        fprintf(stderr, "ok   list.selected starts NULL\n");
    }

    /* Programmatic selection. */
    yetty_ygui_old_widget_list_set_selected(list, b);
    if (yetty_ygui_old_widget_list_get_selected(list) != b) {
        fprintf(stderr, "FAIL list.selected != b after set_selected\n");
        g_failures++;
    } else {
        fprintf(stderr, "ok   list.selected == b\n");
    }

    yetty_ygui_old_engine_destroy(e);
}

int main(void)
{
    /* Skip yetty_ygui_old_init() — it puts the controlling TTY into raw mode,
     * which both fails and is unnecessary under ctest. The layout pass is
     * pure arithmetic.
     *
     * The OSC sink in ygui_osc.c writes directly to FD 1 (bypassing
     * engine->output_fd). Redirect FD 1 to /dev/null so destroy-time
     * "remove card" sequences don't pollute test output. Test results go
     * to stderr. */
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
        dup2(devnull, STDOUT_FILENO);
        close(devnull);
    }

    test_row_grow();
    test_column_stretch();
    test_justify_space_between();
    test_padding();
    test_manual_no_drift();
    test_position_absolute();
    test_basis_percent();
    test_flex_wrap();
    test_css_apply();
    test_width_percent_manual();
    test_tree_node_basic();
    test_list_selection();

    if (g_failures > 0) {
        fprintf(stderr, "\n%d failure(s)\n", g_failures);
        return 1;
    }
    fprintf(stderr, "\nall flex layout assertions passed\n");
    return 0;
}
