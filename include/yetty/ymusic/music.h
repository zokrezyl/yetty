/* GENERATED — do not edit. */
/* Public interface for regular class(es) `music` (module: ymusic).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YMUSIC_MUSIC_H
#define YETTY_YCLASSGEN_YMUSIC_MUSIC_H

#include <yetty/yclass/class.h>
#include <yetty/ymusic/methods.h>

struct yetty_yclass_ptr_result yetty_ymusic_music_class_get(void);

struct yetty_ydraw_drawable_list;

/* Public constants — copied verbatim into the generated music.h. */
#define YETTY_YMUSIC_NO_ELEMENT (-1) /* hit_test: no element under the point */
#define YETTY_YMUSIC_FLAG_NONE 0x0u  /* reserved render flags */
struct yetty_ycore_void_result yetty_ymusic_emit_osc(const struct yetty_ydraw_drawable_list *list,
                                                     int fd);

#endif
