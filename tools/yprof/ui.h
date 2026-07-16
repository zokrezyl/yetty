/*
 * ui.h — yprof presentation layer API.
 *
 * ui.c builds a two-pane dashboard: the flame graph (rendered by the yflame
 * class into a ydraw_embed canvas) above a synchronized top-symbol table. The
 * flame is comparatively expensive to rebuild (reconfigure + reparse), so it is
 * refreshed separately from the cheap symbol-table refresh used on cursor moves.
 */
#ifndef YPROF_UI_H
#define YPROF_UI_H

#include <yetty/ycore/result.h>

#include "app.h"

struct yetty_ycore_void_result yprof_ui_build(struct yprof_app *app);
void yprof_ui_free(struct yprof_app *app);
void yprof_ui_relayout(struct yprof_app *app);
void yprof_ui_refresh(struct yprof_app *app);       /* header + table + full flame rebuild */
void yprof_ui_refresh_table(struct yprof_app *app); /* header + table only (cheap) */

/* Re-emit the flame with the current focus / hover / highlight, WITHOUT
 * reparsing — cheap, used for cross-highlight, search, hover, and zoom. */
void yprof_ui_render_flame(struct yprof_app *app);

/* Route a pane-pixel mouse event (kind = enum yetty_client_input_mouse_kind) to
 * the flame: hover-highlight, click-zoom, and the up/root nav buttons. Converts
 * pane coordinates to flame-local ones using the embed's placement. */
void yprof_ui_flame_mouse(struct yprof_app *app, uint32_t kind, float pane_x, float pane_y,
                          int button, int pressed, float wheel_dy);

#endif /* YPROF_UI_H */
