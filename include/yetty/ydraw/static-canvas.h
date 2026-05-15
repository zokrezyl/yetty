/* static-canvas.h — public API for the static (non-scrolling) ydraw canvas.
 *
 * Viewport fixed, prims that don't fit are discarded. Used by yui chrome
 * (popups, dialogs, statusbar). Cursor pinned at (0,0); rolling_row_0
 * always 0; no scrollback, no eviction.
 *
 * Only a create is exposed — all other operations go through the
 * polymorphic surface defined in <yetty/ydraw/canvas.h>.
 */
#pragma once

#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw/canvas.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_context;

YETTY_ANNOT_CALLER_OWNED
struct yetty_ydraw_canvas_ptr_result yetty_ydraw_static_canvas_create(
    const struct yetty_context *context);

#ifdef __cplusplus
}
#endif
