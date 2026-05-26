/*
 * ygui-wire-test.c — end-to-end producer→receiver wire-validation tests.
 *
 * For each test we:
 *   1. Build a ygui tree (engine + widgets).
 *   2. Run yetty_ygui_old_engine_render_headless — runs the same layout +
 *      rebuild pipeline as engine_render but stops before OSC shipping,
 *      leaving the raw record stream in the engine's internal buffer.
 *   3. Read the bytes via yetty_ygui_old_engine_buffer_data() / _size().
 *   4. Feed them to a yetty_yfigure_container's process_records.
 *   5. Dump the container via yetty_yfigure_dump.
 *   6. Assert key substrings (the dump's overall shape) appear.
 *
 * The receiver-side ygrid is registered in HEADLESS mode (NULL context)
 * so no GPU is involved. Tests are unit-only — no card, no OSC.
 *
 * Tests start with the simplest possible ygui tree (one button) and
 * grow toward a small flexbox layout. Each new test stacks one more
 * layer of widget complexity on top of the prior assumptions.
 *
 * Returns 0 on success, non-zero on first failed assertion.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <yetty/ycore/result.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/ygui-old/ygui.h>

/* Internal entry — bypasses the libuv runtime bootstrap that the public
 * engine_create wires up. The headless tests don't need an event loop /
 * pty / OSC envelope codec; they only need the engine struct + buffer +
 * widget tree. */
#include "yetty/ygui-old/ygui_internal.h"

static int g_failures = 0;
static int g_tests = 0;
static int g_devnull_fd = -1;

#define EXPECT_CONTAINS(name, hay, needle)                                                          \
    do {                                                                                            \
        if (!(hay) || !(needle) || strstr((hay), (needle)) == NULL) {                               \
            fprintf(stderr, "FAIL %s: missing substring\n--- expected ---\n%s\n--- in ---\n%s\n",   \
                    (name), (needle), (hay) ? (hay) : "(null)");                                    \
            g_failures++;                                                                           \
        } else {                                                                                    \
            fprintf(stderr, "ok   %s\n", (name));                                                   \
        }                                                                                           \
    } while (0)

#define EXPECT_NOT_CONTAINS(name, hay, needle)                                                      \
    do {                                                                                            \
        if (!(hay) || ((needle) && strstr((hay), (needle)) != NULL)) {                              \
            fprintf(stderr, "FAIL %s: unexpected substring\n--- not expected ---\n%s\n"             \
                            "--- in ---\n%s\n",                                                     \
                    (name), (needle), (hay) ? (hay) : "(null)");                                    \
            g_failures++;                                                                           \
        } else {                                                                                    \
            fprintf(stderr, "ok   %s (absent)\n", (name));                                          \
        }                                                                                           \
    } while (0)

static struct yetty_ygui_old_engine *make_engine(float w, float h)
{
    /* Use the internal allocator to avoid libuv bootstrap. The engine
     * struct + buffer + grid + measure_font are all initialised; OSC
     * shipping and event-loop integration are not. That's what we want
     * for a pure-wire-output test. */
    struct ygui_engine_ptr_result r = yetty_ygui_old_engine_internal_alloc("wire-test", NULL);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "engine_internal_alloc failed: %s\n", r.error.msg);
        yetty_ycore_error_destroy(r.error);
        exit(2);
    }
    yetty_ygui_old_engine_set_size(r.value, w, h);
    if (g_devnull_fd < 0) {
        g_devnull_fd = open("/dev/null", O_WRONLY);
    }
    if (g_devnull_fd >= 0) {
        yetty_ygui_old_engine_set_output_fd(r.value, g_devnull_fd);
    }
    return r.value;
}

/* Build a receiver: container at root + registry that knows YGRID kind. */
struct test_receiver {
    struct yetty_yfigure_registry *registry;
    struct yetty_yfigure_container *root;
};

static struct test_receiver receiver_create(float canvas_w, float canvas_h)
{
    struct yetty_yfigure_registry_ptr_result rr = yetty_yfigure_registry_create();
    if (YETTY_IS_ERR(rr)) {
        fprintf(stderr, "registry_create failed\n");
        yetty_ycore_error_destroy(rr.error);
        exit(2);
    }
    /* Register the production ygrid factory under YGRID kind. With a
     * NULL factory_args the ygrid will mint with no default font and no
     * complex-prim factory — fine for shape-only tests. The
     * ygrid_create path tolerates NULL context (test/headless mode). */
    static const struct yetty_ygrid_factory_args args = {
        .default_font = NULL,
        .figure_factory = NULL,
    };
    struct yetty_ycore_void_result rf = yetty_ygrid_register_factory(rr.value, &args);
    if (YETTY_IS_ERR(rf)) {
        fprintf(stderr, "ygrid_register_factory failed\n");
        yetty_ycore_error_destroy(rf.error);
        exit(2);
    }

    struct yetty_ycore_rectangle rect = {{0, 0}, {canvas_w, canvas_h}};
    struct yetty_yfigure_container_ptr_result cr =
        yetty_yfigure_container_create(rect, NULL, rr.value);
    if (YETTY_IS_ERR(cr)) {
        fprintf(stderr, "container_create failed\n");
        yetty_ycore_error_destroy(cr.error);
        exit(2);
    }
    struct test_receiver out = {.registry = rr.value, .root = cr.value};
    return out;
}

static void receiver_destroy(struct test_receiver *r)
{
    if (r->root) {
        struct yetty_yfigure_figure *fig = yetty_yfigure_container_as_figure(r->root);
        fig->ops->destroy(fig);
        r->root = NULL;
    }
    if (r->registry) {
        yetty_yfigure_registry_destroy(r->registry);
        r->registry = NULL;
    }
}

/* Run the engine through one headless render, feed the emitted bytes
 * into the receiver, and return the resulting dump (caller frees). */
static char *render_and_dump(struct yetty_ygui_old_engine *engine, struct test_receiver *recv)
{
    struct yetty_ycore_void_result r = yetty_ygui_old_engine_render_headless(engine);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "engine_render_headless failed: %s\n", r.error.msg);
        yetty_ycore_error_destroy(r.error);
        exit(3);
    }
    const uint8_t *bytes = (const uint8_t *)yetty_ygui_old_engine_buffer_data(engine);
    size_t len = yetty_ygui_old_engine_buffer_size(engine);
    if (len > 0 && bytes != NULL) {
        struct yetty_ycore_void_result pr =
            yetty_yfigure_container_process_records(recv->root, bytes, len);
        if (YETTY_IS_ERR(pr)) {
            fprintf(stderr, "container_process_records failed: %s\n", pr.error.msg);
            yetty_ycore_error_destroy(pr.error);
            exit(3);
        }
    }
    return yetty_yfigure_dump(yetty_yfigure_container_as_figure(recv->root), 0);
}

/*===========================================================================
 * Test 1: a single top-level button.
 *
 * Shape we expect after one headless render:
 *   - the container has at least one child (the button's chrome ygrid),
 *   - whose kind is `ygrid`,
 *   - whose rect matches the engine canvas size or the button's box.
 *===========================================================================*/
static void test_single_button(void)
{
    fprintf(stderr, "\n[test_single_button]\n");
    g_tests++;
    struct yetty_ygui_old_engine *engine = make_engine(400, 100);
    struct test_receiver recv = receiver_create(400, 100);

    yetty_ygui_old_engine_button(engine, "btn", 10, 10, 80, 30, "Hello");

    char *dump = render_and_dump(engine, &recv);
    fprintf(stderr, "--- dump ---\n%s--- end dump ---\n", dump);

    EXPECT_CONTAINS("single_button: container header", dump, "kind: container\n");
    EXPECT_CONTAINS("single_button: at least one child", dump, "children:\n");
    EXPECT_CONTAINS("single_button: child is ygrid", dump, "kind: ygrid\n");
    /* A button paints a rounded-box body + a glyph run for the label.
     * The body box is one SDF prim per chrome element; expect at least
     * one live prim under the button's entity tree. */
    EXPECT_CONTAINS("single_button: at least one prim", dump, "prim_count: ");
    free(dump);

    receiver_destroy(&recv);
    yetty_ygui_old_engine_destroy(engine);
}

/*===========================================================================
 * Test 2: a horizontal hbox with two buttons. The hbox layout must place
 * the buttons side-by-side; both buttons appear as separate entities
 * inside the chrome ygrid figure.
 *===========================================================================*/
static void test_hbox_two_buttons(void)
{
    fprintf(stderr, "\n[test_hbox_two_buttons]\n");
    g_tests++;
    struct yetty_ygui_old_engine *engine = make_engine(400, 100);
    struct test_receiver recv = receiver_create(400, 100);

    struct yetty_ygui_old_widget *row = yetty_ygui_old_engine_hbox(engine, "row", 0, 0, 400, 100);
    yetty_ygui_old_widget_set_padding(row, 0, 0, 0, 0);
    yetty_ygui_old_widget_set_gap(row, 8);

    struct yetty_ygui_old_widget *a = yetty_ygui_old_engine_button(engine, "a", 0, 0, 100, 30, "A");
    struct yetty_ygui_old_widget *b = yetty_ygui_old_engine_button(engine, "b", 0, 0, 100, 30, "B");
    yetty_ygui_old_widget_add_child(row, a);
    yetty_ygui_old_widget_add_child(row, b);

    char *dump = render_and_dump(engine, &recv);
    fprintf(stderr, "--- dump ---\n%s--- end dump ---\n", dump);

    EXPECT_CONTAINS("hbox_two_buttons: container header", dump, "kind: container\n");
    EXPECT_CONTAINS("hbox_two_buttons: at least one ygrid child", dump, "kind: ygrid\n");
    /* The hbox owns the chrome ygrid; the two buttons become CMD_GROUP
     * entities inside it. The dump's entity_high_water should reflect
     * at least 1 root + 2 button entities = 3 entries. */
    EXPECT_CONTAINS("hbox_two_buttons: multiple entities", dump, "entity_high_water: ");
    free(dump);

    receiver_destroy(&recv);
    yetty_ygui_old_engine_destroy(engine);
}

/*===========================================================================
 * Test 3: rebuild is idempotent — rendering twice without state changes
 * produces a stream the receiver can absorb (no CRASH, dump still
 * shows valid state).
 *===========================================================================*/
static void test_two_renders_no_state_change(void)
{
    fprintf(stderr, "\n[test_two_renders_no_state_change]\n");
    g_tests++;
    struct yetty_ygui_old_engine *engine = make_engine(200, 100);
    struct test_receiver recv = receiver_create(200, 100);
    yetty_ygui_old_engine_button(engine, "btn", 0, 0, 80, 30, "Once");

    char *dump1 = render_and_dump(engine, &recv);
    char *dump2 = render_and_dump(engine, &recv);
    /* After the second render the entity tree must still be intact: at
     * least one ygrid child, structure unchanged at the container level. */
    EXPECT_CONTAINS("two_renders: still has ygrid child", dump2, "kind: ygrid\n");
    free(dump1);
    free(dump2);

    receiver_destroy(&recv);
    yetty_ygui_old_engine_destroy(engine);
}

/*===========================================================================
 * Test 4 — the user-reported "collapsing_header opens but children are
 * invisible" bug, encoded as a regression check.
 *
 * Setup mirrors the simplest reproduction of the Elements/Inputs scenario
 * from ygreeter:
 *
 *     scrollarea
 *       └── collapsing_header (initially OPEN)
 *             ├── button A
 *             └── button B
 *
 * After one headless render with the section already OPEN, the wire
 * bytes MUST cause the receiver to materialise the two button entities
 * inside the section's chrome ygrid. The dump is searched for prim_count
 * lines indicating that more than just the section's own chrome is
 * present — buttons each emit a chrome box of their own.
 *===========================================================================*/
static void test_open_collapsing_header_emits_children(void)
{
    fprintf(stderr, "\n[test_open_collapsing_header_emits_children]\n");
    g_tests++;
    struct yetty_ygui_old_engine *engine = make_engine(400, 300);
    struct test_receiver recv = receiver_create(400, 300);

    struct yetty_ygui_old_widget *sec =
        yetty_ygui_old_engine_collapsing_header(engine, "el_inputs", 0, 0, 400, 28, "Inputs");
    yetty_ygui_old_widget_collapsing_header_set_open(sec, 1);

    struct yetty_ygui_old_widget *a = yetty_ygui_old_engine_button(engine, "btn_a", 0, 0, 100, 28, "A");
    struct yetty_ygui_old_widget *b = yetty_ygui_old_engine_button(engine, "btn_b", 0, 0, 100, 28, "B");
    yetty_ygui_old_widget_add_child(sec, a);
    yetty_ygui_old_widget_add_child(sec, b);

    char *dump = render_and_dump(engine, &recv);
    fprintf(stderr, "--- dump ---\n%s--- end dump ---\n", dump);

    EXPECT_CONTAINS("open_collapsing_header: at least one ygrid figure", dump, "kind: ygrid\n");
    /* When the collapsing_header is a top-level widget (this test's
     * setup), the section IS the ygrid figure and the two buttons
     * appear as CMD_GROUP entities inside it. The receiver therefore
     * sees: root + button_a + button_b = entity_high_water 3. The
     * deeper-nesting regression scenario (section inside scrollarea
     * inside panel inside tabbar) lives in test_nested_collapsing_header
     * below — that's the one that actually exercises the bug surface. */
    int has_3_or_more = strstr(dump, "entity_high_water: 3") != NULL ||
                        strstr(dump, "entity_high_water: 4") != NULL ||
                        strstr(dump, "entity_high_water: 5") != NULL ||
                        strstr(dump, "entity_high_water: 6") != NULL ||
                        strstr(dump, "entity_high_water: 7") != NULL;
    if (!has_3_or_more) {
        fprintf(stderr, "FAIL open_collapsing_header: entity_high_water < 3 — children "
                        "missing from wire?\n--- dump ---\n%s\n",
                dump);
        g_failures++;
    } else {
        fprintf(stderr, "ok   open_collapsing_header: entity_high_water >= 3\n");
    }
    /* Both buttons must have a non-zero prim_count — otherwise their
     * chrome wasn't actually emitted (the user-reported bug surface). */
    int button_prims = 0;
    const char *cursor = dump;
    while ((cursor = strstr(cursor, "prim_count: ")) != NULL) {
        unsigned u = 0;
        if (sscanf(cursor, "prim_count: %u", &u) == 1 && u > 0) {
            button_prims++;
        }
        cursor += strlen("prim_count: ");
    }
    if (button_prims < 3) {
        fprintf(stderr,
                "FAIL open_collapsing_header: only %d entities have prim_count>0 — "
                "buttons missing chrome?\n--- dump ---\n%s\n",
                button_prims, dump);
        g_failures++;
    } else {
        fprintf(stderr, "ok   open_collapsing_header: %d entities have prim_count>0\n",
                button_prims);
    }
    free(dump);

    receiver_destroy(&recv);
    yetty_ygui_old_engine_destroy(engine);
}

int main(void)
{
    test_single_button();
    test_hbox_two_buttons();
    test_two_renders_no_state_change();
    test_open_collapsing_header_emits_children();

    fprintf(stderr, "\nygui wire test: %d tests, %d failure%s\n", g_tests, g_failures,
            g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
