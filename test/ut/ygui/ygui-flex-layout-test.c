/*
 * ygui-flex-layout-test.c — exercises the flexbox layout pass.
 *
 * Drives the public ygui API in headless mode (no OSC, no event loop):
 *   - create engine
 *   - inject a known canvas size via the testing API
 *   - build a flex container with children using grow / justify / align
 *   - call yetty_ygui_engine_layout()
 *   - assert resolved layout boxes
 *
 * Returns 0 on success, non-zero on first failed assertion.
 */

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <yetty/ygui/ygui.h>

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

static struct yetty_ygui_engine *make_engine(float canvas_w, float canvas_h)
{
    struct ygui_engine_ptr_result r = yetty_ygui_engine_create("flex-test", 0, 0, 80, 24);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "engine_create failed\n");
        exit(2);
    }
    struct yetty_ygui_engine *engine = r.value;
    yetty_ygui_engine_set_size(engine, canvas_w, canvas_h);
    /* Silence destroy-time OSC writes so the test output is clean. */
    if (g_devnull_fd < 0) {
        g_devnull_fd = open("/dev/null", O_WRONLY);
    }
    if (g_devnull_fd >= 0) {
        yetty_ygui_engine_set_output_fd(engine, g_devnull_fd);
    }
    return engine;
}

static void run_layout(struct yetty_ygui_engine *engine)
{
    struct yetty_ycore_void_result lr = yetty_ygui_engine_layout(engine);
    if (YETTY_IS_ERR(lr)) {
        fprintf(stderr, "engine_layout failed\n");
        yetty_ycore_error_destroy(lr.error);
        exit(3);
    }
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
    struct yetty_ygui_engine *e = make_engine(400, 100);

    struct yetty_ygui_widget *box = yetty_ygui_engine_hbox(e, "row", 0, 0, 400, 100);
    yetty_ygui_widget_set_padding(box, 0, 0, 0, 0);
    yetty_ygui_widget_set_gap(box, 10);

    struct yetty_ygui_widget *a = yetty_ygui_engine_button(e, "a", 0, 0, 50, 30, "A");
    struct yetty_ygui_widget *b = yetty_ygui_engine_button(e, "b", 0, 0, 50, 30, "B");
    struct yetty_ygui_widget *c = yetty_ygui_engine_button(e, "c", 0, 0, 50, 30, "C");
    yetty_ygui_widget_add_child(box, a);
    yetty_ygui_widget_add_child(box, b);
    yetty_ygui_widget_add_child(box, c);
    yetty_ygui_widget_set_flex(b, 1.0f, 0.0f, 0.0f);

    run_layout(e);

    float ax, ay, aw, ah;
    yetty_ygui_widget_get_layout_box(a, &ax, &ay, &aw, &ah);
    ASSERT_NEAR("A.x", ax, 0.0f);
    ASSERT_NEAR("A.w", aw, 50.0f);

    float bx, by, bw, bh;
    yetty_ygui_widget_get_layout_box(b, &bx, &by, &bw, &bh);
    ASSERT_NEAR("B.x", bx, 60.0f);
    ASSERT_NEAR("B.w", bw, 280.0f);

    float cx, cy, cw, ch;
    yetty_ygui_widget_get_layout_box(c, &cx, &cy, &cw, &ch);
    ASSERT_NEAR("C.x", cx, 350.0f);
    ASSERT_NEAR("C.w", cw, 50.0f);

    yetty_ygui_engine_destroy(e);
}

/* Test 2: column with align_items = STRETCH, child cross-axis fills container.
 *   container at (10, 20), 200x300, padding=0
 *   child cross-axis (width) should equal 200.
 */
static void test_column_stretch(void)
{
    fprintf(stderr, "\n[test_column_stretch]\n");
    struct yetty_ygui_engine *e = make_engine(500, 500);

    struct yetty_ygui_widget *box = yetty_ygui_engine_vbox(e, "col", 10, 20, 200, 300);
    yetty_ygui_widget_set_padding(box, 0, 0, 0, 0);
    yetty_ygui_widget_set_gap(box, 0);
    yetty_ygui_widget_set_align_items(box, YETTY_YGUI_ALIGN_STRETCH);

    struct yetty_ygui_widget *a = yetty_ygui_engine_button(e, "a", 0, 0, 80, 40, "A");
    yetty_ygui_widget_add_child(box, a);

    run_layout(e);

    float ax, ay, aw, ah;
    yetty_ygui_widget_get_layout_box(a, &ax, &ay, &aw, &ah);
    ASSERT_NEAR("A.x (absolute)", ax, 10.0f);
    ASSERT_NEAR("A.y (absolute)", ay, 20.0f);
    ASSERT_NEAR("A.w (stretched)", aw, 200.0f);
    ASSERT_NEAR("A.h (authored)", ah, 40.0f);

    yetty_ygui_engine_destroy(e);
}

/* Test 3: justify_content = SPACE_BETWEEN distributes free space between children.
 *   row 0..600, padding=0, gap=0, three 100-wide children
 *   total used = 300, free = 300 → 150 between each pair
 *   expected: A.x=0, B.x=250, C.x=500
 */
static void test_justify_space_between(void)
{
    fprintf(stderr, "\n[test_justify_space_between]\n");
    struct yetty_ygui_engine *e = make_engine(600, 100);

    struct yetty_ygui_widget *row = yetty_ygui_engine_hbox(e, "row", 0, 0, 600, 100);
    yetty_ygui_widget_set_padding(row, 0, 0, 0, 0);
    yetty_ygui_widget_set_gap(row, 0);
    yetty_ygui_widget_set_justify_content(row, YETTY_YGUI_JUSTIFY_SPACE_BETWEEN);

    struct yetty_ygui_widget *a = yetty_ygui_engine_button(e, "a", 0, 0, 100, 30, "A");
    struct yetty_ygui_widget *b = yetty_ygui_engine_button(e, "b", 0, 0, 100, 30, "B");
    struct yetty_ygui_widget *c = yetty_ygui_engine_button(e, "c", 0, 0, 100, 30, "C");
    yetty_ygui_widget_add_child(row, a);
    yetty_ygui_widget_add_child(row, b);
    yetty_ygui_widget_add_child(row, c);

    run_layout(e);

    float ax, ay, aw, ah;
    yetty_ygui_widget_get_layout_box(a, &ax, &ay, &aw, &ah);
    float bx, by, bw, bh;
    yetty_ygui_widget_get_layout_box(b, &bx, &by, &bw, &bh);
    float cx, cy, cw, ch;
    yetty_ygui_widget_get_layout_box(c, &cx, &cy, &cw, &ch);

    ASSERT_NEAR("A.x", ax, 0.0f);
    ASSERT_NEAR("B.x", bx, 250.0f);
    ASSERT_NEAR("C.x", cx, 500.0f);

    yetty_ygui_engine_destroy(e);
}

/* Test 4: padding on container shifts children.
 *   container 0..400, padding {top: 5, right: 10, bottom: 15, left: 20}, gap=0
 *   single child 50x50: x = 20, y = 5.
 */
static void test_padding(void)
{
    fprintf(stderr, "\n[test_padding]\n");
    struct yetty_ygui_engine *e = make_engine(400, 400);

    struct yetty_ygui_widget *row = yetty_ygui_engine_hbox(e, "row", 0, 0, 400, 200);
    yetty_ygui_widget_set_padding(row, 5, 10, 15, 20);
    yetty_ygui_widget_set_gap(row, 0);

    struct yetty_ygui_widget *a = yetty_ygui_engine_button(e, "a", 0, 0, 50, 50, "A");
    yetty_ygui_widget_add_child(row, a);

    run_layout(e);

    float ax, ay;
    yetty_ygui_widget_get_layout_box(a, &ax, &ay, NULL, NULL);
    ASSERT_NEAR("A.x (padded)", ax, 20.0f);
    ASSERT_NEAR("A.y (padded)", ay, 5.0f);

    yetty_ygui_engine_destroy(e);
}

/* Test 5: manual mode keeps authored x/y untouched.
 *   top-level button at authored (37, 91); after layout, layout_x/y must match.
 *   Repeat layout twice to make sure authored geometry is not drifting.
 */
static void test_manual_no_drift(void)
{
    fprintf(stderr, "\n[test_manual_no_drift]\n");
    struct yetty_ygui_engine *e = make_engine(300, 300);

    struct yetty_ygui_widget *btn = yetty_ygui_engine_button(e, "btn", 37, 91, 60, 22, "btn");

    run_layout(e);
    float x1, y1, w1, h1;
    yetty_ygui_widget_get_layout_box(btn, &x1, &y1, &w1, &h1);
    ASSERT_NEAR("btn.x first", x1, 37.0f);
    ASSERT_NEAR("btn.y first", y1, 91.0f);
    ASSERT_NEAR("btn.w first", w1, 60.0f);
    ASSERT_NEAR("btn.h first", h1, 22.0f);

    run_layout(e);
    float x2, y2, w2, h2;
    yetty_ygui_widget_get_layout_box(btn, &x2, &y2, &w2, &h2);
    ASSERT_NEAR("btn.x second", x2, 37.0f);
    ASSERT_NEAR("btn.y second", y2, 91.0f);
    ASSERT_NEAR("btn.w second", w2, 60.0f);
    ASSERT_NEAR("btn.h second", h2, 22.0f);

    yetty_ygui_engine_destroy(e);
}

int main(void)
{
    /* Skip yetty_ygui_init() — it puts the controlling TTY into raw mode,
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

    if (g_failures > 0) {
        fprintf(stderr, "\n%d failure(s)\n", g_failures);
        return 1;
    }
    fprintf(stderr, "\nall flex layout assertions passed\n");
    return 0;
}
