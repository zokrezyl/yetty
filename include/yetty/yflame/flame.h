/* GENERATED — do not edit. */
/* Public interface for regular class(es) `flame` (module: yflame).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YFLAME_FLAME_H
#define YETTY_YCLASSGEN_YFLAME_FLAME_H

#include <yetty/yclass/class.h>
#include <yetty/yflame/methods.h>

struct yetty_yclass_ptr_result yetty_yflame_flame_class_get(void);

struct yetty_ydraw_drawable_list;

/* Public flags — copied verbatim into the generated flame.h. */
#define YETTY_YFLAME_FLAG_LABELS 0x1u /* draw truncated frame-name labels */
#define YETTY_YFLAME_FLAG_ICICLE 0x2u /* root at top, growing down (vs flame: bottom-up) */
/* hit_test returns these (in place of a node id) when a navigation button is
 * under the point: the caller should focus the parent / reset to root. */
#define YETTY_YFLAME_HIT_UP (-2)
#define YETTY_YFLAME_HIT_ROOT (-3)
struct yetty_ycore_void_result yetty_yflame_emit_osc(const struct yetty_ydraw_drawable_list *list, int fd);

#endif
