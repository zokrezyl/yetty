/* scrolling-canvas.h — public API for the scrolling (terminal) ypaint canvas.
 *
 * Viewport scrolls, scrollbuffer evicts old rows. Used by the terminal's
 * text + ypaint layers. The variant exposes ONLY a create — every other
 * operation goes through the polymorphic surface defined in
 * <yetty/ypaint/canvas.h>.
 */
#pragma once

#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ypaint/canvas.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_context;

/* Construct a scrolling-canvas variant and return its polymorphic base
 * pointer. The variant struct itself is not exposed — callers hold the
 * `struct yetty_ypaint_canvas *` and use the polymorphic API.
 *
 * On failure rolls back any partial allocations. */
YETTY_ANNOT_CALLER_OWNED
struct yetty_ypaint_canvas_ptr_result yetty_ypaint_scrolling_canvas_create(
    const struct yetty_context *context);

#ifdef __cplusplus
}
#endif
