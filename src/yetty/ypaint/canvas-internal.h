/* canvas — module-internal declarations (ypaint only).
 *
 * These accessors and helpers are not part of the inter-module public API.
 * The public surface lives in include/yetty/ypaint/core/ypaint-canvas.h.
 *
 * They remain declared here (rather than as `static` in ypaint-canvas.c) so
 * other ypaint TUs can call them as the module grows; today the only caller
 * is ypaint-canvas.c itself.
 */
#ifndef YETTY_YPAINT_CANVAS_INTERNAL_H
#define YETTY_YPAINT_CANVAS_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ypaint/core/ypaint-canvas.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_void_result yetty_ypaint_canvas_mark_dirty(struct yetty_ypaint_canvas *canvas);

struct yetty_ycore_void_result yetty_ypaint_canvas_clear_staging(
    struct yetty_ypaint_canvas *canvas);

uint32_t yetty_ypaint_canvas_prim_gpu_size(struct yetty_ypaint_canvas *canvas);

bool yetty_ypaint_canvas_empty(struct yetty_ypaint_canvas *canvas);

struct yetty_ypaint_font *yetty_ypaint_canvas_get_default_font(struct yetty_ypaint_canvas *canvas);

struct yetty_ypaint_core_flyweight_registry;
const struct yetty_ypaint_core_flyweight_registry *yetty_ypaint_canvas_get_flyweight_registry(
    struct yetty_ypaint_canvas *canvas);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPAINT_CANVAS_INTERNAL_H */
