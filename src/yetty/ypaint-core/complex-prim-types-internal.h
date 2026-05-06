/* complex-prim-types — module-internal declarations (ypaint-core only).
 *
 * The public surface lives in include/yetty/ypaint-core/complex-prim-types.h.
 */
#ifndef YETTY_YPAINT_CORE_COMPLEX_PRIM_TYPES_INTERNAL_H
#define YETTY_YPAINT_CORE_COMPLEX_PRIM_TYPES_INTERNAL_H

#include <stddef.h>
#include <yetty/ypaint-core/complex-prim-types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Get total size (reads FAM header) */
size_t yetty_ypaint_core_complex_prim_size(const void *data);

/* Get concrete factory by type id */
struct yetty_ypaint_core_concrete_factory *yetty_ypaint_core_complex_prim_factory_get(
    struct yetty_ypaint_core_complex_prim_factory *factory, uint32_t type_id);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPAINT_CORE_COMPLEX_PRIM_TYPES_INTERNAL_H */
