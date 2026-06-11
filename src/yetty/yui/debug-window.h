#ifndef YETTY_YUI_DEBUG_WINDOW_H
#define YETTY_YUI_DEBUG_WINDOW_H

/*
 * yetty_yui_debug_window — per-pane debug overlay, built on top of yui's
 * ygui engine.
 *
 * This is a ygui construct, in the same spirit as the tabbar / statusbar /
 * splitter widgets owned by yui: yui keeps an array of debug windows
 * (one per pane in the active workspace's tile tree) and reconciles them
 * every frame in yui_debug_windows_sync (see yui.c).
 *
 * Each debug window is a small ygui panel anchored at the top-right of
 * its pane with a single label child. Step 2 will replace the label with
 * actual content; the scaffold is just to make the panel visible.
 */

#include <stddef.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yui_debug_window;
struct yetty_ygui_framework;
struct yetty_yclass_object;
struct yetty_ywire_stats_snapshot;

YETTY_YRESULT_DECLARE(yetty_yui_debug_window_ptr, struct yetty_yui_debug_window *);

/* Build the widget tree under `engine`'s root. `pane_id` is woven into
 * a debug tag so multiple debug windows are distinguishable in traces. */
struct yetty_yui_debug_window_ptr_result yetty_yui_debug_window_create(
    struct yetty_ygui_framework *engine, yetty_ycore_object_id pane_id);

/* Removes the widget tree from the engine and frees the bookkeeping. */
struct yetty_ycore_void_result yetty_yui_debug_window_destroy(struct yetty_yui_debug_window *dw);

/* Reposition the debug panel inside its pane bounds. Called by yui's
 * per-frame reconcile pass. `pane_x`/`pane_y`/`pane_w`/`pane_h` are the
 * owning pane's rect; the debug window picks its own corner inside it. */
struct yetty_ycore_void_result yetty_yui_debug_window_layout(struct yetty_yui_debug_window *dw,
                                                             float pane_x, float pane_y,
                                                             float pane_w, float pane_h);

/* Push the latest wire-stats snapshot into the debug window's label.
 * Cheap on no-change (idempotent string compare inside the widget).
 * Passing NULL clears the label to "(no terminal)". */
struct yetty_ycore_void_result yetty_yui_debug_window_set_stats(
    struct yetty_yui_debug_window *dw, const struct yetty_ywire_stats_snapshot *snap);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YUI_DEBUG_WINDOW_H */
