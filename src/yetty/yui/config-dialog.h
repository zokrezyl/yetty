/* config-dialog.h — settings window for the yui app menu.
 *
 * Builds a two-pane modal-ish window once at yui_create time and keeps
 * it hidden until the user picks "Settings…" from the hamburger or
 * right-click menu. Left pane: scrollable tree of yconfig branches
 * (every node that has children). Right pane: read-only textarea
 * listing the most-recently-toggled branch's direct leaves as
 * `key = value`. Construction is read-only against the live yconfig —
 * editing is not wired yet.
 *
 * Lifetime: created/destroyed by yui.c. The dialog widget tree is
 * owned by the ygui engine; this module owns only the small heap
 * array of per-tree-node callback bundles.
 */
#ifndef YETTY_YUI_CONFIG_DIALOG_H
#define YETTY_YUI_CONFIG_DIALOG_H

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygui_framework;
struct yetty_yconfig_config;
struct yetty_yui_config_dialog;

YETTY_YRESULT_DECLARE(yetty_yui_config_dialog_ptr, struct yetty_yui_config_dialog *);

/* Build the dialog widget tree under `engine`'s root and prime the
 * left-pane tree from `config`. The dialog starts hidden. `config` is
 * borrowed (yetty owns it; yui outlives this dialog but is outlived by
 * yetty). Returns the new handle, or an error if widget allocation
 * failed. */
struct yetty_yui_config_dialog_ptr_result yetty_yui_config_dialog_create(
    struct yetty_ygui_framework *engine, const struct yetty_yconfig_config *config);

/* Free the heap-owned callback bundles. The widget tree is destroyed
 * by engine_destroy — do not touch widgets here. NULL-safe. */
void yetty_yui_config_dialog_destroy(struct yetty_yui_config_dialog *dlg);

/* Toggle visibility. Show also re-marks the engine dirty so the next
 * frame paints the dialog. NULL-safe. */
void yetty_yui_config_dialog_show(struct yetty_yui_config_dialog *dlg);
void yetty_yui_config_dialog_hide(struct yetty_yui_config_dialog *dlg);

/* True iff the dialog window is currently visible. Used by yui's
 * is_active to capture pointer events while the dialog is open. */
int yetty_yui_config_dialog_is_visible(const struct yetty_yui_config_dialog *dlg);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YUI_CONFIG_DIALOG_H */
