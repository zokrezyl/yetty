/*
 * Demo 43_tree_showcase: Tree view usages — plain, nested, and scrolled.
 *
 * Standalone-mode ygui demo. The runner brings up window + GPU +
 * receiver-side container; this file only populates the widget tree.
 *
 * Three scenarios switched by a tabbar:
 *
 *   1. File explorer — a deep, nested project tree; open/close folders
 *      to walk the hierarchy. Exercises tree_node nesting + indentation.
 *   2. Scrolled tree — a tall tree inside a scrollarea whose content
 *      overruns the viewport. Scroll a collapsed folder to the middle of
 *      the viewport, then open it: the revealed children must appear
 *      directly under the folder, and clicking a header must toggle the
 *      folder actually under the pointer (issue #79).
 *   3. Live detail — a tree whose toggle callback updates a side label,
 *      showing tree_node on_toggle wiring.
 *
 * Press 'q' (or Ctrl-C / Ctrl-D) to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yguiapp/app.h>
#include <yetty/yguiapp/run.h>
#include <yetty/ygui/event.h>
#include <yetty/ygui/ygui.h>

#define COLOR_TEXT 0xFFE0E5E4u
#define COLOR_MUTED 0xFF9FA7A8u
#define COLOR_ACCENT 0xFF6BA892u

/* Per-run demo state: the scene columns the tabbar toggles between, and the
 * detail-scene label the toggle callback updates. Allocated once in build()
 * and threaded to the callbacks via their userdata pointer (no file-scope
 * state). Lives for the app's lifetime; the process owns it. */
struct demo_state {
    struct yetty_yclass_object *scenes[8];
    int count;
    struct yetty_yclass_object *detail_label;
};

static inline void err_ok(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

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
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(o);
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
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(o);
    if (YETTY_IS_ERR(layout_res)) {
        yetty_ycore_error_destroy(layout_res.error);
        return;
    }
    struct yetty_ygui_layout l = *layout_res.value;
    l.flex_grow = grow;
    err_ok(yetty_ygui_widget_layout_set(o, &l));
}

/* A leaf entry (file) — a fixed-height label row. */
static void add_leaf(struct yetty_yclass_object *parent, const char *text)
{
    struct yetty_yclass_object *leaf = add_obj(parent, yetty_ygui_label_class_get());
    if (!leaf) {
        return;
    }
    err_ok(yetty_ygui_label_set_text(leaf, text));
    err_ok(yetty_ygui_label_set_font_size(leaf, 13.0f));
    set_size(leaf, -1.0f, 22.0f);
}

/* A folder node — a tree_node with a label. Always created OPEN: tree_node
 * folds by hiding its in-flow children, and a folder's children are nested by
 * the caller AFTER this returns, so an initial closed state must be applied
 * with fold_folder() once the children exist (set_open only folds children
 * present at the time of the call). Returns the node for nesting. */
static struct yetty_yclass_object *add_folder(struct yetty_yclass_object *parent, const char *label)
{
    struct yetty_yclass_object *node = add_obj(parent, yetty_ygui_tree_node_class_get());
    if (!node) {
        return NULL;
    }
    err_ok(yetty_ygui_tree_node_set_label(node, label));
    return node;
}

/* Collapse a folder after its children have been added, hiding them. Call this
 * once the caller has finished nesting children under `node`. */
static void fold_folder(struct yetty_yclass_object *node)
{
    if (!node) {
        return;
    }
    err_ok(yetty_ygui_tree_node_set_open(node, 0));
}

/* Reveal exactly one scene, fold the rest away. */
static void show_scene(struct demo_state *state, int idx)
{
    for (int i = 0; i < state->count; i++) {
        err_ok(yetty_ygui_widget_set_visible(state->scenes[i], i == idx));
    }
}

static struct yetty_ycore_void_result on_tab_changed(struct yetty_yclass_object *target,
                                                     const struct yetty_ygui_event *event,
                                                     void *userdata)
{
    (void)target;
    struct demo_state *state = userdata;
    if (!state) {
        return YETTY_OK_VOID();
    }
    int idx = event ? event->i0 : 0;
    if (idx < 0) {
        idx = 0;
    }
    if (idx >= state->count) {
        idx = state->count - 1;
    }
    show_scene(state, idx);
    return YETTY_OK_VOID();
}

/* Begin a scene: an outer column registered for tab toggling. */
static struct yetty_yclass_object *begin_scene(struct demo_state *state,
                                               struct yetty_yclass_object *content)
{
    struct yetty_yclass_object *scene = add_obj(content, yetty_ygui_vbox_class_get());
    if (!scene) {
        return NULL;
    }
    set_grow(scene, 1.0f);
    if (state->count < (int)(sizeof(state->scenes) / sizeof(state->scenes[0]))) {
        state->scenes[state->count++] = scene;
    }
    return scene;
}

/* Scene 1 — a deep, nested project tree (no scroll). */
static void build_explorer_scene(struct demo_state *state, struct yetty_yclass_object *content)
{
    struct yetty_yclass_object *scene = begin_scene(state, content);
    if (!scene) {
        return;
    }
    struct yetty_yclass_object *root = add_folder(scene, "project/");
    set_grow(root, 1.0f);
    struct yetty_yclass_object *src = add_folder(root, "src/");
    struct yetty_yclass_object *yetty = add_folder(src, "yetty/");
    add_leaf(yetty, "main.c");
    struct yetty_yclass_object *ygui = add_folder(yetty, "ygui/");
    add_leaf(ygui, "widget.c");
    add_leaf(ygui, "layout.c");
    add_leaf(ygui, "tree_node.c");
    fold_folder(ygui); /* start collapsed — children are hidden */
    add_leaf(src, "util.c");
    struct yetty_yclass_object *docs = add_folder(root, "docs/");
    add_leaf(docs, "design.md");
    add_leaf(docs, "architecture.md");
    fold_folder(docs);
    add_leaf(root, "README.md");
}

/* Scene 2 — a tall tree inside a scrollarea. Many sibling folders so the
 * content overruns the viewport and a folder can be scrolled to the
 * middle before opening it (issue #79 reproduction). */
static void build_scrolled_scene(struct demo_state *state, struct yetty_yclass_object *content)
{
    struct yetty_yclass_object *scene = begin_scene(state, content);
    if (!scene) {
        return;
    }
    struct yetty_yclass_object *scroll = add_obj(scene, yetty_ygui_scrollarea_class_get());
    if (!scroll) {
        return;
    }
    set_grow(scroll, 1.0f);
    char label[32];
    for (int i = 1; i <= 20; i++) {
        snprintf(label, sizeof(label), "module_%02d/", i);
        struct yetty_yclass_object *folder = add_folder(scroll, label);
        if (!folder) {
            continue;
        }
        for (int k = 1; k <= 3; k++) {
            snprintf(label, sizeof(label), "file_%02d_%d.c", i, k);
            add_leaf(folder, label);
        }
        /* Every other folder starts collapsed so there is something to open
         * once it has been scrolled into the middle of the viewport. Fold
         * AFTER the children exist so they are actually hidden. */
        if (i % 2 != 0) {
            fold_folder(folder);
        }
    }
}

/* Scene 3 — a tree whose toggles update a side detail label. */
static struct yetty_ycore_void_result on_toggle(struct yetty_yclass_object *node, void *userdata)
{
    struct demo_state *state = userdata;
    struct yetty_ycore_int_result open_res = yetty_ygui_tree_node_is_open(node);
    int open = YETTY_IS_ERR(open_res) ? 0 : open_res.value;
    if (YETTY_IS_ERR(open_res)) {
        yetty_ycore_error_destroy(open_res.error);
    }
    if (state && state->detail_label) {
        err_ok(yetty_ygui_label_set_text(state->detail_label,
                                         open ? "last: opened a node" : "last: closed a node"));
    }
    return YETTY_OK_VOID();
}

static void build_detail_scene(struct demo_state *state, struct yetty_yclass_object *content)
{
    struct yetty_yclass_object *scene = begin_scene(state, content);
    if (!scene) {
        return;
    }
    {
        struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(scene);
        if (!YETTY_IS_ERR(layout_res)) {
            struct yetty_ygui_layout l = *layout_res.value;
            l.direction = YETTY_YGUI_FLEX_ROW;
            l.gap = 12.0f;
            err_ok(yetty_ygui_widget_layout_set(scene, &l));
        } else {
            yetty_ycore_error_destroy(layout_res.error);
        }
    }
    struct yetty_yclass_object *tree = add_folder(scene, "settings/");
    set_size(tree, 240.0f, -1.0f);
    struct yetty_yclass_object *appearance = add_folder(tree, "appearance/");
    err_ok(yetty_ygui_tree_node_on_toggle(appearance, on_toggle, state));
    add_leaf(appearance, "theme");
    add_leaf(appearance, "font size");
    fold_folder(appearance);
    struct yetty_yclass_object *network = add_folder(tree, "network/");
    err_ok(yetty_ygui_tree_node_on_toggle(network, on_toggle, state));
    add_leaf(network, "proxy");
    add_leaf(network, "timeout");
    fold_folder(network);

    state->detail_label = add_obj(scene, yetty_ygui_label_class_get());
    if (state->detail_label) {
        err_ok(yetty_ygui_label_set_text(state->detail_label, "toggle a folder to update this"));
        err_ok(yetty_ygui_label_set_font_size(state->detail_label, 14.0f));
        set_grow(state->detail_label, 1.0f);
    }
}

/* Demo app class: a yguiapp:app subclass with no extra state. */
struct [[clang::annotate("class@demoygui:43_tree_showcase")]] [[clang::annotate(
    "parent@yguiapp:app")]] yetty_demoygui_43_tree_showcase {
    int unused;
};

/* Result wrapper + class accessor forward-decls (this TU does not include its
 * own generated header; main.gen.c is #included at the foot). */
YETTY_YRESULT_DECLARE(yetty_demoygui_43_tree_showcase_ptr,
                      struct yetty_demoygui_43_tree_showcase *);
struct yetty_yclass_ptr_result yetty_demoygui_43_tree_showcase_class_get(void);

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{
    (void)app;
    {
        struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "43_tree_showcase: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.direction = YETTY_YGUI_FLEX_COLUMN;
        l.gap = 8.0f;
        l.padding_left = l.padding_right = l.padding_top = l.padding_bottom = 10.0f;
        err_ok(yetty_ygui_widget_layout_set(root, &l));
    }

    struct yetty_yclass_object *tabbar = add_obj(root, yetty_ygui_tabbar_class_get());
    if (tabbar) {
        set_size(tabbar, -1.0f, 36.0f);
        const char *labels[] = {"File explorer", "Scrolled tree", "Live detail"};
        for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
            struct yetty_yclass_object_ptr_result t = yetty_ygui_tabbar_add_tab(tabbar, labels[i]);
            if (YETTY_IS_ERR(t)) {
                yetty_ycore_error_destroy(t.error);
            }
        }
    }

    struct yetty_yclass_object *content = add_obj(root, yetty_ygui_vbox_class_get());
    if (!content) {
        return YETTY_ERR(yetty_ycore_void, "43_tree_showcase: content container");
    }
    set_grow(content, 1.0f);

    struct demo_state *state = calloc(1, sizeof(struct demo_state));
    if (!state) {
        return YETTY_ERR(yetty_ycore_void, "43_tree_showcase: state alloc");
    }

    build_explorer_scene(state, content);
    build_scrolled_scene(state, content);
    build_detail_scene(state, content);

    show_scene(state, 0);
    if (tabbar) {
        err_ok(yetty_ygui_widget_subscribe(tabbar, YETTY_YGUI_EVENT_VALUE_CHANGED, on_tab_changed,
                                           state));
        err_ok(yetty_ygui_tabbar_set_active(tabbar, 0));
    }
    (void)COLOR_TEXT;
    (void)COLOR_MUTED;
    (void)COLOR_ACCENT;
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv, yetty_demoygui_43_tree_showcase_class_get().value);
}

#include "main.gen.c"
