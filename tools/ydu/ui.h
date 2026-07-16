/*
 * ui.h — ydu presentation layer API.
 *
 * ui.c builds a two-pane dashboard (a squarified treemap of the current
 * directory beside a synchronized sortable table) from ygui widgets and paints
 * the treemap into a ydraw_embed canvas. The harness in main.c calls build once
 * and refresh/relayout on navigation, sort changes, and resize.
 */
#ifndef YDU_UI_H
#define YDU_UI_H

#include <yetty/ycore/result.h>

#include "app.h"

struct yetty_ycore_void_result ydu_ui_build(struct ydu_app *app);
void ydu_ui_free(struct ydu_app *app);
void ydu_ui_relayout(struct ydu_app *app);
void ydu_ui_refresh(struct ydu_app *app);

#endif /* YDU_UI_H */
