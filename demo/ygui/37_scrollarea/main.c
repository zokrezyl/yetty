/*
 * Demo 37_scrollarea: Scrollarea — a tabbar of scrollbar scenarios.
 *
 * Standalone-mode ygui demo. The runner brings up window + GPU +
 * receiver-side container; this file only populates the widget tree.
 *
 * A tabbar across the top switches between four scenarios, each a
 * scrollarea fed different content so the draggable scrollbar can be
 * exercised in the situations it actually has to handle:
 *
 *   1. Multi-line text — a tall block of styled `rich` text; drag to
 *      page through paragraphs that don't fit the viewport.
 *   2. Widget group    — a settings-style stack of headings, buttons and
 *      checkboxes that overruns its area; scroll to reach them all while
 *      the buttons/checkboxes stay clickable.
 *   3. Long list       — 60 rows; content far taller than the viewport,
 *      so the thumb is short and travels the full track.
 *   4. Fits            — 4 rows; content shorter than the viewport, so
 *      the thumb fills the track and nothing scrolls (the contrast case).
 *
 * Tab switching toggles each scene's visibility off a VALUE_CHANGED
 * subscription on the tabbar. Scrolled content is culled to the
 * scrollarea's client area by the emit walk (a CPU stand-in for a GPU
 * scissor), so nothing spills over the tabbar; the cull is row/widget
 * granular, so edges step rather than pixel-clip. Press 'q' (or Ctrl-C /
 * Ctrl-D) to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runner.h"
#include <yetty/ygui/event.h>
#include <yetty/ygui/ygui.h>

#define COLOR_VIEWPORT 0xFF15110Du /* dark panel so the scroll viewport is visible */
#define COLOR_ACCENT 0xFF92A86Bu
#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_MUTED 0xFFA8A79Fu

/* Outer scene containers, toggled in/out of view as tabs change. The
 * demo is a single instance, so file-static state is fine here. */
static struct {
    struct yetty_yclass_object *scenes[8];
    int count;
} g;

static inline void err_ok(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/* Instantiate `cls` under `parent`, swallowing (and freeing) any error so
 * the build keeps going — a demo wants best-effort, not abort-on-first. */
static struct yetty_yclass_object *add_obj(struct yetty_yclass_object *parent,
                                         struct yetty_yclass_ptr_result cls)
{
    if (YETTY_IS_ERR(cls)) {
        yetty_ycore_error_destroy(cls.error);
        return NULL;
    }
    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_add(parent, cls.value);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    return r.value;
}

/* Author width/height; pass a negative value to leave that axis unset. */
static void set_size(struct yetty_yclass_object *o, float w, float h)
{
    if (!o) {
        return;
    }
    struct yetty_ygui_layout_const_ptr_result layout_res =
        yetty_ygui_widget_layout_get(o);
    if (YETTY_IS_ERR(layout_res)) {
        yetty_ycore_error_destroy(layout_res.error);
        return;
    }
    struct yetty_ygui_layout l = *layout_res.value;
    if (w >= 0.0f) {
        l.width = w;
    }
    if (h >= 0.0f) {
        l.height = h;
    }
    err_ok(yetty_ygui_widget_layout_set(o, &l));
}

static void set_grow(struct yetty_yclass_object *o, float grow)
{
    if (!o) {
        return;
    }
    struct yetty_ygui_layout_const_ptr_result layout_res =
        yetty_ygui_widget_layout_get(o);
    if (YETTY_IS_ERR(layout_res)) {
        yetty_ycore_error_destroy(layout_res.error);
        return;
    }
    struct yetty_ygui_layout l = *layout_res.value;
    l.flex_grow = grow;
    err_ok(yetty_ygui_widget_layout_set(o, &l));
}

static void set_gap(struct yetty_yclass_object *o, float gap)
{
    if (!o) {
        return;
    }
    struct yetty_ygui_layout_const_ptr_result layout_res =
        yetty_ygui_widget_layout_get(o);
    if (YETTY_IS_ERR(layout_res)) {
        yetty_ycore_error_destroy(layout_res.error);
        return;
    }
    struct yetty_ygui_layout l = *layout_res.value;
    l.gap = gap;
    err_ok(yetty_ygui_widget_layout_set(o, &l));
}

/* Reveal exactly one scene, fold the rest away. */
static void show_scene(int idx)
{
    for (int i = 0; i < g.count; i++) {
        err_ok(yetty_ygui_widget_set_visible(g.scenes[i], i == idx));
    }
}

static struct yetty_ycore_void_result on_tab_changed(
                                                     struct yetty_yclass_object *target,
                                                     const struct yetty_ygui_event *event,
                                                     void *userdata)
{
    (void)target;
    (void)userdata;
    int idx = event ? event->i0 : 0;
    if (idx < 0) {
        idx = 0;
    }
    if (idx >= g.count) {
        idx = g.count - 1;
    }
    show_scene(idx);
    return YETTY_OK_VOID();
}

/* Begin a scene: an outer column holding a one-line description above a
 * flex-filling scrollarea. Registers the outer column for tab toggling
 * and returns the scrollarea for the caller to populate. */
static struct yetty_yclass_object *begin_scene(struct yetty_yclass_object *content, const char *desc)
{
    struct yetty_yclass_object *scene = add_obj(content, yetty_ygui_vbox_class_get());
    if (!scene) {
        return NULL;
    }
    set_grow(scene, 1.0f);
    set_gap(scene, 6.0f);

    struct yetty_yclass_object *label = add_obj(scene, yetty_ygui_label_class_get());
    if (label) {
        err_ok(yetty_ygui_label_set_text(label, desc));
        err_ok(yetty_ygui_label_set_font_size(label, 13.0f));
        set_size(label, -1.0f, 22.0f);
    }

    struct yetty_yclass_object *scroll = add_obj(scene, yetty_ygui_scrollarea_class_get());
    if (scroll) {
        set_grow(scroll, 1.0f);
    }
    if (g.count < (int)(sizeof(g.scenes) / sizeof(g.scenes[0]))) {
        g.scenes[g.count++] = scene;
    }
    return scroll;
}

static void add_label_row(struct yetty_yclass_object *scroll, const char *text)
{
    struct yetty_yclass_object *row = add_obj(scroll, yetty_ygui_label_class_get());
    if (!row) {
        return;
    }
    err_ok(yetty_ygui_label_set_text(row, text));
    set_size(row, -1.0f, 24.0f);
}

static void add_button_row(struct yetty_yclass_object *scroll, const char *text)
{
    struct yetty_yclass_object *row = add_obj(scroll, yetty_ygui_button_class_get());
    if (!row) {
        return;
    }
    err_ok(yetty_ygui_button_set_label(row, text));
    set_size(row, -1.0f, 28.0f);
}

static void add_checkbox_row(struct yetty_yclass_object *scroll, const char *text)
{
    struct yetty_yclass_object *row = add_obj(scroll, yetty_ygui_checkbox_class_get());
    if (!row) {
        return;
    }
    err_ok(yetty_ygui_checkbox_set_label(row, text));
    set_size(row, -1.0f, 24.0f);
}

/* Scene 1 — a tall multi-line styled text block inside the scrollarea. */
static void build_text_scene(struct yetty_yclass_object *content)
{
    struct yetty_yclass_object *scroll =
        begin_scene(content, "Multi-line text — drag to page through paragraphs");
    if (!scroll) {
        return;
    }
    struct yetty_yclass_object *rich = add_obj(scroll, yetty_ygui_rich_class_get());
    if (!rich) {
        return;
    }
    const int lines = 48;
    char buf[16];
    for (int i = 1; i <= lines; i++) {
        err_ok(yetty_ygui_rich_add_line(rich));
        snprintf(buf, sizeof(buf), "%2d  ", i);
        err_ok(yetty_ygui_rich_add_span(rich, buf, 15.0f, COLOR_ACCENT));
        err_ok(yetty_ygui_rich_add_span(
            rich, "The quick brown fox jumps over the lazy dog while the ", 15.0f, COLOR_TEXT));
        err_ok(yetty_ygui_rich_add_span(rich, "scrollbar", 15.0f, COLOR_ACCENT));
        err_ok(yetty_ygui_rich_add_span(rich, " keeps the rest in reach.", 15.0f, COLOR_MUTED));
    }
    /* The rich block is one child; give it the height its lines occupy so
     * the scrollarea sees the overflow. */
    set_size(rich, -1.0f, (float)lines * 22.0f);
}

/* Scene 2 — a settings-style group of widgets that overruns its area. */
static void build_group_scene(struct yetty_yclass_object *content)
{
    struct yetty_yclass_object *scroll =
        begin_scene(content, "Widget group — scroll to reach controls; buttons stay clickable");
    if (!scroll) {
        return;
    }
    const char *sections[] = {"Appearance", "Network", "Privacy", "Advanced", "About"};
    char buf[48];
    for (size_t s = 0; s < sizeof(sections) / sizeof(sections[0]); s++) {
        struct yetty_yclass_object *heading = add_obj(scroll, yetty_ygui_label_class_get());
        if (heading) {
            err_ok(yetty_ygui_label_set_text(heading, sections[s]));
            err_ok(yetty_ygui_label_set_font_size(heading, 15.0f));
            set_size(heading, -1.0f, 28.0f);
        }
        for (int i = 1; i <= 3; i++) {
            snprintf(buf, sizeof(buf), "%s option %d", sections[s], i);
            add_checkbox_row(scroll, buf);
        }
        snprintf(buf, sizeof(buf), "Apply %s", sections[s]);
        add_button_row(scroll, buf);
    }
}

/* Scene 3 — a long flat list: short thumb, full-track travel. */
static void build_list_scene(struct yetty_yclass_object *content)
{
    struct yetty_yclass_object *scroll =
        begin_scene(content, "Long list — 60 rows; short thumb travels the whole track");
    if (!scroll) {
        return;
    }
    char buf[24];
    for (int i = 1; i <= 60; i++) {
        snprintf(buf, sizeof(buf), "List item %02d", i);
        add_label_row(scroll, buf);
    }
}

/* Scene 4 — content that fits: full-height thumb, nothing scrolls. */
static void build_fits_scene(struct yetty_yclass_object *content)
{
    struct yetty_yclass_object *scroll =
        begin_scene(content, "Fits — content shorter than the viewport, thumb fills the track");
    if (!scroll) {
        return;
    }
    char buf[24];
    for (int i = 1; i <= 4; i++) {
        snprintf(buf, sizeof(buf), "Row %d of 4", i);
        add_label_row(scroll, buf);
    }
}

/* Scene 5 — three scrollables nested. The outer scrolls; the middle box
 * (fixed height inside the outer) scrolls its own content; the inner box
 * (fixed height inside the middle) scrolls too. Each is its own ygrid
 * figure and clips to the intersection of its ancestors' boxes. */
static void build_nested_scene(struct yetty_yclass_object *content)
{
    struct yetty_yclass_object *outer =
        begin_scene(content, "Nested x3 — outer scrolls; the middle and inner boxes scroll too");
    if (!outer) {
        return;
    }
    char buf[32];
    for (int i = 1; i <= 5; i++) {
        snprintf(buf, sizeof(buf), "Outer row %02d", i);
        add_label_row(outer, buf);
    }
    struct yetty_yclass_object *middle = add_obj(outer, yetty_ygui_scrollarea_class_get());
    if (middle) {
        set_size(middle, -1.0f, 320.0f); /* fixed box inside the outer */
        for (int i = 1; i <= 5; i++) {
            snprintf(buf, sizeof(buf), "Middle row %02d", i);
            add_label_row(middle, buf);
        }
        struct yetty_yclass_object *inner = add_obj(middle, yetty_ygui_scrollarea_class_get());
        if (inner) {
            set_size(inner, -1.0f, 140.0f); /* fixed box inside the middle */
            for (int i = 1; i <= 30; i++) {
                snprintf(buf, sizeof(buf), "Inner row %02d", i);
                add_label_row(inner, buf);
            }
        }
        for (int i = 6; i <= 14; i++) {
            snprintf(buf, sizeof(buf), "Middle row %02d", i);
            add_label_row(middle, buf);
        }
    }
    for (int i = 6; i <= 22; i++) {
        snprintf(buf, sizeof(buf), "Outer row %02d", i);
        add_label_row(outer, buf);
    }
}

static struct yetty_ycore_void_result build(struct demo_runner *runner,
                                            struct yetty_yclass_object *root)
{
    (void)runner;
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "37_scrollarea: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.direction = YETTY_YGUI_FLEX_COLUMN;
        l.gap = 8.0f;
        l.padding_left = l.padding_right = l.padding_top = l.padding_bottom = 10.0f;
        err_ok(yetty_ygui_widget_layout_set(root, &l));
    }

    struct yetty_yclass_object *tabbar = add_obj(root, yetty_ygui_tabbar_class_get());
    if (tabbar) {
        set_size(tabbar, -1.0f, 36.0f);
        /* add_tab returns the header widget in a result; the demo doesn't
         * need the handle, so just free any error and move on. */
        const char *labels[] = {"Multi-line text", "Widget group", "Long list", "Fits",
                                "Nested x3"};
        for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
            struct yetty_yclass_object_ptr_result t = yetty_ygui_tabbar_add_tab(tabbar, labels[i]);
            if (YETTY_IS_ERR(t)) {
                yetty_ycore_error_destroy(t.error);
            }
        }
    }

    struct yetty_yclass_object *content = add_obj(root, yetty_ygui_vbox_class_get());
    if (!content) {
        return YETTY_ERR(yetty_ycore_void, "scrollarea demo: content container");
    }
    set_grow(content, 1.0f);

    build_text_scene(content);
    build_group_scene(content);
    build_list_scene(content);
    build_fits_scene(content);
    build_nested_scene(content);

    show_scene(0);
    if (tabbar) {
        err_ok(yetty_ygui_widget_subscribe(tabbar, YETTY_YGUI_EVENT_VALUE_CHANGED, on_tab_changed,
                                           NULL));
        err_ok(yetty_ygui_tabbar_set_active(tabbar, 0));
    }
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return demo_runner_run(argc, argv, "37_scrollarea", build);
}
