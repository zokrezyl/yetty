/* config-dialog.c — yui Settings window.
 *
 * Two-pane window built once at yui_create. The widget tree:
 *
 *   yui_dlg_settings  (window, hidden at start)
 *     body            (auto vbox; padding + gap)
 *       split         (hbox, flex:1)
 *         left_scroll (scrollarea, fixed 220px)
 *           tree_box  (vbox)  ← tree_nodes attached here
 *         props       (textarea, flex:1)
 *       actions hbox
 *         Close button
 *
 * The tree is built by walking the live yconfig at create time: every
 * node that has at least one child becomes a tree_node; pure leaves
 * are skipped. Each tree_node's on_toggle handler updates the
 * right-pane textarea with that branch's direct-leaf children as
 * `key = value` lines.
 *
 * Memory model: each tree_node's on_toggle takes a per-node bundle
 * (dialog + path) as userdata. The bundles array is sized up-front by
 * counting branches, so the array never reallocates and the pointers
 * we hand out stay valid for the life of the dialog.
 */

#include "config-dialog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yconfig/config.h>
#include <yetty/ygui/ygui.h>
#include <yetty/yfigure/wire.h>

#define YUI_CFG_DLG_MAX_DEPTH 8
#define YUI_CFG_DLG_PATH_MAX 256

static inline void yetty_ycore_error_destroy_safe(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

struct path_bundle {
    struct yetty_yui_config_dialog *dlg;
    char path[YUI_CFG_DLG_PATH_MAX];
};

struct yetty_yui_config_dialog {
    struct yetty_ygui_framework *framework;    /* borrowed */
    const struct yetty_yconfig_config *config; /* borrowed */
    struct yetty_ygui_object *window;
    struct yetty_ygui_object *textarea;
    struct path_bundle *bundles; /* owned, sized at build */
    size_t bundles_count;
    size_t bundles_cap;
};

/* Add `cls` under `parent`, returning the new object or NULL (error
 * destroyed). Keeps the build code below readable. */
static struct yetty_ygui_object *cfg_add(struct yetty_ygui_object *parent,
                                         struct yetty_yclass_ptr_result cls_r)
{
    if (YETTY_IS_ERR(cls_r)) {
        yetty_ycore_error_destroy(cls_r.error);
        return NULL;
    }
    struct yetty_ygui_object_ptr_result r = yetty_ygui_add(cls_r.value, parent);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    return r.value;
}

/* Compose `parent/key` into `out`. Treats NULL/empty parent as "root",
 * yielding just `key`. Returns 1 on success, 0 if the combined path
 * would have been truncated. */
static int join_path(char *out, const char *parent, const char *key)
{
    int n;
    if (parent && parent[0]) {
        n = snprintf(out, YUI_CFG_DLG_PATH_MAX, "%s/%s", parent, key);
    } else {
        n = snprintf(out, YUI_CFG_DLG_PATH_MAX, "%s", key);
    }
    return n > 0 && (size_t)n < YUI_CFG_DLG_PATH_MAX;
}

/* Branch = node with at least one child. Counts every branch in the
 * subtree rooted at `path`. */
static int count_branches(const struct yetty_yconfig_config *cfg, const char *path, int depth)
{
    if (depth > YUI_CFG_DLG_MAX_DEPTH) {
        return 0;
    }
    int total = 0;
    int n = cfg->ops->get_child_count(cfg, path);
    for (int i = 0; i < n; i++) {
        const char *key = cfg->ops->get_child_key(cfg, path, i);
        if (!key) {
            continue;
        }
        char child_path[YUI_CFG_DLG_PATH_MAX];
        if (!join_path(child_path, path, key)) {
            continue;
        }
        if (cfg->ops->get_child_count(cfg, child_path) == 0) {
            continue;
        }
        total++;
        total += count_branches(cfg, child_path, depth + 1);
    }
    return total;
}

/* Repaint the right pane with the leaves of `path`. */
static void show_path(struct yetty_yui_config_dialog *dlg, const char *path)
{
    if (!dlg || !dlg->textarea || !dlg->config) {
        return;
    }
    char buf[8192];
    size_t off = 0;
    int written = snprintf(buf + off, sizeof(buf) - off, "%s\n\n", path && path[0] ? path : "/");
    if (written > 0) {
        off += (size_t)written;
    }

    int leaves = 0;
    int n = dlg->config->ops->get_child_count(dlg->config, path);
    for (int i = 0; i < n && off + 1 < sizeof(buf); i++) {
        const char *key = dlg->config->ops->get_child_key(dlg->config, path, i);
        if (!key) {
            continue;
        }
        char child_path[YUI_CFG_DLG_PATH_MAX];
        if (!join_path(child_path, path, key)) {
            continue;
        }
        if (dlg->config->ops->get_child_count(dlg->config, child_path) > 0) {
            continue;
        }
        const char *val = dlg->config->ops->get_string(dlg->config, child_path, "");
        written = snprintf(buf + off, sizeof(buf) - off, "%s = %s\n", key, val ? val : "");
        if (written > 0) {
            off += (size_t)written;
        }
        leaves++;
    }
    if (leaves == 0 && off + 1 < sizeof(buf)) {
        snprintf(buf + off, sizeof(buf) - off, "(no direct settings in this section)\n");
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_textarea_set_text(dlg->textarea, buf));
}

static struct yetty_ycore_void_result on_tree_toggle(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *node,
                                                     void *userdata)
{
    (void)ctx;
    (void)node;
    struct path_bundle *pb = userdata;
    if (!pb || !pb->dlg) {
        return YETTY_OK_VOID();
    }
    show_path(pb->dlg, pb->path);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result on_close(struct yetty_yclass_ctx *ctx,
                                               struct yetty_yclass_object *button, void *userdata)
{
    (void)ctx;
    (void)button;
    yetty_yui_config_dialog_hide(userdata);
    return YETTY_OK_VOID();
}

/* Recursively materialise tree_nodes for every branch under
 * `parent_path`. Children of a node are added directly under the
 * tree_node object itself. */
static void build_tree(struct yetty_yui_config_dialog *dlg, struct yetty_ygui_object *container,
                       const char *parent_path, int depth)
{
    if (!dlg || !container || depth > YUI_CFG_DLG_MAX_DEPTH) {
        return;
    }
    int n = dlg->config->ops->get_child_count(dlg->config, parent_path);
    for (int i = 0; i < n; i++) {
        const char *key = dlg->config->ops->get_child_key(dlg->config, parent_path, i);
        if (!key) {
            continue;
        }
        char child_path[YUI_CFG_DLG_PATH_MAX];
        if (!join_path(child_path, parent_path, key)) {
            continue;
        }
        if (dlg->config->ops->get_child_count(dlg->config, child_path) == 0) {
            continue;
        }
        if (dlg->bundles_count >= dlg->bundles_cap) {
            continue;
        }
        struct path_bundle *pb = &dlg->bundles[dlg->bundles_count++];
        pb->dlg = dlg;
        snprintf(pb->path, sizeof(pb->path), "%s", child_path);

        struct yetty_ygui_object *node = cfg_add(container, yetty_ygui_tree_node_class_get());
        if (!node) {
            continue;
        }
        yetty_ycore_error_destroy_safe(yetty_ygui_tree_node_set_label(node, key));
        yetty_ycore_error_destroy_safe(yetty_ygui_tree_node_on_toggle(node, on_tree_toggle, pb));

        /* Nested branches go directly under the tree_node. */
        build_tree(dlg, node, child_path, depth + 1);

        /* Start collapsed: folds this node's children (added above) so
         * the tree opens as a clean list of top-level categories the
         * user expands on demand. */
        yetty_ycore_error_destroy_safe(yetty_ygui_tree_node_set_open(node, 0));
    }
}

struct yetty_yui_config_dialog_ptr_result yetty_yui_config_dialog_create(
    struct yetty_ygui_framework *framework, const struct yetty_yconfig_config *config)
{
    if (!framework) {
        return YETTY_ERR(yetty_yui_config_dialog_ptr, "config_dialog: NULL framework");
    }
    if (!config || !config->ops) {
        return YETTY_ERR(yetty_yui_config_dialog_ptr, "config_dialog: NULL config");
    }
    if (!config->ops->get_child_count || !config->ops->get_child_key) {
        return YETTY_ERR(yetty_yui_config_dialog_ptr,
                         "config_dialog: yconfig missing child iteration ops");
    }
    struct yetty_ygui_object *root = yetty_ygui_framework_root(framework);
    if (!root) {
        return YETTY_ERR(yetty_yui_config_dialog_ptr, "config_dialog: framework has no root");
    }

    struct yetty_yui_config_dialog *dlg = calloc(1, sizeof(*dlg));
    if (!dlg) {
        return YETTY_ERR(yetty_yui_config_dialog_ptr, "config_dialog: alloc");
    }
    dlg->framework = framework;
    dlg->config = config;

    /* Reserve room for every branch up front so the bundle array never
     * reallocates and the on_toggle userdata pointers stay valid. */
    int branches = count_branches(config, NULL, 0);
    if (branches > 0) {
        dlg->bundles = calloc((size_t)branches, sizeof(*dlg->bundles));
        if (!dlg->bundles) {
            free(dlg);
            return YETTY_ERR(yetty_yui_config_dialog_ptr, "config_dialog: bundles alloc");
        }
        dlg->bundles_cap = (size_t)branches;
    }

    struct yetty_ygui_object *win = cfg_add(root, yetty_ygui_window_class_get());
    if (!win) {
        free(dlg->bundles);
        free(dlg);
        return YETTY_ERR(yetty_yui_config_dialog_ptr, "config_dialog: window alloc");
    }
    dlg->window = win;
    yetty_ycore_error_destroy_safe(yetty_ygui_window_set_title(win, "Settings"));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(win, 720.0f, 460.0f));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_position(win, 140.0f, 90.0f));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(win, 0));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_floating(win, 1));

    struct yetty_ygui_object *body = yetty_ygui_window_body(win);
    if (!body) {
        return YETTY_OK(yetty_yui_config_dialog_ptr, dlg);
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_apply_css(
        body, "display:flex;flex-direction:column;gap:10;padding:14 14 14 14;"));

    struct yetty_ygui_object *split = cfg_add(body, yetty_ygui_hbox_class_get());
    if (split) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_apply_css(
            split, "display:flex;flex-direction:row;gap:12;flex:1 1 0;align-items:stretch;"));

        struct yetty_ygui_object *left_scroll = cfg_add(split, yetty_ygui_scrollarea_class_get());
        if (left_scroll) {
            yetty_ycore_error_destroy_safe(
                yetty_ygui_widget_apply_css(left_scroll, "flex:0 0 220;align-self:stretch;"));
            /* apply_css's flex shorthand doesn't carry the basis into a
             * width, so pin the left tree column at a fixed 220px (it
             * must not flex-grow or collapse onto the right pane). */
            struct yetty_ygui_layout sl = *yetty_ygui_widget_layout_get(left_scroll);
            sl.width = 220.0f;
            sl.flex_grow = 0.0f;
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_layout_set(left_scroll, &sl));

            struct yetty_ygui_object *tree_box = cfg_add(left_scroll, yetty_ygui_vbox_class_get());
            if (tree_box) {
                yetty_ycore_error_destroy_safe(yetty_ygui_widget_apply_css(
                    tree_box, "display:flex;flex-direction:column;gap:2;align-items:stretch;"));
                build_tree(dlg, tree_box, NULL, 0);
            }
        }

        struct yetty_ygui_object *right = cfg_add(split, yetty_ygui_textarea_class_get());
        if (right) {
            yetty_ycore_error_destroy_safe(
                yetty_ygui_widget_apply_css(right, "flex:1 1 0;align-self:stretch;"));
            yetty_ycore_error_destroy_safe(yetty_ygui_textarea_set_text(
                right, "Select a category on the left to view its settings."));
            dlg->textarea = right;
        }
    }

    /* Authored height on the actions row so flex reserves the space up
     * front (see gpu_info dialog comment in yui.c). */
    struct yetty_ygui_object *actions = cfg_add(body, yetty_ygui_hbox_class_get());
    if (actions) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(actions, 0.0f, 36.0f));
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_apply_css(
            actions, "display:flex;flex-direction:row;justify-content:end;gap:8;"
                     "flex:0 0 auto;align-items:center;"));
        struct yetty_ygui_object *close = cfg_add(actions, yetty_ygui_button_class_get());
        if (close) {
            yetty_ycore_error_destroy_safe(yetty_ygui_button_set_label(close, "Close"));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(close, 80.0f, 28.0f));
            yetty_ycore_error_destroy_safe(yetty_ygui_clickable_on_click_set(close, on_close, dlg));
        }
    }

    return YETTY_OK(yetty_yui_config_dialog_ptr, dlg);
}

void yetty_yui_config_dialog_destroy(struct yetty_yui_config_dialog *dlg)
{
    if (!dlg) {
        return;
    }
    free(dlg->bundles);
    free(dlg);
}

void yetty_yui_config_dialog_show(struct yetty_yui_config_dialog *dlg)
{
    if (!dlg || !dlg->window) {
        return;
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(dlg->window, 1));
    if (dlg->framework) {
        yetty_ygui_framework_mark_dirty(dlg->framework);
    }
}

void yetty_yui_config_dialog_hide(struct yetty_yui_config_dialog *dlg)
{
    if (!dlg || !dlg->window) {
        return;
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(dlg->window, 0));
    if (dlg->framework) {
        yetty_ygui_framework_mark_dirty(dlg->framework);
    }
}

int yetty_yui_config_dialog_is_visible(const struct yetty_yui_config_dialog *dlg)
{
    if (!dlg || !dlg->window) {
        return 0;
    }
    return yetty_ygui_widget_is_visible(dlg->window);
}
