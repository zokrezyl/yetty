/*
 * ui.h — ytop presentation layer (platform-independent).
 *
 * Builds the btop-style multi-box dashboard on the ygui widget engine and maps
 * the latest platform snapshots onto those widgets each tick. Knows nothing
 * about /proc or any OS interface — it reads only struct ytop_app's snapshots.
 */
#ifndef YTOP_UI_H
#define YTOP_UI_H

#include <yetty/ycore/result.h>

#include "app.h"

/* Allocate the widget tree under app->root and store the handles in app->ui.
 * Sized to the first snapshot (core count, mount count). Call once, after the
 * first sample and after framework_set_root. */
struct yetty_ycore_void_result ytop_ui_build(struct ytop_app *app);

/* Free app->ui (the handle table; widgets are owned by the framework tree). */
void ytop_ui_free(struct ytop_app *app);

/* Push values from app's snapshots into the widgets. Best-effort: individual
 * setter failures are absorbed so one bad update never blanks the frame. */
void ytop_ui_refresh(struct ytop_app *app);

/* Recompute positions/sizes from the current viewport (SIGWINCH path). */
void ytop_ui_relayout(struct ytop_app *app);

#endif /* YTOP_UI_H */
