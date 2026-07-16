/*
 * app.h — central ydu application state.
 *
 * `struct ydu_app` owns the scanned filesystem tree, the current-directory
 * cursor and selection used for navigation, the ygui engine + widget tree, and
 * the interaction config. It is heap-allocated once in main() and threaded by
 * pointer through the client harness and the UI, mirroring ytop's shape.
 */
#ifndef YDU_APP_H
#define YDU_APP_H

#include "scan.h"

struct yetty_yclass_object;
struct ydu_ui;

struct ydu_app {
    /* ygui engine + root of the widget tree. */
    struct yetty_yclass_object *engine;
    struct yetty_yclass_object *root_widget;
    struct ydu_ui *ui; /* opaque widget handles, owned by ui.c */

    /* Scan model + navigation cursor. */
    struct ydu_node *tree; /* scanned root (owns the whole tree) */
    struct ydu_node *cwd;  /* directory currently in view */
    int selected;          /* index into cwd->children, in current sort order */
    enum ydu_sort_mode sort_mode;
    struct ydu_scan_stats stats;
    char root_path[4096];

    int running;
};

#endif /* YDU_APP_H */
