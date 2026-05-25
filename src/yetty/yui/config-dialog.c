/* config-dialog.c — yui Settings window.
 *
 * Two-pane window built once at yui_create. The widget tree:
 *
 *   yui_dlg_settings  (window, hidden at start)
 *     body            (auto vbox; padding + gap)
 *       split         (hbox, flex:1)
 *         left_scroll (scrollarea, fixed 220px)
 *           tree_box  (vbox)  ← tree_nodes attached here
 *         props       (textarea_wrapped, flex:1)
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
#include <yetty/ygui-old/ygui.h>

#define YUI_CFG_DLG_MAX_DEPTH 8
#define YUI_CFG_DLG_PATH_MAX 256

struct path_bundle {
    struct yetty_yui_config_dialog *dlg;
    char path[YUI_CFG_DLG_PATH_MAX];
};

struct yetty_yui_config_dialog {
    struct yetty_ygui_old_engine *engine;          /* borrowed */
    const struct yetty_yconfig_config *config; /* borrowed */
    struct yetty_ygui_old_widget *window;
    struct yetty_ygui_old_widget *textarea;
    struct path_bundle *bundles; /* owned, sized at build */
    size_t bundles_count;
    size_t bundles_cap;
};

/* Compose `parent/key` into `out`. Treats NULL/empty parent as "root",
 * yielding just `key`. Returns 1 on success, 0 if the combined path
 * would have been truncated — the caller is expected to skip that
 * branch instead of using a half-built path that won't resolve. */
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
 * subtree rooted at `path` so the caller can reserve exactly that
 * many bundle slots before tree construction begins. */
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

/* Repaint the right pane with the leaves of `path`. Header line is the
 * path itself; one `key = value` line per direct-leaf child. Nodes
 * that are themselves branches are skipped — those show up on the
 * left tree, not on the right. */
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
    yetty_ygui_old_widget_textarea_set_text(dlg->textarea, buf);
}

static void on_tree_toggle(struct yetty_ygui_old_widget *node, int expanded, void *userdata)
{
    (void)node;
    (void)expanded;
    struct path_bundle *pb = userdata;
    if (!pb || !pb->dlg) {
        return;
    }
    show_path(pb->dlg, pb->path);
}

static void on_close(struct yetty_ygui_old_widget *button, void *userdata)
{
    (void)button;
    yetty_yui_config_dialog_hide(userdata);
}

/* Recursively materialise tree_nodes for every branch under
 * `parent_path`. Bundle array has been pre-sized; we just append. */
static void build_tree(struct yetty_yui_config_dialog *dlg, struct yetty_ygui_old_widget *container,
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

        /* Node id encodes the full path so engine_find can hit each
         * tree_node uniquely. Slashes are fine — other yui widget ids
         * already use them (e.g. "yui_dlg_gpu_info/text"). */
        char node_id[YUI_CFG_DLG_PATH_MAX + 32];
        snprintf(node_id, sizeof(node_id), "yui_dlg_settings/tree/%s", child_path);
        struct yetty_ygui_old_widget *node = yetty_ygui_old_engine_tree_node(dlg->engine, node_id, key);
        if (!node) {
            continue;
        }
        yetty_ygui_old_widget_add_child(container, node);
        yetty_ygui_old_widget_tree_node_on_toggle(node, on_tree_toggle, pb);

        struct yetty_ygui_old_widget *children = yetty_ygui_old_widget_tree_node_children(node);
        if (children) {
            build_tree(dlg, children, child_path, depth + 1);
        }
    }
}

struct yetty_yui_config_dialog_ptr_result yetty_yui_config_dialog_create(
    struct yetty_ygui_old_engine *engine, const struct yetty_yconfig_config *config)
{
    if (!engine) {
        return YETTY_ERR(yetty_yui_config_dialog_ptr, "config_dialog: NULL engine");
    }
    if (!config || !config->ops) {
        return YETTY_ERR(yetty_yui_config_dialog_ptr, "config_dialog: NULL config");
    }
    /* The yconfig API we walk lives on .ops — guard against an older
     * config impl missing the new accessors instead of crashing on the
     * first call inside build_tree. */
    if (!config->ops->get_child_count || !config->ops->get_child_key) {
        return YETTY_ERR(yetty_yui_config_dialog_ptr,
                         "config_dialog: yconfig missing child iteration ops");
    }

    struct yetty_yui_config_dialog *dlg = calloc(1, sizeof(*dlg));
    if (!dlg) {
        return YETTY_ERR(yetty_yui_config_dialog_ptr, "config_dialog: alloc");
    }
    dlg->engine = engine;
    dlg->config = config;

    /* Reserve room for every branch we're about to materialise. Doing
     * it up front means the bundle array never reallocates, so the
     * pointers handed to tree_node_on_toggle stay valid forever. */
    int branches = count_branches(config, NULL, 0);
    if (branches > 0) {
        dlg->bundles = calloc((size_t)branches, sizeof(*dlg->bundles));
        if (!dlg->bundles) {
            free(dlg);
            return YETTY_ERR(yetty_yui_config_dialog_ptr, "config_dialog: bundles alloc");
        }
        dlg->bundles_cap = (size_t)branches;
    }

    struct yetty_ygui_old_widget *win =
        yetty_ygui_old_engine_window(engine, "yui_dlg_settings", /*x=*/140.0f, /*y=*/90.0f,
                                 /*w=*/720.0f, /*h=*/460.0f, "Settings");
    if (!win) {
        free(dlg->bundles);
        free(dlg);
        return YETTY_ERR(yetty_yui_config_dialog_ptr, "config_dialog: window alloc");
    }
    dlg->window = win;
    yetty_ygui_old_widget_set_visible(win, 0);

    struct yetty_ygui_old_widget *body = yetty_ygui_old_widget_window_body(win);
    if (!body) {
        return YETTY_OK(yetty_yui_config_dialog_ptr, dlg);
    }
    yetty_ygui_old_widget_apply_css(body,
                                "display:flex;flex-direction:column;gap:10;padding:14 14 14 14;");

    struct yetty_ygui_old_widget *split =
        yetty_ygui_old_engine_hbox(engine, "yui_dlg_settings/split", 0, 0, 0, 0);
    if (split) {
        yetty_ygui_old_widget_apply_css(
            split, "display:flex;flex-direction:row;gap:12;flex:1 1 0;align-items:stretch;");
        yetty_ygui_old_widget_add_child(body, split);

        struct yetty_ygui_old_widget *left_scroll =
            yetty_ygui_old_engine_scrollarea(engine, "yui_dlg_settings/tree_scroll", 0, 0, 0, 0);
        if (left_scroll) {
            yetty_ygui_old_widget_apply_css(left_scroll, "flex:0 0 220;align-self:stretch;");
            yetty_ygui_old_widget_add_child(split, left_scroll);

            struct yetty_ygui_old_widget *tree_box =
                yetty_ygui_old_engine_vbox(engine, "yui_dlg_settings/tree", 0, 0, 0, 0);
            if (tree_box) {
                yetty_ygui_old_widget_apply_css(
                    tree_box, "display:flex;flex-direction:column;gap:2;align-items:stretch;");
                yetty_ygui_old_widget_add_child(left_scroll, tree_box);
                build_tree(dlg, tree_box, NULL, 0);
            }
        }

        struct yetty_ygui_old_widget *right = yetty_ygui_old_engine_textarea_wrapped(
            engine, "yui_dlg_settings/props", 0, 0, 0, 0,
            "Select a category on the left to view its settings.");
        if (right) {
            yetty_ygui_old_widget_apply_css(right, "flex:1 1 0;align-self:stretch;");
            yetty_ygui_old_widget_add_child(split, right);
            dlg->textarea = right;
        }
    }

    /* See gpu_info dialog: authored_h on the actions row, not just
     * min-height. flex distributes free space using flex_basis =
     * authored_h, then applies min-height as a post-clamp; with a
     * basis of 0, the textarea above (flex:1) consumes the entire
     * body and the min-height re-grows the actions row past the
     * bottom edge. 36 reserves the row space up front. */
    struct yetty_ygui_old_widget *actions =
        yetty_ygui_old_engine_hbox(engine, "yui_dlg_settings/actions", 0, 0, 0, 36);
    if (actions) {
        yetty_ygui_old_widget_apply_css(actions,
                                    "display:flex;flex-direction:row;justify-content:end;gap:8;"
                                    "flex:0 0 auto;align-items:center;");
        yetty_ygui_old_widget_add_child(body, actions);
        struct yetty_ygui_old_widget *close =
            yetty_ygui_old_engine_button(engine, "yui_dlg_settings/close", 0, 0, 80, 28, "Close");
        if (close) {
            yetty_ygui_old_widget_button_on_click(close, on_close, dlg);
            yetty_ygui_old_widget_add_child(actions, close);
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
    yetty_ygui_old_widget_set_visible(dlg->window, 1);
    if (dlg->engine) {
        yetty_ygui_old_engine_mark_dirty(dlg->engine);
    }
}

void yetty_yui_config_dialog_hide(struct yetty_yui_config_dialog *dlg)
{
    if (!dlg || !dlg->window) {
        return;
    }
    yetty_ygui_old_widget_set_visible(dlg->window, 0);
    if (dlg->engine) {
        yetty_ygui_old_engine_mark_dirty(dlg->engine);
    }
}

int yetty_yui_config_dialog_is_visible(const struct yetty_yui_config_dialog *dlg)
{
    if (!dlg || !dlg->window) {
        return 0;
    }
    return yetty_ygui_old_widget_is_visible(dlg->window);
}
