/*
 * ui.h — yperf presentation layer API.
 *
 * ui.c builds a two-pane dashboard: the flame graph (rendered by the yflame
 * class into a ydraw_embed canvas) above a synchronized top-symbol table. The
 * flame is comparatively expensive to rebuild (reconfigure + reparse), so it is
 * refreshed separately from the cheap symbol-table refresh used on cursor moves.
 */
#ifndef YPERF_UI_H
#define YPERF_UI_H

#include <yetty/ycore/result.h>

#include "app.h"

struct yetty_ycore_void_result yperf_ui_build(struct yperf_app *app);
void yperf_ui_free(struct yperf_app *app);
void yperf_ui_relayout(struct yperf_app *app);
void yperf_ui_refresh(struct yperf_app *app);       /* header + table + full flame rebuild */
void yperf_ui_refresh_table(struct yperf_app *app); /* header + table only (cheap) */

/* Re-emit the flame with the current focus / hover / highlight, WITHOUT
 * reparsing — cheap, used for cross-highlight, search, hover, and zoom. */
void yperf_ui_render_flame(struct yperf_app *app);

/* Route a pane-pixel mouse event (kind = enum yetty_client_input_mouse_kind) to
 * the flame: hover-highlight, click-zoom, and the up/root nav buttons. Converts
 * pane coordinates to flame-local ones using the embed's placement. */
void yperf_ui_flame_mouse(struct yperf_app *app, uint32_t kind, float pane_x, float pane_y,
                          int button, int pressed, float wheel_dy);

#endif /* YPERF_UI_H */
