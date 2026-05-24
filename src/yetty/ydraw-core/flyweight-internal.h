/* flyweight — module-internal declarations (ydraw-core only).
 *
 * The public surface lives in include/yetty/ydraw-core/flyweight.h.
 */
#ifndef YETTY_YDRAW_CORE_FLYWEIGHT_INTERNAL_H
#define YETTY_YDRAW_CORE_FLYWEIGHT_INTERNAL_H

#include <stdint.h>
#include <yetty/ydraw-core/flyweight.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Handler function — takes drawable_type, returns base ops pointer or error. */
typedef struct yetty_ydraw_drawable_base_ops_ptr_result (*yetty_ydraw_drawable_handler_fn)(
    uint32_t drawable_type);

/* Get flyweight for primitive (tries default first, then by type range).
 * `drawable_data[0]` is the type — read inside; no separate type param. */
struct yetty_ydraw_drawable_flyweight_ptr_result yetty_ydraw_flyweight_registry_get(
    const struct yetty_ydraw_flyweight_registry *reg, const uint32_t *drawable_data);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_CORE_FLYWEIGHT_INTERNAL_H */
