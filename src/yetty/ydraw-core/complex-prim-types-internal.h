/* complex-prim-types — module-internal declarations (ydraw-core only).
 *
 * The public surface lives in include/yetty/ydraw-core/figure-types.h.
 */
#ifndef YETTY_YDRAW_CORE_COMPLEX_PRIM_TYPES_INTERNAL_H
#define YETTY_YDRAW_CORE_COMPLEX_PRIM_TYPES_INTERNAL_H

#include <stddef.h>
#include <yetty/ydraw-core/figure-types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Get total size (reads FAM header) */
size_t yetty_ydraw_raw_figure_size(const void *data);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_CORE_COMPLEX_PRIM_TYPES_INTERNAL_H */
