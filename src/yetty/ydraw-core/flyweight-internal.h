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

/* Handler function — takes prim_type, returns base ops pointer or error. */
typedef struct yetty_ydraw_core_prim_base_ops_ptr_result (*yetty_ydraw_prim_handler_fn)(
    uint32_t prim_type);

/* Get flyweight for primitive (tries default first, then by type range). */
struct yetty_ydraw_core_prim_flyweight_ptr_result yetty_ydraw_core_flyweight_registry_get(
    const struct yetty_ydraw_core_flyweight_registry *reg, uint32_t prim_type,
    const uint32_t *prim_data);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_CORE_FLYWEIGHT_INTERNAL_H */
