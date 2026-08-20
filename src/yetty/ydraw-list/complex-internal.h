/* complex — module-internal declarations (ydraw-list only).
 *
 * The public surface lives in include/yetty/ydraw-list/complex.h.
 */
#ifndef YETTY_YDRAW_CORE_COMPLEX_INTERNAL_H
#define YETTY_YDRAW_CORE_COMPLEX_INTERNAL_H

#include <stddef.h>
#include <yetty/ydraw-list/complex.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Get total size (reads FAM header) */
size_t yetty_ydraw_complex_record_size(const void *data);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_CORE_COMPLEX_INTERNAL_H */
