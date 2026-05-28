/*
 * ygui-elements-tab-test.c — regression test that mirrors the structure
 * of ygreeter's Elements tab and drives the open/close transitions on
 * its collapsing-header sections.
 *
 * The ygreeter Elements tab is built like this:
 *
 *     tab_panel (vbox, top-level on the engine)
 *       └── menubar (hbox of menu buttons)
 *       └── el_root (scrollarea, flex-column)
 *             ├── collapsing_header "Inputs"     (initially closed)
 *             │     ├── button
 *             │     ├── button
 *             │     └── button
 *             ├── collapsing_header "Selectors"  (initially closed)
 *             │     └── ...
 *             └── collapsing_header "Display"    (initially closed)
 *                   └── ...
 *
 * The user-reported bug: after clicking a section header to open it,
 * the section's children never appear on the receiver — only after
 * mousing over each one individually does it materialise (because the
 * hover-induced dirty bit triggers an incremental emit).
 *
 * What this test does:
 *   1. Build that exact tree (3 sections × 3 buttons each).
 *   2. Run engine_render_headless and feed the bytes into a
 *      yfigure_container.
 *   3. Assert: at that point ALL sections are closed and the children
 *      are NOT on the receiver — that's the expected baseline.
 *   4. Open "Inputs" via collapsing_header_set_open(open=1).
 *   5. Run engine_render_headless again, feed the (incremental) bytes
 *      into the receiver.
 *   6. Assert: the receiver's ygrid now has entities corresponding to
 *      each Inputs button, each with non-zero prim_count.
 *
 * The test fails LOUDLY if step 6's assertions don't hold — i.e., it
 * reproduces the user's "open-section-shows-empty" symptom.
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
#include "yetty/ygui-old/ygui_internal.h"

static int g_failures = 0;
static int g_tests = 0;
static int g_devnull_fd = -1;

#define FAIL(...)                                                                                  \
    do {                                                                                           \
        fprintf(stderr, "FAIL: " __VA_ARGS__);                                                     \
        fprintf(stderr, "\n");                                                                     \
        g_failures++;                                                                              \
    } while (0)

#define EXPECT(name, cond)                                                                          \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            fprintf(stderr, "FAIL %s\n", (name));                                                   \
            g_failures++;                                                                           \
        } else {                                                                                    \
            fprintf(stderr, "ok   %s\n", (name));                                                   \
        }                                                                                          \
    } while (0)

/*===========================================================================
 * Engine + receiver setup
 *===========================================================================*/

static struct yetty_ygui_old_engine *make_engine(float w, float h)
{
    struct ygui_engine_ptr_result r = yetty_ygui_old_engine_internal_alloc("elements-test", NULL);
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

struct receiver {
    struct yetty_yfigure_registry *registry;
    struct yetty_yfigure_container *root;
};

static struct receiver receiver_create(float w, float h)
{
    struct yetty_yfigure_registry_ptr_result rr = yetty_yfigure_registry_create();
    if (YETTY_IS_ERR(rr)) {
        fprintf(stderr, "registry_create failed\n");
        yetty_ycore_error_destroy(rr.error);
        exit(2);
    }
    static const struct yetty_ygrid_factory_args args = {.default_font = NULL,
                                                          .figure_factory = NULL};
    struct yetty_ycore_void_result rf = yetty_ygrid_register_factory(rr.value, &args);
    if (YETTY_IS_ERR(rf)) {
        fprintf(stderr, "ygrid_register_factory failed\n");
        yetty_ycore_error_destroy(rf.error);
        exit(2);
    }
    struct yetty_ycore_rectangle rect = {{0, 0}, {w, h}};
    struct yetty_yfigure_container_ptr_result cr =
        yetty_yfigure_container_create_local(rect, NULL, rr.value);
    if (YETTY_IS_ERR(cr)) {
        fprintf(stderr, "container_create failed\n");
        yetty_ycore_error_destroy(cr.error);
        exit(2);
    }
    struct receiver out = {.registry = rr.value, .root = cr.value};
    return out;
}

static void receiver_destroy(struct receiver *r)
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

/* Run one headless render on the engine, push the bytes into the
 * receiver, and return the receiver's dump (caller frees). */
static char *render_step(struct yetty_ygui_old_engine *engine, struct receiver *recv,
                         const char *label)
{
    struct yetty_ycore_void_result r = yetty_ygui_old_engine_render_headless(engine);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "engine_render_headless failed at %s: %s\n", label, r.error.msg);
        yetty_ycore_error_destroy(r.error);
        exit(3);
    }
    const uint8_t *bytes = (const uint8_t *)yetty_ygui_old_engine_buffer_data(engine);
    size_t len = yetty_ygui_old_engine_buffer_size(engine);
    fprintf(stderr, "  [%s] wire bytes = %zu\n", label, len);
    if (len > 0 && bytes) {
        struct yetty_ycore_void_result pr =
            yetty_yfigure_container_process_records(recv->root, bytes, len);
        if (YETTY_IS_ERR(pr)) {
            fprintf(stderr, "container_process_records failed at %s: %s\n", label, pr.error.msg);
            yetty_ycore_error_destroy(pr.error);
            exit(3);
        }
    }
    return yetty_yfigure_dump(yetty_yfigure_container_as_figure(recv->root), 0);
}

/*===========================================================================
 * Helpers — count entities with non-zero prim_count, count entities total.
 *===========================================================================*/

static int count_substr(const char *hay, const char *needle)
{
    int n = 0;
    const char *p = hay;
    size_t nlen = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += nlen;
    }
    return n;
}

/* Count the number of "prim_count: N" occurrences where N > 0. */
static int count_nonzero_prim_counts(const char *dump)
{
    int n = 0;
    const char *p = dump;
    while ((p = strstr(p, "prim_count: ")) != NULL) {
        unsigned u = 0;
        if (sscanf(p, "prim_count: %u", &u) == 1 && u > 0) {
            n++;
        }
        p += strlen("prim_count: ");
    }
    return n;
}

/*===========================================================================
 * Build the Elements-tab tree on the engine. Returns the array of section
 * widgets (caller-owned only by the engine itself; they're freed when the
 * engine is destroyed). `out_sections` must hold at least 3 entries.
 *===========================================================================*/
static void build_elements_tab(struct yetty_ygui_old_engine *engine,
                                struct yetty_ygui_old_widget *out_sections[3])
{
    /* Tab panel — full-canvas vbox, top-level on the engine. */
    struct yetty_ygui_old_widget *panel = yetty_ygui_old_engine_vbox(engine, "tab_panel", 0, 0, 0, 0);

    /* Menubar — a hbox sized to the canvas width. */
    struct yetty_ygui_old_widget *menubar = yetty_ygui_old_engine_hbox(engine, "menubar", 0, 0, 0, 28);
    yetty_ygui_old_widget_apply_css(menubar, "align-self: stretch;");
    yetty_ygui_old_widget_add_child(panel, menubar);

    /* Scrollarea — fills the rest of the panel. */
    struct yetty_ygui_old_widget *root =
        yetty_ygui_old_engine_scrollarea(engine, "el_root", 0, 0, 0, 0);
    yetty_ygui_old_widget_apply_css(root,
                                "padding: 12px; gap: 4px; flex: 1 0 0; "
                                "align-self: stretch; align-items: stretch;");
    yetty_ygui_old_widget_add_child(panel, root);

    static const char *section_ids[3] = {"el_inputs", "el_select", "el_display"};
    static const char *section_labels[3] = {"Inputs", "Selectors", "Display"};
    static const char *button_id_prefix[3] = {"in_", "sel_", "disp_"};

    for (int i = 0; i < 3; i++) {
        struct yetty_ygui_old_widget *sec = yetty_ygui_old_engine_collapsing_header(
            engine, section_ids[i], 0, 0, 600, 28, section_labels[i]);
        yetty_ygui_old_widget_collapsing_header_set_open(sec, 0); /* start closed */
        yetty_ygui_old_widget_apply_css(sec, "align-self: stretch;");
        yetty_ygui_old_widget_add_child(root, sec);

        /* Three buttons per section. */
        for (int j = 0; j < 3; j++) {
            char id[32];
            snprintf(id, sizeof(id), "%sbtn%d", button_id_prefix[i], j);
            struct yetty_ygui_old_widget *btn =
                yetty_ygui_old_engine_button(engine, id, 0, 0, 120, 28, id);
            yetty_ygui_old_widget_add_child(sec, btn);
        }
        out_sections[i] = sec;
    }
}

/*===========================================================================
 * The big regression test.
 *===========================================================================*/

static void test_elements_tab_open_section(void)
{
    fprintf(stderr, "\n[test_elements_tab_open_section]\n");
    g_tests++;

    struct yetty_ygui_old_engine *engine = make_engine(800, 600);
    struct receiver recv = receiver_create(800, 600);
    struct yetty_ygui_old_widget *sections[3] = {0};
    build_elements_tab(engine, sections);

    /* --- Step 1: initial render with all sections closed --- */
    char *dump1 = render_step(engine, &recv, "initial-all-closed");
    fprintf(stderr, "--- dump after initial render ---\n%s\n", dump1);

    /* The receiver should have at least one top-level child — the
     * tab_panel's chrome ygrid. The exact count depends on how ygui
     * organises figures at depth=0, but there must be SOMETHING. */
    int n_top_children_initial = count_substr(dump1, "kind: ygrid\n");
    EXPECT("initial-all-closed: at least one ygrid figure", n_top_children_initial >= 1);

    /* Closed sections paint only their header strip; their children
     * never get visited. Count how many entities have non-zero prim
     * count — that tells us how many widgets actually emitted chrome. */
    int prims_initial = count_nonzero_prim_counts(dump1);
    fprintf(stderr, "  initial non-zero prim entities = %d\n", prims_initial);

    /* --- Step 2: open the Inputs section --- */
    yetty_ygui_old_widget_collapsing_header_set_open(sections[0], 1);

    char *dump2 = render_step(engine, &recv, "after-open-inputs");
    fprintf(stderr, "--- dump after opening Inputs ---\n%s\n", dump2);

    int prims_after_open = count_nonzero_prim_counts(dump2);
    fprintf(stderr, "  after-open non-zero prim entities = %d\n", prims_after_open);

    /* The bug: after opening, the three buttons inside Inputs should
     * each emit their chrome (one or more prims) as new CMD_GROUP
     * entities. If the receiver-side ygrid doesn't gain at least 3
     * more entities with non-zero prim_count, the buttons are missing
     * from the wire — that's the user-reported symptom. */
    int new_prim_entities = prims_after_open - prims_initial;
    fprintf(stderr, "  new non-zero prim entities = %d (expect >= 3 buttons)\n",
            new_prim_entities);
    EXPECT("after-open-inputs: >= 3 new entities with prims", new_prim_entities >= 3);

    /* Also check entity_high_water grew. */
    int hw_initial = 0, hw_after = 0;
    if (sscanf(strstr(dump1, "entity_high_water: "), "entity_high_water: %d", &hw_initial) != 1) {
        hw_initial = -1;
    }
    if (sscanf(strstr(dump2, "entity_high_water: "), "entity_high_water: %d", &hw_after) != 1) {
        hw_after = -1;
    }
    fprintf(stderr, "  entity_high_water: initial=%d after=%d\n", hw_initial, hw_after);
    EXPECT("after-open-inputs: entity_high_water grew", hw_after > hw_initial);

    /* --- Step 3: close Inputs again --- */
    yetty_ygui_old_widget_collapsing_header_set_open(sections[0], 0);
    char *dump3 = render_step(engine, &recv, "after-close-inputs");
    fprintf(stderr, "--- dump after closing Inputs ---\n%s\n", dump3);

    /* When the section closes, the producer queues delete records for
     * the children. After this render the receiver should drop the
     * entities for those buttons — otherwise the user sees stale
     * widgets "in the background" (the other half of the bug). */
    int prims_after_close = count_nonzero_prim_counts(dump3);
    fprintf(stderr, "  after-close non-zero prim entities = %d\n", prims_after_close);
    EXPECT("after-close-inputs: prim entities back near baseline",
           prims_after_close <= prims_initial + 1);

    /* --- Step 4: re-open Inputs --- */
    yetty_ygui_old_widget_collapsing_header_set_open(sections[0], 1);
    char *dump4 = render_step(engine, &recv, "after-reopen-inputs");
    fprintf(stderr, "--- dump after reopening Inputs ---\n%s\n", dump4);

    int prims_after_reopen = count_nonzero_prim_counts(dump4);
    fprintf(stderr, "  after-reopen non-zero prim entities = %d\n", prims_after_reopen);
    EXPECT("after-reopen-inputs: button chrome present again",
           prims_after_reopen >= prims_initial + 3);

    free(dump1);
    free(dump2);
    free(dump3);
    free(dump4);
    receiver_destroy(&recv);
    yetty_ygui_old_engine_destroy(engine);
}

/*===========================================================================
 * Richer regression test: real ygreeter Inputs section widget mix
 * (button, textinput, slider, spinner_i, spinner_f, checkbox, toggle,
 * progress, progress-indeterminate, textarea) — 10 widgets in one
 * section. Then open and verify every widget produces visible chrome
 * (entity with prim_count > 0) on the receiver.
 *===========================================================================*/
static void test_inputs_section_full_mix(void)
{
    fprintf(stderr, "\n[test_inputs_section_full_mix]\n");
    g_tests++;
    struct yetty_ygui_old_engine *engine = make_engine(800, 600);
    struct receiver recv = receiver_create(800, 600);

    struct yetty_ygui_old_widget *panel = yetty_ygui_old_engine_vbox(engine, "tab_panel", 0, 0, 0, 0);
    struct yetty_ygui_old_widget *sa = yetty_ygui_old_engine_scrollarea(engine, "sa", 0, 0, 0, 0);
    yetty_ygui_old_widget_apply_css(sa, "padding: 12px; gap: 4px; flex: 1 0 0; align-self: stretch;");
    yetty_ygui_old_widget_add_child(panel, sa);

    struct yetty_ygui_old_widget *sec =
        yetty_ygui_old_engine_collapsing_header(engine, "el_inputs", 0, 0, 600, 28, "Inputs");
    yetty_ygui_old_widget_collapsing_header_set_open(sec, 0);
    yetty_ygui_old_widget_apply_css(sec, "align-self: stretch;");
    yetty_ygui_old_widget_add_child(sa, sec);

    /* Mirror build_elements_tab(...)'s Inputs section exactly. */
    yetty_ygui_old_widget_add_child(
        sec, yetty_ygui_old_engine_button(engine, "el_btn", 24, 0, 160, 32, "Button"));
    yetty_ygui_old_widget_add_child(
        sec, yetty_ygui_old_engine_textinput(engine, "el_input", 24, 0, 320, 28, "type here…"));
    yetty_ygui_old_widget_add_child(
        sec, yetty_ygui_old_engine_slider(engine, "el_slider", 24, 0, 320, 28, 0.0f, 1.0f, 0.4f));
    yetty_ygui_old_widget_add_child(
        sec, yetty_ygui_old_engine_spinner(engine, "el_spin_i", 24, 0, 160, 30, 1.0f, 100.0f, 1.0f,
                                       42.0f));
    yetty_ygui_old_widget_add_child(
        sec, yetty_ygui_old_engine_spinner(engine, "el_spin_f", 24, 0, 160, 30, 0.0f, 10.0f, 0.25f,
                                       2.5f));
    yetty_ygui_old_widget_add_child(
        sec, yetty_ygui_old_engine_checkbox(engine, "el_check", 24, 0, 220, 24, "Enabled", 1));
    yetty_ygui_old_widget_add_child(
        sec, yetty_ygui_old_engine_toggle(engine, "el_toggle", 24, 0, 240, 26, "Notif", 1));
    yetty_ygui_old_widget_add_child(
        sec, yetty_ygui_old_engine_progress(engine, "el_prog", 24, 0, 320, 16, 0.65f));
    yetty_ygui_old_widget_add_child(
        sec, yetty_ygui_old_engine_progress(engine, "el_prog_indet", 24, 0, 320, 16, 0.0f));
    yetty_ygui_old_widget_add_child(
        sec, yetty_ygui_old_engine_textarea(engine, "el_ta", 24, 0, 420, 120, "Multi\nline\n"));

    char *dump1 = render_step(engine, &recv, "init-closed");
    int prims_initial = count_nonzero_prim_counts(dump1);
    int hw_initial = 0;
    sscanf(strstr(dump1, "entity_high_water: "), "entity_high_water: %d", &hw_initial);
    fprintf(stderr, "  initial: non-zero-prim entities = %d, entity_high_water = %d\n",
            prims_initial, hw_initial);

    /* Open the section. */
    yetty_ygui_old_widget_collapsing_header_set_open(sec, 1);
    char *dump2 = render_step(engine, &recv, "after-open");

    int prims_after_open = count_nonzero_prim_counts(dump2);
    int hw_after_open = 0;
    sscanf(strstr(dump2, "entity_high_water: "), "entity_high_water: %d", &hw_after_open);
    fprintf(stderr, "  after-open: non-zero-prim entities = %d, entity_high_water = %d\n",
            prims_after_open, hw_after_open);

    /* Each of the 10 widgets should produce an entity with prim_count > 0
     * after the section opens. The section's chrome also re-emits (prim
     * count goes from "closed chrome" to "open chrome"), so the delta is
     * at least 10 new prim-bearing entities. */
    int delta = prims_after_open - prims_initial;
    fprintf(stderr, "  delta = %d (expect >= 10 widgets)\n", delta);
    EXPECT("inputs_full_mix: 10+ new widget entities after open", delta >= 10);

    /* If we can spot at least 10 button-sized chrome blocks in the entity
     * tree, that confirms the wire delivered the widgets. */
    int big_prim_entities = 0;
    const char *p = dump2;
    while ((p = strstr(p, "prim_count: ")) != NULL) {
        unsigned u = 0;
        if (sscanf(p, "prim_count: %u", &u) == 1 && u >= 2) {
            big_prim_entities++;
        }
        p += strlen("prim_count: ");
    }
    fprintf(stderr, "  entities with prim_count>=2 = %d\n", big_prim_entities);
    EXPECT("inputs_full_mix: every widget has chrome (>=2 prims)", big_prim_entities >= 10);

    /* Hexdump the raw bytes of the most recent open render to make wire
     * regressions visible at a glance — useful when the receiver
     * believes it has the entities but the user reports the screen is
     * empty. */
    const uint8_t *bytes = (const uint8_t *)yetty_ygui_old_engine_buffer_data(engine);
    size_t len = yetty_ygui_old_engine_buffer_size(engine);
    fprintf(stderr, "  open-render wire bytes = %zu (first 64):", len);
    for (size_t i = 0; i < len && i < 64; i++) {
        if (i % 16 == 0) fprintf(stderr, "\n    ");
        fprintf(stderr, "%02x ", bytes[i]);
    }
    fprintf(stderr, "\n");

    free(dump1);
    free(dump2);
    receiver_destroy(&recv);
    yetty_ygui_old_engine_destroy(engine);
}

/*===========================================================================
 * Closest-to-ygreeter test: the section's enclosing chrome is a TABBAR
 * (panels are children of the tabbar widget, only the active panel is
 * VISIBLE) — same shape as the real ygreeter Elements layout. The
 * "Elements" panel sits at tab index 1 here; the test switches to it
 * before opening Inputs so the activation path mirrors what
 * tabbar_set_active does in ygreeter (`set_visible(prev, 0)` →
 * `set_visible(new, 1)`).
 *===========================================================================*/
static void test_tabbar_wrapped_elements_tab(void)
{
    fprintf(stderr, "\n[test_tabbar_wrapped_elements_tab]\n");
    g_tests++;
    struct yetty_ygui_old_engine *engine = make_engine(800, 600);
    struct receiver recv = receiver_create(800, 600);

    /* Tabbar at the top — the ONLY top-level widget the engine sees,
     * just like real ygreeter. Other panels live inside it as children. */
    struct yetty_ygui_old_widget *tabbar = yetty_ygui_old_engine_tabbar(engine, "tabbar", 0, 0, 800, 600);

    struct yetty_ygui_old_widget *welcome_panel =
        yetty_ygui_old_widget_tabbar_add_tab(tabbar, "Welcome");
    yetty_ygui_old_widget_add_child(
        welcome_panel,
        yetty_ygui_old_engine_label(engine, "welcome_lbl", 0, 0, "Welcome"));

    struct yetty_ygui_old_widget *elements_panel =
        yetty_ygui_old_widget_tabbar_add_tab(tabbar, "Elements");
    /* Inside Elements: menubar + scrollarea + collapsing_header w/ buttons. */
    struct yetty_ygui_old_widget *mb = yetty_ygui_old_engine_hbox(engine, "mb", 0, 0, 0, 28);
    yetty_ygui_old_widget_apply_css(mb, "align-self: stretch;");
    yetty_ygui_old_widget_add_child(elements_panel, mb);

    struct yetty_ygui_old_widget *sa = yetty_ygui_old_engine_scrollarea(engine, "sa", 0, 0, 0, 0);
    yetty_ygui_old_widget_apply_css(sa, "padding: 12px; gap: 4px; flex: 1 0 0; align-self: stretch;");
    yetty_ygui_old_widget_add_child(elements_panel, sa);

    struct yetty_ygui_old_widget *sec =
        yetty_ygui_old_engine_collapsing_header(engine, "el_inputs", 0, 0, 600, 28, "Inputs");
    yetty_ygui_old_widget_collapsing_header_set_open(sec, 0);
    yetty_ygui_old_widget_apply_css(sec, "align-self: stretch;");
    yetty_ygui_old_widget_add_child(sa, sec);

    /* Mirror build_elements_tab(...)'s Inputs section. */
    yetty_ygui_old_widget_add_child(
        sec, yetty_ygui_old_engine_button(engine, "el_btn", 24, 0, 160, 32, "Button"));
    yetty_ygui_old_widget_add_child(
        sec, yetty_ygui_old_engine_textinput(engine, "el_input", 24, 0, 320, 28, "type here…"));
    yetty_ygui_old_widget_add_child(
        sec, yetty_ygui_old_engine_slider(engine, "el_slider", 24, 0, 320, 28, 0.0f, 1.0f, 0.4f));
    yetty_ygui_old_widget_add_child(
        sec, yetty_ygui_old_engine_checkbox(engine, "el_check", 24, 0, 220, 24, "Enabled", 1));
    yetty_ygui_old_widget_add_child(
        sec, yetty_ygui_old_engine_toggle(engine, "el_toggle", 24, 0, 240, 26, "Notif", 1));
    yetty_ygui_old_widget_add_child(
        sec, yetty_ygui_old_engine_progress(engine, "el_prog", 24, 0, 320, 16, 0.65f));

    /* Activate the Elements tab (index 1) — same path as ygreeter's
     * YGREETER_START_ELEMENTS=1 hook: tabbar_set_active. */
    yetty_ygui_old_widget_tabbar_set_active(tabbar, 1);

    /* --- Step 1: initial render. Section is closed. --- */
    char *dump1 = render_step(engine, &recv, "init-elements-active");
    int prims_initial = count_nonzero_prim_counts(dump1);
    int hw_initial = 0;
    sscanf(strstr(dump1, "entity_high_water: "), "entity_high_water: %d", &hw_initial);
    fprintf(stderr, "  initial: non-zero-prim entities = %d, entity_high_water = %d\n",
            prims_initial, hw_initial);

    /* --- Step 2: open Inputs section --- */
    yetty_ygui_old_widget_collapsing_header_set_open(sec, 1);
    char *dump2 = render_step(engine, &recv, "after-open-inputs");
    fprintf(stderr, "--- dump after open ---\n%s--- end ---\n", dump2);
    int prims_after_open = count_nonzero_prim_counts(dump2);
    int hw_after_open = 0;
    sscanf(strstr(dump2, "entity_high_water: "), "entity_high_water: %d", &hw_after_open);
    fprintf(stderr, "  after-open: non-zero-prim entities = %d, entity_high_water = %d\n",
            prims_after_open, hw_after_open);

    int delta = prims_after_open - prims_initial;
    fprintf(stderr, "  delta = %d (expect >= 6 widgets)\n", delta);
    EXPECT("tabbar_wrapped: 6+ new widget entities after open", delta >= 6);

    /* Each widget's chrome should be at least 2 SDF prims (shadow + bg). */
    int big_prim_entities = 0;
    const char *p = dump2;
    while ((p = strstr(p, "prim_count: ")) != NULL) {
        unsigned u = 0;
        if (sscanf(p, "prim_count: %u", &u) == 1 && u >= 2) {
            big_prim_entities++;
        }
        p += strlen("prim_count: ");
    }
    fprintf(stderr, "  entities with prim_count>=2 = %d (expect >= 6)\n", big_prim_entities);
    EXPECT("tabbar_wrapped: every widget has chrome (>=2 prims)", big_prim_entities >= 6);

    /* --- Step 3: close Inputs --- */
    yetty_ygui_old_widget_collapsing_header_set_open(sec, 0);
    char *dump3 = render_step(engine, &recv, "after-close-inputs");
    int prims_after_close = count_nonzero_prim_counts(dump3);
    fprintf(stderr, "  after-close: non-zero-prim entities = %d (expect <= initial+1)\n",
            prims_after_close);
    EXPECT("tabbar_wrapped: widgets gone after close",
           prims_after_close <= prims_initial + 1);

    /* --- Step 4: reopen --- */
    yetty_ygui_old_widget_collapsing_header_set_open(sec, 1);
    char *dump4 = render_step(engine, &recv, "after-reopen-inputs");
    int prims_after_reopen = count_nonzero_prim_counts(dump4);
    fprintf(stderr, "  after-reopen: non-zero-prim entities = %d\n", prims_after_reopen);
    EXPECT("tabbar_wrapped: widgets back after reopen", prims_after_reopen >= prims_initial + 6);

    free(dump1);
    free(dump2);
    free(dump3);
    free(dump4);
    receiver_destroy(&recv);
    yetty_ygui_old_engine_destroy(engine);
}

int main(void)
{
    test_elements_tab_open_section();
    test_inputs_section_full_mix();
    test_tabbar_wrapped_elements_tab();
    fprintf(stderr, "\nygui elements-tab test: %d tests, %d failure%s\n", g_tests, g_failures,
            g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
